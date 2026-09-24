#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com>
"""Versioned local performance reports. No timing gates or significance claims."""
import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import statistics

SCHEMA = 1


def read_json(path):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError(f'duplicate JSON key: {key}')
            result[key] = value
        return result
    return json.loads(path.read_text(), object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError(f'non-finite {value}')))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def normalize(value, roots):
    if isinstance(value, dict):
        return {key: normalize(item, roots) for key, item in value.items()}
    if isinstance(value, list):
        return [normalize(item, roots) for item in value]
    if isinstance(value, str):
        for path, label in sorted(roots.items(), key=lambda item: -len(item[0])):
            value = value.replace(path, label)
    return value


def settings(cache):
    values = {}
    for line in cache.splitlines():
        if '=' not in line or line.startswith(('#', '//')):
            continue
        name, value = line.split('=', 1)
        name = name.split(':')[0]
        # Effective compiler/linker options and project switches, not internal cache hashes.
        if (name.startswith(('CMAKE_CXX_FLAGS', 'CMAKE_EXE_LINKER_FLAGS', 'CMAKE_SHARED_LINKER_FLAGS',
                             'CMAKE_STATIC_LINKER_FLAGS', 'BUILD_', 'HOLONIGHT_')) or name in
                ('CMAKE_BUILD_TYPE', 'CMAKE_CXX_COMPILER', 'CMAKE_CXX_COMPILER_LAUNCHER',
                 'CMAKE_INTERPROCEDURAL_OPTIMIZATION', 'CMAKE_POSITION_INDEPENDENT_CODE',
                 'CMAKE_GENERATOR', 'CMAKE_MAKE_PROGRAM', 'CMAKE_PREFIX_PATH', 'QML_IMPORT_PATH')):
            values[name] = value
    return values


def metadata(root, binary, prefix, original, scenarios, required, fixture_identity):
    roots = {str(root): '<checkout>', str(binary.parent.parent): '<build>', str(prefix): '<prefix>',
             str(root.parent): '<workspace>'}
    cache = (binary.parent.parent / 'CMakeCache.txt').read_text()
    providers = {}
    for name, info in original['providers'].items():
        provider_cache = root / 'build/deps' / name / 'CMakeCache.txt'
        providers[name] = {'revision': info['revision'], 'settings': settings(provider_cache.read_text())}
    provenance = {
        'production_sources': original['production_sha256'],
        'compiler': original['compiler'], 'qt': original['qt'],
        'build_settings': settings(cache), 'providers': providers,
        'provider_artifacts': original['installed_library_sha256'],
        'system': original['system'], 'cpu': '\n'.join(line for line in original['cpu'].splitlines()
                                                     if 'MHz' not in line),
    }
    return {'schema_version': SCHEMA, 'trial_count': 5,
            'workloads': {name: {'test': test, 'definition_version': 1,
                                'metrics': sorted(required[test] | {'peak_rss_kib'})}
                          for name, test in scenarios.items()},
            'metric_definition_version': 1,
            'metric_definitions': 'Suffix ns/ms is elapsed time; kib is KiB; bytes is bytes; '
                                  'remaining fields are event/pixel/item counts. Definitions are '
                                  'the hashed instrumented scenario source; peak RSS is wait4 ru_maxrss.',
            'fixtures': fixture_identity,
            'instrumentation': original['instrumentation_sha256'],
            'rendering': {'QT_QPA_PLATFORM': 'offscreen', 'QT_QUICK_BACKEND': 'software',
                          'QSG_RHI_BACKEND': 'software', 'QT_SCALE_FACTOR': '1',
                          'bus': 'private', 'user_directories': 'per-trial'},
            'provenance': normalize(provenance, roots), 'original_provenance': original,
            'path_roots': roots, 'complete': False}


def finish(output, report, scenarios, runner):
    raw = {}
    for name in scenarios:
        directory = output / name if (output / name / 'results.json').exists() else output
        raw[name] = {'directory': str(directory.relative_to(output)),
                     'hashes': {p.name: sha(p) for p in sorted(directory.glob('run-*')) if p.is_file()},
                     'results_sha256': sha(directory / 'results.json')}
    report['raw'] = raw
    report['complete'] = True
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    load(output, runner)  # Refuse to certify a malformed fresh dataset.


def load(directory, runner):
    report = read_json(directory / 'report.json')
    if report.get('schema_version') != SCHEMA or report.get('trial_count') != 5 or report.get('complete') is not True:
        raise ValueError('incompatible schema or incomplete report')
    for key in ('workloads', 'fixtures', 'instrumentation', 'rendering', 'provenance', 'original_provenance',
                'path_roots', 'raw', 'metric_definitions', 'metric_definition_version'):
        if not report.get(key):
            raise ValueError(f'missing report field: {key}')
    if (directory / 'failures.json').exists():
        raise ValueError('dataset records failed trials')
    if set(report['raw']) != set(report['workloads']):
        raise ValueError('incomplete scenario set')
    summaries = {}
    for name, workload in report['workloads'].items():
        if name not in runner.SCENARIOS or workload['test'] != runner.SCENARIOS[name]:
            raise ValueError('unknown scenario definition')
        expected = runner.REQUIRED[workload['test']] | {'peak_rss_kib'}
        if set(workload['metrics']) != expected or workload['definition_version'] != 1:
            raise ValueError('incompatible metric schema')
        raw = report['raw'][name]
        path = (directory / raw['directory']).resolve()
        if not path.is_relative_to(directory.resolve()):
            raise ValueError('raw path escapes dataset')
        results = read_json(path / 'results.json')
        if not isinstance(results, list) or len(results) != 5 or sha(path / 'results.json') != raw['results_sha256']:
            raise ValueError('missing or corrupt trials')
        for trial, result in enumerate(results):
            if set(result) != expected or any(type(v) not in (int, float) or not math.isfinite(v) or v < 0
                                              for v in result.values()):
                raise ValueError('invalid raw metrics')
            if result['peak_rss_kib'] <= 0:
                raise ValueError('missing peak RSS')
            for suffix in ('.xml', '.log', '-rss.json', '-process.json'):
                filename = f'run-{trial}{suffix}'
                if sha(path / filename) != raw['hashes'].get(filename):
                    raise ValueError(f'corrupt raw evidence: {filename}')
            process = read_json(path / f'run-{trial}-process.json')
            if (type(process.get('returncode')) is not int or process['returncode'] != 0
                    or type(process.get('peak_rss_kib')) is not int
                    or process['peak_rss_kib'] != result['peak_rss_kib']):
                raise ValueError('failed process or inconsistent peak RSS')
            xml_values = runner.measurements(path / f'run-{trial}.xml', workload['test'])
            if xml_values != {k: v for k, v in result.items() if k != 'peak_rss_kib'}:
                raise ValueError('raw XML and results disagree')
            samples = read_json(path / f'run-{trial}-rss.json')
            if not samples or any(type(s.get('rss_kib')) is not int or s['rss_kib'] <= 0 or
                                  type(s.get('elapsed_ms')) is not int or s['elapsed_ms'] < 0 for s in samples):
                raise ValueError('missing or invalid RSS samples')
        summaries[name] = {key: {'median': statistics.median(row[key] for row in results),
                                 'min': min(row[key] for row in results),
                                 'max': max(row[key] for row in results)} for key in sorted(expected)}
    return report, summaries


def differences(left, right, prefix=''):
    if isinstance(left, dict) and isinstance(right, dict):
        result = {}
        for key in sorted(left.keys() | right.keys()):
            field = f'{prefix}.{key}' if prefix else key
            if key not in left or key not in right:
                result[field] = {'baseline': left.get(key), 'candidate': right.get(key)}
            else:
                result.update(differences(left[key], right[key], field))
        return result
    return {} if left == right else {prefix: {'baseline': left, 'candidate': right}}


def compare(baseline, candidate, runner, allowed):
    before, old = load(baseline, runner)
    after, new = load(candidate, runner)
    for field in ('workloads', 'fixtures', 'instrumentation', 'rendering', 'metric_definitions',
                  'metric_definition_version'):
        if before[field] != after[field]:
            raise ValueError(f'incompatible {field}; workload/instrumentation overrides are forbidden')
    changes = differences(before['provenance'], after['provenance'])
    if set(allowed) != set(changes) or any(not reason.strip() for reason in allowed.values()):
        raise ValueError(f'provenance changes require exact --allow FIELD=REASON entries: {sorted(changes)}')
    metrics = {}
    for scenario in old:
        metrics[scenario] = {}
        for key, previous in old[scenario].items():
            current = new[scenario][key]
            delta = current['median'] - previous['median']
            metrics[scenario][key] = {'baseline': previous, 'candidate': current, 'absolute_change': delta,
                                      'percentage_change': delta / previous['median'] * 100
                                      if previous['median'] else 'not applicable'}
    return {'schema_version': SCHEMA, 'baseline': str(baseline.resolve()), 'candidate': str(candidate.resolve()),
            'provenance_changes': {key: dict(value, reason=allowed[key]) for key, value in changes.items()},
            'original_provenance_changes': differences(before['original_provenance'], after['original_provenance']),
            'metrics': metrics, 'note': 'Descriptive five-trial comparison; no timing gate or significance claim.'}


def main(runner_name):
    spec = importlib.util.spec_from_file_location('measure', Path(__file__).with_name(runner_name))
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('baseline', type=Path)
    parser.add_argument('candidate', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--allow', action='append', default=[], metavar='FIELD=REASON')
    args = parser.parse_args()
    allowed = {}
    for item in args.allow:
        key, sep, reason = item.partition('=')
        if not sep or key in allowed or not reason.strip():
            parser.error('each --allow must name one distinct field and a nonempty reason')
        allowed[key] = reason
    try:
        result = compare(args.baseline, args.candidate, runner, allowed)
        args.output.mkdir(parents=True, exist_ok=False)
        (args.output / 'comparison.json').write_text(json.dumps(result, indent=2, allow_nan=False) + '\n')
        lines = ['# Performance comparison', '', result['note'], '',
                 '| Scenario / metric | Baseline median [min, max] | Candidate median [min, max] | Absolute | Percent |',
                 '|---|---|---|---|---|']
        for scenario, metrics in result['metrics'].items():
            for key, row in metrics.items():
                old, new = row['baseline'], row['candidate']
                lines.append(f"| {scenario} / {key} | {old['median']} [{old['min']}, {old['max']}] | "
                             f"{new['median']} [{new['min']}, {new['max']}] | {row['absolute_change']} | "
                             f"{row['percentage_change']} |")
        lines += ['', '## Provenance differences', '', '```json',
                  json.dumps({key: result[key] for key in ('provenance_changes', 'original_provenance_changes')},
                             indent=2), '```', '']
        (args.output / 'comparison.md').write_text('\n'.join(lines))
    except (ValueError, KeyError, TypeError, OSError, RuntimeError) as error:
        parser.exit(1, f'Comparison rejected: {error}\n')
    return 0
