#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
task build:release
DESTDIR="$PWD/build/stage" cmake --install build/release
bash scripts/check-install.sh
bash scripts/prepare-runtime-check.sh
cmake -S . -B build/core -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_FILES_APP=OFF -DBUILD_FILE_BROWSER_QUICK=OFF -DBUILD_TESTING=ON
cmake --build build/core
ctest --test-dir build/core --output-on-failure --no-tests=error
cmake -S . -B build/quick -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_FILES_APP=OFF -DBUILD_FILE_BROWSER_QUICK=ON -DBUILD_TESTING=ON
cmake --build build/quick
ctest --test-dir build/quick --output-on-failure --no-tests=error
LC_ALL=en_US.UTF-8 ctest --preset dev
