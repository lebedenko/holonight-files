#!/usr/bin/env python3
"""Install checksum-pinned supplements only inside the disposable container."""
import hashlib
import json
from pathlib import Path
import subprocess

prefix = Path('/work/packages')
prefix.mkdir()
for package in json.loads(Path('/input/scripts/ci/packages.json').read_text()):
    archive = prefix / (package['name'] + '.pkg.tar.zst')
    print(f"Supplement: {package['name']} {package['version']} sha256:{package['sha256']}", flush=True)
    subprocess.run(['curl', '--fail', '--location', '--silent', '--show-error', '--max-time', '60',
                    '--retry', '2', package['url'], '--output', str(archive)], check=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != package['sha256']:
        raise RuntimeError(f"Archive checksum mismatch: {package['name']}")
    subprocess.run(['bsdtar', '-xf', str(archive), '-C', '/'], check=True)

Path('/usr/local/bin/task').symlink_to('/usr/bin/go-task')
subprocess.run(['fc-cache', '-f'], check=True)
subprocess.run(['ldconfig'], check=True)
subprocess.run(['reuse', '--version'], check=True)

subprocess.run(['localedef', '-i', 'en_US', '-f', 'UTF-8', 'en_US.UTF-8'], check=True)

subprocess.run(['update-mime-database', '/usr/share/mime'], check=True)
