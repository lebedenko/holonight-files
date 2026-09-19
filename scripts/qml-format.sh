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
  formatter=''
  if [[ -z $formatter ]] && command -v qtpaths6 >/dev/null 2>&1; then
    for property in QT_INSTALL_BINS QT_HOST_BINS QT_INSTALL_LIBEXECS QT_HOST_LIBEXECS; do
      directory=$(qtpaths6 --query "$property" 2>/dev/null) || continue
      if [[ -n $directory && -f $directory/qmlformat && -x $directory/qmlformat ]]; then
        formatter=$directory/qmlformat
        break
      fi
    done
  fi
  if [[ -z $formatter && -x /usr/lib/qt6/bin/qmlformat ]]; then
    formatter=/usr/lib/qt6/bin/qmlformat
  fi
  if [[ -z $formatter ]]; then
    for name in qmlformat-qt6 qmlformat; do
      if formatter=$(command -v -- "$name"); then break; fi
    done
  fi
  if [[ -z $formatter ]]; then
    echo 'Cannot find qmlformat. Set QMLFORMAT to an executable or install Qt 6 formatting tools.' >&2
    exit 1
  fi
fi
exec "$formatter" "$@"
