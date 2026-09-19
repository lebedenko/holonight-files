#!/usr/bin/env bash
set -euo pipefail
exec python3 "$(dirname "$0")/format-sources.py" --check --qml-only
