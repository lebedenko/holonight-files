#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
stage=$(mktemp -d "$root/build/install-check.XXXXXX")
DESTDIR="$stage" cmake --install "$root/build/release"
for file in bin/holonight-files share/applications/org.holonight.Files.desktop \
  share/icons/hicolor/scalable/apps/org.holonight.Files.svg \
  share/licenses/holonight-files/LICENSE share/licenses/holonight-files/GPL-3.0-or-later.txt; do
  test -s "$stage/usr/$file"
done
desktop-file-validate "$stage/usr/share/applications/org.holonight.Files.desktop"
# Only installed provider imports are available; no source or build QML paths.
export QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH:-${HOLONIGHT_DEPENDENCY_PREFIX:-$root/build/deps/prefix}/lib/qt6/qml}"
export LD_LIBRARY_PATH="${HOLONIGHT_DEPENDENCY_PREFIX:-$root/build/deps/prefix}/lib"
unset QML2_IMPORT_PATH QT_QUICK_CONTROLS_STYLE QT_QUICK_CONTROLS_CONF QT_QUICK_CONTROLS_FALLBACK_STYLE
cd "$stage"
QT_QPA_PLATFORM=offscreen "$stage/usr/bin/holonight-files" --version
set +e
QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software timeout 3 "$stage/usr/bin/holonight-files" > runtime.log 2>&1
status=$?
set -e
cat runtime.log
test "$status" -eq 124
if rg -i 'failed|error|not installed|not found|unavailable' runtime.log; then exit 1; fi
printf 'Staged installation passed: %s\n' "$stage"
