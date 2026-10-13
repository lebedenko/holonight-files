#!/usr/bin/env python3
"""Run push validation against isolated current working-tree inputs."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
LANES = ('build', 'isolation', 'licensing')
PLATFORM = 'linux/amd64'

PROVIDERS = {
    'holonight-config': '0ccd25f323662f50a47bb048be636160f2b7c135',
    'holonight-system-services': '70614eb70329af7014853c2973e5624acf0b251e',
    'holonight-qt': '7c3ce074a8c2e02e4ee8cad8cd19eb8627aa4f68',
    'holonight-images': 'ae827d78db9cc21f9537c27f50f49baf879324da',
    'holonight-thumbnails': 'c9ae0ad9a64e8ce28ee669b5463ef1bec74bc16d',
    'holonight-search': '5e71fd7c6494084b4fcc1b5b7a8df9bb2e690bdc',
}
PREPARATION_INPUTS = ('tooling/VERSION', 'tooling/module.json', 'tooling/dependencies.cmake',
                      'tooling/providers.cmake', 'tooling/external.py', 'tooling/workflow.py',
                      'tooling/external-packages.json', 'CMakeLists.txt', 'CMakePresets.json', 'Taskfile.yml',
                      'scripts/ci/lane.sh', 'scripts/ci/run.py', 'scripts/workflow.py')


def preparation_identity(images):
    return {'images': images, 'platform': PLATFORM, 'providers': PROVIDERS,
            'inputs': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                       for name in PREPARATION_INPUTS}}


def prepared_hashes(prepared):
    # Include provider sources, installed payloads, build state and environment evidence.
    # Git objects are transport only; source contents and exact revisions are recorded.
    hashes = {}
    for path in sorted(prepared.rglob('*')):
        relative = path.relative_to(prepared)
        if '.git' in relative.parts or relative.as_posix() == 'manifest.json':
            continue
        if path.is_symlink():
            hashes[relative.as_posix()] = {'sha256': hashlib.sha256(os.fsencode(os.readlink(path))).hexdigest(),
                                           'mode': path.lstat().st_mode}
        elif path.is_file():
            digest = hashlib.sha256()
            with path.open('rb') as source:
                for block in iter(lambda: source.read(1024 * 1024), b''):
                    digest.update(block)
            hashes[relative.as_posix()] = {'sha256': digest.hexdigest(), 'mode': path.stat().st_mode}
    return hashes


def validate_prepared(prepared, identity):
    if not (prepared / 'manifest.json').is_file():
        raise RuntimeError('Missing prepared CI environment; run task ci:prepare explicitly.')
    manifest = json.loads((prepared / 'manifest.json').read_text())
    if manifest['identity'] != identity:
        raise RuntimeError('Stale prepared CI identity; run task ci:prepare explicitly.')
    if manifest['hashes'] != prepared_hashes(prepared):
        raise RuntimeError('Tampered prepared CI contents; run task ci:prepare explicitly.')


def prepare_sources(prepared, source_root=None):
    for name, revision in PROVIDERS.items():
        source = str(source_root / name) if source_root else f'https://github.com/lebedenko/{name}.git'
        destination = prepared / 'sources' / name
        subprocess.run(['git', 'clone', '--quiet', '--no-checkout', '--no-hardlinks', source, str(destination)], check=True)
        subprocess.run(['git', '-C', str(destination), 'checkout', '--quiet', '--detach', revision], check=True)
        actual = subprocess.check_output(['git', '-C', str(destination), 'rev-parse', 'HEAD'], text=True).strip()
        if actual != revision:
            raise RuntimeError(f'{name}: expected {revision}, got {actual}')
    (prepared / 'provider-revisions.json').write_text(json.dumps(PROVIDERS, indent=2) + '\n')


def archive_preparation(prepared):
    number = 1
    while prepared.with_name(f'ci-prepared.previous-{number}').exists():
        number += 1
    archived = prepared.with_name(f'ci-prepared.previous-{number}')
    prepared.rename(archived)
    return archived


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args])


def snapshot(destination):
    # Independent objects/config/index; retain the real HEAD for versioning.
    subprocess.run(['git', 'clone', '--quiet', '--no-checkout', '--no-hardlinks',
                    str(ROOT), str(destination)], check=True)
    tracked = git('ls-files', '-z').split(b'\0')
    new = git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0')
    for raw in dict.fromkeys(tracked + new):
        if not raw:
            continue
        name = os.fsdecode(raw)
        source = ROOT / name
        if source.is_symlink() or source.is_file():
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target, follow_symlinks=False)
        elif source.exists():
            raise RuntimeError(f'Unsupported snapshot input: {name}')
    subprocess.run(['git', '-C', str(destination), 'add', '-A'], check=True)
    return [os.fsdecode(raw) for raw in new if raw]


def installed_runtime(runtime, image, artifacts, output):
    context = artifacts / 'runtime-context'
    identity = output / 'runtime-image-id'
    with (output / 'installed-runtime.log').open('w') as log:
        result = subprocess.run([runtime, 'build', '--pull=false', '--network=none', '--platform', PLATFORM, '--iidfile', str(identity),
                                 '--build-arg', f'FILES_CI_IMAGE={image}', str(context)],
                                stdout=log, stderr=subprocess.STDOUT)
        if result.returncode == 0:
            result = subprocess.run([runtime, 'run', '--rm', '--pull=never', '--network=none', '--platform', PLATFORM,
                                     identity.read_text().strip()], stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print((output / 'installed-runtime.log').read_text(errors='replace'), flush=True)
    if result.returncode == 0:
        runner = output / 'runtime-fixture-runner'
        (runner / 'scripts').mkdir(parents=True)
        (runner / 'build').mkdir()
        shutil.copy2(artifacts / 'check-runtime-fixtures.py', runner / 'scripts/check-runtime-fixtures.py')
        with (output / 'runtime-fixtures.log').open('w') as log:
            result = subprocess.run([sys.executable, str(runner / 'scripts/check-runtime-fixtures.py'),
                                     identity.read_text().strip()],
                                    env=dict(os.environ, FILES_CONTAINER_RUNTIME=runtime),
                                    stdout=log, stderr=subprocess.STDOUT)
        if result.returncode:
            print((output / 'runtime-fixtures.log').read_text(errors='replace'), flush=True)
    return result


def isolation_tests(runtime, image, artifacts, output, account_options):
    command = [runtime, 'run', '--rm', '--pull=never', '--network=none', '--platform', PLATFORM,
               '--security-opt', 'seccomp=unconfined', '--user', '0:0',
               '--env', f'CI_UID={os.getuid()}', '--env', f'CI_GID={os.getgid()}',
               '--mount', f'type=bind,src={artifacts},dst=/evidence,readonly',
               '--mount', f'type=bind,src={output},dst=/output',
               '--tmpfs', '/work:rw,exec,mode=1777', '--workdir', '/work',
               *account_options]
    apparmor = Path('/sys/module/apparmor/parameters/enabled')
    if apparmor.exists() and apparmor.read_text().strip() == 'Y':
        command += ['--security-opt', 'apparmor=holonight-files-isolation']
    if Path(runtime).name == 'podman' and os.getuid() != 0:
        command.insert(2, '--userns=keep-id')
    command += ['--entrypoint', '/bin/sh', image, '/evidence/isolation-test.sh']
    with (output / 'isolation-test.log').open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print((output / 'isolation-test.log').read_text(errors='replace'), flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lane', choices=LANES, action='append')
    parser.add_argument('--prepare', action='store_true', help='Explicitly acquire inputs and prepare providers')
    parser.add_argument('--provider-source-root', type=Path, help='Clone exact commits from local provider repositories')
    args = parser.parse_args()
    output = ROOT / 'build' / 'ci'
    output.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix=datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%SZ-'), dir=output))
    print(f'CI logs: {output}', flush=True)
    report = {'revision': git('rev-parse', 'HEAD').decode().strip(),
              'status': git('status', '--porcelain=v1', '-z').decode(errors='surrogateescape'),
              'platform': PLATFORM, 'lanes': {}}
    report['dirty'] = bool(report['status'])
    failed = False
    try:
        if os.getuid() == 0:
            raise RuntimeError('Run Files CI as a non-root host user; permission checks require an unprivileged identity.')
        runtime = shutil.which('docker') or shutil.which('podman')
        if not runtime:
            raise RuntimeError('Neither Docker nor Podman is available; install a container runtime.')
        report['runtime'] = subprocess.check_output([runtime, '--version'], text=True).strip()
        images = json.loads((ROOT / 'scripts/ci/images.json').read_text())
        prepared = ROOT / 'build/ci-prepared'
        identity = preparation_identity(images)
        report['preparation_identity'] = identity
        if args.prepare:
            reusable = None
            for image in dict.fromkeys(images.values()):
                subprocess.run([runtime, 'pull', '--platform', PLATFORM, image], check=True)
            if prepared.exists():
                try:
                    validate_prepared(prepared, identity)
                    print('Compatible hash-validated preparation reused.', flush=True)
                    report['reused'] = True
                    return 0
                except (OSError, ValueError, KeyError, RuntimeError):
                    # A changed launcher/application contract need not discard compatible provider builds.
                    # The canonical workflow rechecks source/toolchain/options before accepting any seed.
                    try:
                        manifest = json.loads((prepared / 'manifest.json').read_text())
                        same_providers = all(manifest['identity'][key] == identity[key]
                                             for key in ('images', 'providers', 'platform'))
                        intact = manifest['hashes'] == prepared_hashes(prepared)
                    except (OSError, ValueError, KeyError):
                        same_providers = intact = False
                    archived = archive_preparation(prepared)
                    if same_providers and intact:
                        reusable = archived / 'deps'
            prepared.mkdir(parents=True)
            prepare_sources(prepared, args.provider_source_root)
            if reusable and reusable.is_dir():
                shutil.copytree(reusable, prepared / 'deps', symlinks=True)
            image_ids = {name: subprocess.check_output([runtime, 'image', 'inspect', '--format', '{{.Id}}', image], text=True).strip()
                         for name, image in images.items()}
            (prepared / 'image-ids.json').write_text(json.dumps(image_ids, indent=2) + '\n')
            reuse_command = [runtime, 'run', '--rm', '--pull=never', '--network=none',
                             '--platform', PLATFORM, '--user', f'{os.getuid()}:{os.getgid()}',
                             '--entrypoint', 'reuse', images['licensing'], '--version']
            if Path(runtime).name == 'podman':
                reuse_command.insert(2, '--userns=keep-id')
            (prepared / 'reuse.txt').write_text(subprocess.check_output(reuse_command, text=True))
        else:
            validate_prepared(prepared, identity)
        with tempfile.TemporaryDirectory(prefix='holonight-files-ci-') as temporary:
            source = Path(temporary) / 'source'
            source.mkdir()
            account_options = []
            if os.getuid() != 0:
                passwd = Path(temporary) / 'passwd'
                group = Path(temporary) / 'group'
                passwd.write_text('root:x:0:0:root:/root:/bin/sh\n'
                                  'alpm:x:979:979:Arch Linux Package Management:/:/usr/bin/nologin\n'
                                  f'files-test:x:{os.getuid()}:{os.getgid()}:CI:/work/build/home:/bin/sh\n'
                                  'nobody:x:65534:65534:nobody:/nonexistent:/bin/sh\n')
                group.write_text('root:x:0:\nalpm:x:979:\n' + f'files-test:x:{os.getgid()}:\n' + 'nobody:x:65534:\n')
                account_options = ['--mount', f'type=bind,src={passwd},dst=/etc/passwd,readonly',
                                   '--mount', f'type=bind,src={group},dst=/etc/group,readonly']
            report['untracked_inputs'] = snapshot(source)
            if report['untracked_inputs']:
                print('Untracked inputs included; add before pushing:')
                for name in report['untracked_inputs']:
                    print(f'  {name!r}')
            for lane in ('prepare',) if args.prepare else args.lane or LANES:
                image = images['licensing' if lane == 'licensing' else 'build']
                artifacts = output / 'artifacts' / lane
                artifacts.mkdir(parents=True)
                command = [runtime, 'run', '--rm', '--pull=never', '--network=none', '--platform', report['platform'], '--user', f'{os.getuid()}:{os.getgid()}',
                           '--mount', f'type=bind,src={source},dst=/input,readonly',
                           '--mount', f'type=bind,src={artifacts},dst=/output',
                           '--tmpfs', '/work:rw,exec,mode=1777', '--workdir', '/work',
                           '--env', 'JOBS=' + os.environ.get('JOBS', '2'),
                           '--entrypoint', '/bin/sh', image, '/input/scripts/ci/lane.sh', lane]
                if Path(runtime).name == 'podman' and os.getuid() != 0:
                    command.insert(2, '--userns=keep-id')
                if lane != 'licensing':
                    index = command.index(image)
                    command[index:index] = ['--mount', f'type=bind,src={prepared},dst=/prepared' + ('' if lane == 'prepare' else ',readonly')]
                image_index = command.index(image)
                command[image_index:image_index] = account_options
                print(f'Running {lane}: {image}', flush=True)
                with (output / f'{lane}.log').open('w') as log:
                    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
                if result.returncode == 0 and lane == 'prepare':
                    (prepared / 'manifest.json').write_text(json.dumps(
                        {'identity': identity, 'hashes': prepared_hashes(prepared)}, indent=2) + '\n')
                if result.returncode == 0 and lane == 'isolation':
                    result = isolation_tests(runtime, image, artifacts, output, account_options)
                if result.returncode == 0 and lane == 'build':
                    result = installed_runtime(runtime, image, artifacts, output)
                report['lanes'][lane] = {'image': image, 'exit_code': result.returncode}
                if lane == 'build' and (output / 'runtime-image-id').exists():
                    report['lanes'][lane]['installed_runtime_image'] = (output / 'runtime-image-id').read_text().strip()
                print(f'{lane}: {"PASS" if result.returncode == 0 else "FAIL"} (see {output / (lane + ".log")})', flush=True)
                if result.returncode != 0:
                    print((output / f'{lane}.log').read_text(errors='replace'), flush=True)
                failed |= result.returncode != 0
    except (OSError, RuntimeError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        report['error'] = str(error)
        print(f'CI failed: {error}', file=sys.stderr)
        failed = True
    finally:
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
