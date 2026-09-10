#!/usr/bin/env bash
set -euo pipefail
formatted=$(mktemp)
trap 'rm -f "$formatted"' EXIT
for qml in apps/files/Main.qml apps/files/DirectoryListing.qml apps/files/PlacesPanel.qml apps/files/PreviewPane.qml apps/files/QuickLookOverlay.qml apps/files/ModeStatusBar.qml; do
  /usr/lib/qt6/bin/qmlformat "$qml" > "$formatted"
  diff -u "$qml" "$formatted"
done
