#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com>
set -euo pipefail

if [[ ${QMLFORMAT+x} ]]; then
  formatter=$(command -v -- "$QMLFORMAT") || {
    echo "Invalid QMLFORMAT override: $QMLFORMAT" >&2
    exit 1
  }
  if [[ ! -f $formatter || ! -x $formatter ]]; then
    echo "Invalid QMLFORMAT override: $QMLFORMAT" >&2
    exit 1
  fi
else
  root=$(cd -- "${BASH_SOURCE[0]%/*}/.." && pwd)
  formatter=$(python3 "$root/tooling/workflow.py" qt-tool --tool qmlformat --preset "${EDITOR_PRESET:-test}") || {
    echo 'Cannot find qmlformat in configured Qt. Configure build/test or set QMLFORMAT.' >&2
    exit 1
  }
fi
exec "$formatter" "$@"
