#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Preserve Files compiler-context selection around the canonical bundle."""
import argparse
import json
import os
from pathlib import Path
import shlex
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tooling"))
from workflow import ROOT, refresh, owned_files, run
from compilation import atomic_json

def tidy(config, scope):
    database = os.environ.get('HOLONIGHT_TIDY_DATABASE')
    if database:
        entries = json.loads(Path(database).read_text())
    else:
        refresh(config)
        entries = json.loads((ROOT / 'compile_commands.json').read_text())
    if config.get('installed_consumers'):
        # These are the three fresh consumers exercised by CTest, not archived build trees.
        for consumer in sorted((ROOT / 'build/test').glob('file-browser-consumer-*/build/compile_commands.json')):
            entries.extend(json.loads(consumer.read_text()))
    clang_entries = []
    for entry in entries:
        args = entry.get('arguments') or shlex.split(entry['command'])
        args = [a for a in args if a not in {'-mno-direct-extern-access', '-fno-keep-inline-dllexport', '-Wno-template-id-cdtor'}]
        clang_entries.append(dict(directory=entry['directory'], file=entry['file'], arguments=args))
    target = ROOT / '.cache/tooling/clang'
    atomic_json(target / 'compile_commands.json', clang_entries)
    sources = set(owned_files({'.cpp', '.cc', '.cxx', '.c'}))
    if scope != 'all':
        sources = {path for path in sources if ('tests' in path.relative_to(ROOT).parts) == (scope == 'tests')}
    uncovered = sources - {Path(e['file']) for e in entries}
    if uncovered:
        raise RuntimeError('No compile command for: ' + ', '.join(str(p.relative_to(ROOT)) for p in sorted(uncovered)))
    # clang-tidy evaluates every compiler context for a file itself; invoke it once.
    for path in sorted(sources):
        is_test = 'tests' in path.relative_to(ROOT).parts
        tidy_config = ROOT / 'tests/.clang-tidy' if is_test and (ROOT / 'tests/.clang-tidy').exists() else ROOT / '.clang-tidy'
        run([os.environ.get('CLANG_TIDY', 'clang-tidy'), path, '-p', target,
             f'--config-file={tidy_config}',
             rf'--header-filter=^{ROOT}/(apps|libs|tests)/.*\.(h|hpp)$'])


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--scope", choices=("all", "src", "tests"), default="all")
    parser.add_argument("--installed-consumers", action="store_true")
    args = parser.parse_args()
    tidy({"installed_consumers": args.installed_consumers}, args.scope)
