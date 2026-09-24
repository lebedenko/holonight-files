#!/usr/bin/env python3
"""Sequential offscreen Release measurements; never automate desktop interaction."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import statistics
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

import performance_report

ROOT = Path(__file__).resolve().parent.parent
SCENARIOS = {
    'cache': 'PreviewPerformance.CacheReuse',
    'resize': 'PreviewPerformance.ResizeQuickLook',
    'pressure': 'PreviewPerformance.SelectionPressure',
}


def sample_fields(labels):
    return {f'{label}_{field}' for label in labels
            for field in ('pixels_ns', 'metadata_ns', 'attempts')}


REQUIRED = {
    'PreviewPerformance.GenerateFixtures': {'fixtures'},
    SCENARIOS['cache']: sample_fields(f'{phase}_{i}' for phase in ('cold', 'disk', 'memory') for i in range(6)),
    SCENARIOS['resize']: sample_fields([*(f'resize_{i}_{size}' for i in range(6)
                                        for size in (128, 256, 512, 1024, 1800)),
                                       *(f'{phase}_{i}' for phase in ('rapid', 'return') for i in range(6))]),
    SCENARIOS['pressure']: sample_fields([*(f'selection_{cycle}_{step}' for cycle in range(3)
                                          for step in range(12)), 'replacement']) | {
        'shutdown_ns', 'rss_start_kib', 'rss_shutdown_kib', 'attempts_total',
        'max_gui_timer_gap_ns', 'timer_ticks', *(f'rss_cycle_{i}_kib' for i in range(3))},
}


def measurements(xml, expected):
    try:
        tree = ET.parse(xml)
        cases = list(tree.iter('testcase'))
        if (len(cases) != 1 or cases[0].get('status') != 'run'
                or cases[0].get('result') != 'completed'
                or f"{cases[0].get('classname')}.{cases[0].get('name')}" != expected
                or list(tree.iter('skipped')) or list(tree.iter('failure'))):
            raise ValueError('expected one completed, passing scenario')
        result = {}
        for prop in cases[0].iter('property'):
            name = prop.attrib['name']
            value = int(prop.attrib['value'])
            if name in result or value < 0:
                raise ValueError('duplicate or negative measurement')
            result[name] = value
        if not result or set(result) != REQUIRED[expected]:
            raise ValueError('missing measurements')
        for name, value in result.items():
            if name.endswith('_metadata_ns') and value < result[name.replace('_metadata_ns', '_pixels_ns')]:
                raise ValueError('metadata completed before adequate pixels')
            if (name.startswith('rss_') or name == 'timer_ticks') and value == 0:
                raise ValueError('missing RSS or GUI timer observation')
        if expected == SCENARIOS['cache']:
            for i in range(6):
                if (result[f'cold_{i}_attempts'] != 1 or result[f'disk_{i}_attempts'] != 0
                        or result[f'memory_{i}_attempts'] != 0):
                    raise ValueError('incorrect cache reuse')
        if expected == 'PreviewPerformance.GenerateFixtures' and result['fixtures'] != 6:
            raise ValueError('incorrect fixture count')
        return result
    except (OSError, ET.ParseError, ValueError, KeyError) as error:
        raise RuntimeError(f'Invalid measurements in {xml}: {error}') from error


def run_trial(binary, directory, scenario, run, env, timeout=300):
    xml = directory / f'run-{run}.xml'
    log = directory / f'run-{run}.log'
    samples = []
    started = time.monotonic()
    usage = None
    with log.open('w') as output:
        process = subprocess.Popen([str(binary), f'--gtest_filter={scenario}',
                                    f'--gtest_output=xml:{xml}'],
                                   stdout=output, stderr=subprocess.STDOUT, env=env)
        try:
            while True:
                pid, status, usage = os.wait4(process.pid, os.WNOHANG)
                if pid:
                    process.returncode = os.waitstatus_to_exitcode(status)
                    break
                elapsed = time.monotonic() - started
                if elapsed > timeout:
                    raise RuntimeError(f'Benchmark timed out: {log}')
                try:
                    for line in Path(f'/proc/{process.pid}/status').read_text().splitlines():
                        if line.startswith('VmRSS:'):
                            samples.append({'elapsed_ms': round(elapsed * 1000),
                                            'rss_kib': int(line.split()[1])})
                            break
                except FileNotFoundError:
                    pass  # Child exited between wait4 and reading /proc.
                time.sleep(0.02)
        finally:
            if process.returncode is None:
                process.kill()
                process.wait()
            (directory / f'run-{run}-process.json').write_text(json.dumps({
                'returncode': process.returncode,
                'peak_rss_kib': usage.ru_maxrss if usage else None}, indent=2) + '\n')
            (directory / f'run-{run}-rss.json').write_text(json.dumps(samples, indent=2) + '\n')
    if process.returncode != 0:
        raise RuntimeError(f'Benchmark failed ({process.returncode}): {log}')
    if not samples or usage.ru_maxrss <= 0:
        raise RuntimeError(f'Missing process memory measurements: {log}')
    result = measurements(xml, scenario)
    result['peak_rss_kib'] = usage.ru_maxrss
    return result


def summary(results):
    names = set(results[0])
    if any(set(row) != names for row in results):
        raise RuntimeError('Measurement fields differ across trials')
    return {name: {'median': statistics.median(row[name] for row in results),
                   'min': min(row[name] for row in results),
                   'max': max(row[name] for row in results)} for name in sorted(names)}


def command(*args):
    return subprocess.check_output(args, text=True, cwd=ROOT).strip()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def provenance(binary, prefix):
    cache = binary.parent.parent / 'CMakeCache.txt'
    values = {}
    for line in cache.read_text().splitlines():
        if '=' in line and not line.startswith(('#', '//')):
            key, value = line.split('=', 1)
            values[key.split(':')[0]] = value
    if values.get('BUILD_TESTING') != 'ON':
        raise RuntimeError('Measurements require testing enabled')
    if values.get('CMAKE_BUILD_TYPE') != 'Release':
        raise RuntimeError('Measurements require a Release build')
    if Path(values['CMAKE_HOME_DIRECTORY']).resolve() != ROOT:
        raise RuntimeError('Binary build must belong to this instrumented source checkout')
    if str(prefix) not in values.get('CMAKE_PREFIX_PATH', '').split(';'):
        raise RuntimeError('Provider prefix differs from the configured build')
    files = ['scripts/performance_report.py', 'tests/fixtures/dbus-session.conf',
             'scripts/measure-preview.py', 'tests/preview_performance_test.cpp',
             'tests/preview_service_test_access.h', 'tests/preview_fixtures.h', 'tests/smoke.cpp',
             'tests/CMakeLists.txt']
    providers = {}
    for name in ('holonight-config', 'holonight-qt', 'holonight-images'):
        repo = ROOT.parent / name
        providers[name] = {'revision': command('git', '-C', str(repo), 'rev-parse', 'HEAD'),
                           'status': command('git', '-C', str(repo), 'status', '--porcelain')}
    state = ROOT / 'build/deps/provider-revisions.tsv'
    provider_builds = {}
    for line in state.read_text().splitlines():
        name, source, revision = line.split('\t')
        if providers[name]['revision'] != revision:
            raise RuntimeError(f'Provider source does not match installed evidence: {name}')
        repo = ROOT.parent / name
        for folder in ('src', 'include'):
            for path in (repo / folder).rglob('*'):
                if path.is_file():
                    committed = subprocess.check_output(['git', '-C', str(repo), 'show',
                        f'{revision}:{path.relative_to(repo)}'])
                    if hashlib.sha256(committed).hexdigest() != digest(path):
                        raise RuntimeError(f'Provider production sources changed: {path}')
        provider_cache = ROOT / 'build/deps' / name / 'CMakeCache.txt'
        content = provider_cache.read_text()
        if f'CMAKE_INSTALL_PREFIX:PATH={prefix}\n' not in content:
            raise RuntimeError(f'Provider prefix mismatch: {name}')
        if ('CMAKE_BUILD_TYPE:STRING=Release\n' not in content or
                f'CMAKE_CXX_COMPILER:FILEPATH={values["CMAKE_CXX_COMPILER"]}\n' not in content):
            raise RuntimeError(f'Incompatible provider build: {name}')
        if name != 'holonight-config' and f'Qt6Core_DIR:PATH={values["Qt6Core_DIR"]}\n' not in content:
            raise RuntimeError(f'Incompatible provider Qt: {name}')
        provider_builds[name] = {'recorded_source': source, 'recorded_revision': revision,
                                'cmake_cache': content, 'cmake_cache_sha256': digest(provider_cache)}
    if set(provider_builds) != set(providers):
        raise RuntimeError('Incomplete installed provider evidence')
    libraries = {str(path.relative_to(prefix)): digest(path)
                 for path in sorted((prefix / 'lib').glob('libholonight*')) if path.is_file()}
    return {'revision': command('git', 'rev-parse', 'HEAD'),
            'source_diff_sha256': hashlib.sha256(command('git', 'diff', 'HEAD').encode()).hexdigest(),
            'instrumentation_sha256': {name: digest(ROOT / name) for name in files},
            'production_sha256': {str(path.relative_to(ROOT)): digest(path)
                                  for path in sorted((ROOT / 'apps').rglob('*')) if path.is_file()},
            'binary_sha256': digest(binary), 'cmake_cache_sha256': digest(cache),
            'compiler': command(values['CMAKE_CXX_COMPILER'], '--version'),
            'qt': command('pkg-config', '--modversion', 'Qt6Core'),
            'system': platform.platform(), 'cpu': command('lscpu'),
            'providers': providers, 'provider_builds': provider_builds, 'installed_library_sha256': libraries,
            'configuration': {key: value for key, value in values.items()
                              if key in ('CMAKE_BUILD_TYPE', 'CMAKE_CXX_COMPILER', 'CMAKE_CXX_FLAGS',
                                         'CMAKE_CXX_FLAGS_RELEASE', 'CMAKE_PREFIX_PATH', 'QML_IMPORT_PATH',
                                         'BUILD_TESTING')},
            'rss_note': 'Fixtures are generated in a separate process. RSS includes allocator retention; '
                        'filesystem caches are not flushed. Offscreen is not native rendering acceptance.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--scenario', choices=[*SCENARIOS, 'all'], default='all')
    parser.add_argument('--prefix', type=Path, default=ROOT / 'build/deps/prefix')
    parser.add_argument('--private-bus', action='store_true', help=argparse.SUPPRESS)
    args = parser.parse_args()
    if not args.private_bus:
        return subprocess.call(['dbus-run-session',
                                f'--config-file={ROOT}/tests/fixtures/dbus-session.conf', '--',
                                sys.executable, str(Path(__file__).resolve()), *sys.argv[1:], '--private-bus'])
    binary = args.binary.resolve(strict=True)
    prefix = args.prefix.resolve(strict=True)
    metadata = provenance(binary, prefix)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise RuntimeError('Use an empty output directory to preserve earlier evidence')
    (output / 'environment.json').write_text(json.dumps(metadata, indent=2) + '\n')
    env = dict(os.environ, QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software',
               QT_SCALE_FACTOR='1', QSG_RHI_BACKEND='software',
               QML_IMPORT_PATH=str(prefix / 'lib/qt6/qml'), LD_LIBRARY_PATH=str(prefix / 'lib'),
               FILES_PREVIEW_PERFORMANCE='1',
               FILES_PERFORMANCE_FIXTURES=str(output / 'fixtures'))
    # No user cache, configuration or runtime paths enter benchmark processes.
    def isolated_env(directory):
        isolated = dict(env)
        for variable, child in (('HOME', 'home'), ('XDG_CACHE_HOME', 'cache'),
                                ('XDG_CONFIG_HOME', 'config'), ('XDG_DATA_HOME', 'data'),
                                ('XDG_STATE_HOME', 'state'), ('XDG_RUNTIME_DIR', 'runtime')):
            path = directory / child
            path.mkdir(parents=True, mode=0o700)
            isolated[variable] = str(path)
        return isolated

    generation = output / 'generation'
    generation.mkdir()
    run_trial(binary, generation, 'PreviewPerformance.GenerateFixtures', 0,
              isolated_env(generation))
    fixtures = sorted((output / 'fixtures').iterdir())
    if {path.name for path in fixtures} != {'0.png', '1.png', '2.png', '3.jpg', '4.jpg', '5.jpg'}:
        raise RuntimeError('Incorrect fixture set')
    (output / 'fixtures.json').write_text(json.dumps(
        {path.name: {'sha256': digest(path), 'bytes': path.stat().st_size} for path in fixtures},
        indent=2) + '\n')
    scenarios = SCENARIOS if args.scenario == 'all' else {args.scenario: SCENARIOS[args.scenario]}
    report = performance_report.metadata(
        ROOT, binary, prefix, metadata, scenarios, REQUIRED,
        performance_report.read_json(output / 'fixtures.json'))
    summaries = {}
    failures = []
    for name, scenario in scenarios.items():
        directory = output / name if args.scenario == 'all' else output
        directory.mkdir(exist_ok=True)
        results = []
        for run in range(5):
            print(f'{name}: trial {run + 1}/5', flush=True)
            try:
                results.append(run_trial(binary, directory, scenario, run,
                                         isolated_env(directory / f'trial-{run}')))
            except RuntimeError as error:
                failures.append({'scenario': name, 'trial': run, 'error': str(error)})
                (output / 'failures.json').write_text(json.dumps(failures, indent=2) + '\n')
                print(str(error), file=sys.stderr, flush=True)
            (directory / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
        if len(results) == 5:
            summaries[name] = summary(results)
    (output / 'summary.json').write_text(json.dumps(summaries, indent=2) + '\n')
    if failures:
        raise RuntimeError(f'{len(failures)} failed trials; see {output / "failures.json"}')
    performance_report.finish(output, report, scenarios, sys.modules[__name__])
    print(json.dumps(summaries, indent=2))
    return 0


if __name__ == '__main__':
    sys.exit(main())
