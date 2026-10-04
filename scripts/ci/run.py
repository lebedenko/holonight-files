#!/usr/bin/env python3
"""Run push validation against isolated current working-tree inputs."""
import argparse
import datetime
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
        result = subprocess.run([runtime, 'build', '--platform', PLATFORM, '--iidfile', str(identity),
                                 '--build-arg', f'FILES_CI_IMAGE={image}', str(context)],
                                stdout=log, stderr=subprocess.STDOUT)
        if result.returncode == 0:
            result = subprocess.run([runtime, 'run', '--rm', '--platform', PLATFORM, '--network', 'none',
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
    command = [runtime, 'run', '--rm', '--platform', PLATFORM, '--network', 'none',
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
            for lane in args.lane or LANES:
                image = images['licensing' if lane == 'licensing' else 'build']
                artifacts = output / 'artifacts' / lane
                artifacts.mkdir(parents=True)
                command = [runtime, 'run', '--rm', '--platform', report['platform'], '--user', f'{os.getuid()}:{os.getgid()}',
                           '--mount', f'type=bind,src={source},dst=/input,readonly',
                           '--mount', f'type=bind,src={artifacts},dst=/output',
                           '--tmpfs', '/work:rw,exec,mode=1777', '--workdir', '/work',
                           '--entrypoint', '/bin/sh', image, '/input/scripts/ci/lane.sh', lane]
                if Path(runtime).name == 'podman' and os.getuid() != 0:
                    command.insert(2, '--userns=keep-id')
                if lane != 'licensing':
                    command[command.index('--user') + 1] = '0:0'
                    command[command.index('/input/scripts/ci/lane.sh')] = '/input/scripts/ci/bootstrap.sh'
                    command[command.index(image):command.index(image)] = [
                        '--env', f'CI_UID={os.getuid()}', '--env', f'CI_GID={os.getgid()}']
                image_index = command.index(image)
                command[image_index:image_index] = account_options
                print(f'Running {lane}: {image}', flush=True)
                with (output / f'{lane}.log').open('w') as log:
                    result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
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
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        report['error'] = str(error)
        print(f'CI failed: {error}', file=sys.stderr)
        failed = True
    finally:
        (output / 'results.json').write_text(json.dumps(report, indent=2) + '\n')
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
