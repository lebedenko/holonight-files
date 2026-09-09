#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
prefix=${HOLONIGHT_DEPENDENCY_PREFIX:-"$root/build/deps/prefix"}
for provider in holonight-config holonight-qt; do
  source_dir="$root/../$provider"
  if [[ $provider == holonight-config ]]; then
    source_dir=${HOLONIGHT_CONFIG_SOURCE:-$source_dir}
  else
    source_dir=${HOLONIGHT_QT_SOURCE:-$source_dir}
  fi
  cmake -S "$source_dir" -B "$root/build/deps/$provider" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$prefix" \
    -DBUILD_TESTING=OFF -DBUILD_TESTS=OFF -DBUILD_WAYLAND=OFF
  cmake --build "$root/build/deps/$provider" --parallel "${JOBS:-2}"
  cmake --install "$root/build/deps/$provider"
done
