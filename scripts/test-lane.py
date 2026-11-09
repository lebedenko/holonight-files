#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run an exhaustive, disjoint test selection; reject stale entries and host skips."""
import argparse
import fnmatch
import json
import os
from pathlib import Path
import subprocess
import tempfile
import xml.etree.ElementTree as ET


def select(inventory, names, lane):
    owners = {name: [] for name in names}
    for group, patterns in inventory.items():
        for pattern in patterns:
            matches = [name for name in names if fnmatch.fnmatchcase(name, pattern)]
            if not matches:
                raise ValueError(f'Stale {group} selection: {pattern}')
            for name in matches:
                owners[name].append(group)
    invalid = {name: groups for name, groups in owners.items() if len(groups) != 1}
    if invalid:
        raise ValueError(f'Unassigned or overlapping tests: {invalid}')
    selected = [name for name, groups in owners.items() if groups == [lane]]
    if not selected:
        raise ValueError(f'Empty {lane} selection')
    return selected


def verify_report(report, selected, lane, framework):
    tree = ET.parse(report)
    cases = tree.findall('.//testcase')
    if framework == 'gtest':
        executed = {case.get('classname', '') + '.' + case.get('name', '') for case in cases}
    else:
        executed = {case.get('name', '').split('(')[0] for case in cases}
    if not set(selected) <= executed:
        raise ValueError(f'Incomplete execution: {set(selected) - executed}')
    if lane == 'host' and any(case.find('skipped') is not None or case.get('status') == 'notrun' for case in cases):
        raise ValueError('Required host test skipped')
    if tree.findall('.//failure') or tree.findall('.//error'):
        raise ValueError('Failed test report')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--lane', choices=['portable', 'host'], required=True)
    parser.add_argument('--inventory', type=Path, required=True)
    parser.add_argument('--framework', choices=['gtest', 'qt'], required=True)
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ['--'] else args.command
    if not command:
        parser.error('Executable required')
    if args.lane == 'host' and (args.runtime is None or not (args.runtime / 'manifest.json').is_file()):
        parser.error('Host selection requires --runtime with manifest.json')
    os.environ.update(QT_QPA_PLATFORM='offscreen', QT_QUICK_BACKEND='software', QT_QPA_PLATFORMTHEME='')
    isolated = tempfile.TemporaryDirectory(prefix='consumer-environment-')
    # Keep this alive through enumeration and execution.
    for key in ('CONFIG', 'CACHE', 'DATA', 'STATE'):
        os.environ['XDG_' + key + '_HOME'] = str(Path(isolated.name) / key.lower())
    listing = subprocess.run(command + (['--gtest_list_tests'] if args.framework == 'gtest' else ['-functions']),
                             check=True, stdout=subprocess.PIPE, text=True, timeout=30).stdout
    names = []
    suite = ''
    for line in listing.splitlines():
        line = line.split('#', 1)[0].rstrip()
        if args.framework == 'qt':
            if line.endswith('()'):
                names.append(line[:-2])
        elif line and not line.startswith(' '):
            suite = line
        elif line.strip() and suite.endswith('.'):
            names.append(suite + line.strip())
    inventory = json.loads(args.inventory.read_text())
    selected = select(inventory, names, args.lane)
    print(f'{args.lane}: {len(selected)} tests; '
          f'{len(names) - len(selected)} tests excluded (other lane)', flush=True)
    if args.lane == 'portable':
        print('Container/portable success is not isolated runtime acceptance; run task test:isolated.', flush=True)
    with tempfile.TemporaryDirectory(prefix='consumer-tests-') as temporary:
        report = Path(temporary) / 'results.xml'
        if args.framework == 'gtest':
            options = ['--gtest_filter=' + ':'.join(selected), '--gtest_output=xml:' + str(report)]
        else:
            options = selected + ['-o', str(report) + ',xunitxml', '-o', '-,txt']
        if args.lane == 'host':
            options += ['--image-runtime', str(args.runtime.resolve())]
        result = subprocess.run(command + options, timeout=600)
        if result.returncode:
            return result.returncode
        verify_report(report, selected, args.lane, args.framework)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
