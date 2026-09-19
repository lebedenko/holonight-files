#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
build_dir=${1:-build/debug}
[[ "$build_dir" = /* ]] || build_dir="$root/$build_dir"
metadata="$build_dir/apps/files/qml/hn-files/HolonightFiles/hn-files.qmltypes"
test -s "$metadata"
for type in DirectoryController DirectoryProxyModel PlacesModel VimModeController TaskManager PreviewService PreviewImageItem QuickLookPresentationModel WindowState IconFallbacks InspectionKeys SizeFormat; do
  grep -q "HolonightFiles/$type 1.0" "$metadata" || { echo "Missing metadata: $type" >&2; exit 1; }
done
echo 'Files QML metadata check passed.'
