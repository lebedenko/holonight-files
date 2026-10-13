#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
exec python3 "$1/scripts/ci/test_provider_preparation.py"
