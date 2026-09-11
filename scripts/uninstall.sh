#!/usr/bin/env bash
set -euo pipefail
prefix="${DESTDIR:-}/usr"
for file in "$prefix/bin/holonight-files" \
  "$prefix/share/applications/org.holonight.Files.desktop" \
  "$prefix/share/icons/hicolor/scalable/apps/org.holonight.Files.svg" \
  "$prefix/share/licenses/holonight-files/LICENSE" \
  "$prefix/share/licenses/holonight-files/GPL-3.0-or-later.txt"; do
  rm -f -- "$file"
done
licenses="$prefix/share/licenses/holonight-files"
if [[ -d "$licenses" ]]; then
  rmdir --ignore-fail-on-non-empty -- "$licenses"
fi
if [[ -d "$prefix/share/applications" ]]; then
  update-desktop-database "$prefix/share/applications"
fi
