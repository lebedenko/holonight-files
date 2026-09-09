#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
mkdir -p "$root/build/visual"
export QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software
for mode in dark light; do
  for scale in 1 1.25 1.5; do
    HOLONIGHT_APPEARANCE_FILE="$root/tests/fixtures/$mode.toml" \
      QT_SCALE_FACTOR="$scale" FILES_CAPTURE_PREFIX="$root/build/visual/$mode-$scale" \
      "$root/build/test/tests/files-smoke" --gtest_filter=Files.*Window*
  done
done
