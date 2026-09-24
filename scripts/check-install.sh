#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
stage=$(mktemp -d "$root/build/install-check.XXXXXX")
DESTDIR="$stage" cmake --install "$root/build/release"
for file in bin/hn-files share/applications/org.holonight.Files.desktop \
  share/icons/hicolor/scalable/apps/org.holonight.Files.svg \
  share/licenses/holonight-files/LICENSE share/licenses/holonight-files/GPL-3.0-or-later.txt; do
  test -s "$stage/usr/$file"
done
test ! -e "$stage/usr/bin/holonight-files"
test ! -L "$stage/usr/bin/holonight-files"
# config.toml/state.toml parsing links the system tomlplusplus shared library.
readelf -d "$stage/usr/bin/hn-files" | rg 'libtomlplusplus\.so\.3'
desktop="$stage/usr/share/applications/org.holonight.Files.desktop"
desktop-file-validate "$desktop"
rg -Fx 'Exec=hn-files -- %f' "$desktop"
rg -Fx 'MimeType=inode/directory;' "$desktop"
update-desktop-database "$stage/usr/share/applications"
mkdir -p "$stage/xdg/config" "$stage/xdg/data"
# GIO requires the desktop executable to be discoverable in PATH.
PATH="$stage/usr/bin:$PATH" XDG_DATA_HOME="$stage/xdg/data" XDG_DATA_DIRS="$stage/usr/share" \
  XDG_CONFIG_HOME="$stage/xdg/config" XDG_CONFIG_DIRS="$stage/xdg/config" \
  gio mime inode/directory > "$stage/mime.log"
cat "$stage/mime.log"
rg -F 'org.holonight.Files.desktop' "$stage/mime.log"
test ! -e "$stage/xdg/config/mimeapps.list"
# Only installed provider imports are available; no source or build QML paths.
export QML_IMPORT_PATH="${HOLONIGHT_QML_IMPORT_PATH:-${HOLONIGHT_DEPENDENCY_PREFIX:-$root/build/deps/prefix}/lib/qt6/qml}"
export LD_LIBRARY_PATH="${HOLONIGHT_DEPENDENCY_PREFIX:-$root/build/deps/prefix}/lib"
unset QML2_IMPORT_PATH QT_QUICK_CONTROLS_STYLE QT_QUICK_CONTROLS_CONF QT_QUICK_CONTROLS_FALLBACK_STYLE
cd "$stage"
QT_QPA_PLATFORM=offscreen "$stage/usr/bin/hn-files" --version
set +e
# Isolated config/data/state, so user files can't add warnings to the log.
mkdir -p "$stage/xdg/state"
XDG_CONFIG_HOME="$stage/xdg/config" XDG_DATA_HOME="$stage/xdg/data" XDG_STATE_HOME="$stage/xdg/state" \
  QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software timeout 3 "$stage/usr/bin/hn-files" > runtime.log 2>&1
status=$?
set -e
cat runtime.log
test "$status" -eq 124
if rg -i 'failed|error|not installed|not found|unavailable' runtime.log; then exit 1; fi
printf 'Staged installation passed: %s\n' "$stage"
