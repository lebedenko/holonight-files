#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Select Files component providers before entering the canonical workflow."""
import argparse
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tooling'))
import workflow

settings = workflow.settings


def selected_command():
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('command', nargs='?')
    parser.add_argument('--preset', default='debug')
    return parser.parse_known_args()[0]


def component_settings():
    config = settings()
    args = selected_command()
    preset = 'test' if args.command == 'test' else args.preset
    features = workflow.selected_features(config, preset)
    if not features['BUILD_FILES_APP']:
        modules = set()
        if features['BUILD_FILE_BROWSER_QUICK']:
            modules.add('holonight-qt')
            # Config is exported by Qt as a required transitive dependency.
            modules.add('holonight-config')
        config['dependencies'] = [dep for dep in config['dependencies'] if dep['module'] in modules]
    return config


if __name__ == '__main__':
    workflow.settings = component_settings
    try:
        args = selected_command()
        if args.command == 'deps' and args.preset == 'test':
            config = component_settings()
            workflow.check_external(config, scopes=('test',), features=workflow.selected_features(config, 'test'),
                                    cmake_options=workflow.build_context('test'))
        workflow.main()
    except (RuntimeError, subprocess.CalledProcessError, OSError) as error:
        sys.exit(str(error))
