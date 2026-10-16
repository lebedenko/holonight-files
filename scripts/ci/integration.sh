#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
task build:release
DESTDIR="$PWD/build/stage" cmake --install build/release
bash scripts/check-install.sh
bash scripts/prepare-runtime-check.sh
LC_ALL=en_US.UTF-8 ctest --preset dev
