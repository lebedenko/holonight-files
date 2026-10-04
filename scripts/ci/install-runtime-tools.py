#!/usr/bin/env python3
"""Reinstall the build lane's verified supplements without network access."""
import hashlib
import json
from pathlib import Path
import subprocess

for package in json.loads(Path('/ci/packages.json').read_text()):
    archive = Path('/ci/packages') / (package['name'] + '.pkg.tar.zst')
    if hashlib.sha256(archive.read_bytes()).hexdigest() != package['sha256']:
        raise RuntimeError(f"Archive checksum mismatch: {package['name']}")
    subprocess.run(['bsdtar', '-xf', str(archive), '-C', '/'], check=True)
subprocess.run(['fc-cache', '-f'], check=True)

subprocess.run(['update-mime-database', '/usr/share/mime'], check=True)
