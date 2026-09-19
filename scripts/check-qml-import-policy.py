# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com>
"""Enforce the UQC provider's application import boundary in Files QML."""
import re
import sys
from pathlib import Path

TYPES = ('AbstractButton ApplicationWindow Label ToolButton ToolBar ToolSeparator MenuSeparator Popup MenuBar '
         'MenuBarItem Button CheckBox ComboBox ItemDelegate Menu MenuItem ProgressBar RadioButton '
         'ScrollBar ScrollView Slider SpinBox Switch TabBar TabButton TextArea TextField ToolTip '
         'Control ButtonGroup Overlay RangeSlider Frame Pane Page Dialog DialogButtonBox BusyIndicator '
         'SwipeView StackView Action ActionGroup RoundButton DelayButton Tumbler SplitView '
         'HorizontalHeaderView VerticalHeaderView').split()


def violations(source):
    errors = []
    if re.search(r'^\s*import (?:Holonight(?:\s|$)|QtQuick\.Controls\.)', source, re.M):
        errors.append('direct style import')
    imports = re.findall(r'^\s*import QtQuick\.Controls([^\n]*)', source, re.M)
    if any(suffix.strip() != 'as Controls' for suffix in imports):
        errors.append('runtime import must use Controls namespace')
    if re.search(r'\bControls\.', source) and 'as Controls' not in [s.strip() for s in imports]:
        errors.append('Controls use requires file-local runtime import')
    if re.search(r'(?<![.\w])(?:' + '|'.join(TYPES) + r')(?:\s*\{|\.)', source):
        errors.append('qualify control instances, enums and attached properties')
    core_types = ('HoloniightPalette HolonightTheme HnAppearance HnShapeProfile HnSurfaceRole '
                  'HnCornerStyle HnShapeKind HnCornerMask HnIconProvider HnIcon HnControlSize '
                  'HnMetrics HnTypographyRole HnLabel').split()
    if re.search(r'\b(?:' + '|'.join(core_types) + r')\b', source) and not re.search(
            r'^\s*import Holonight\.Core(?:\s|$)', source, re.M):
        errors.append('Core use requires file-local Holonight.Core import')
    return errors


def main():
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1] / 'apps/files/qml'
    files = sorted(root.rglob('*.qml'))
    if not files:
        raise SystemExit('No application QML found')
    failed = False
    for path in files:
        for error in violations(path.read_text()):
            print(f'{path}: {error}', file=sys.stderr)
            failed = True
    return int(failed)


if __name__ == '__main__':
    sys.exit(main())
