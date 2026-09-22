#!/usr/bin/env bash
# Prepare only installed payloads for a container context beneath build/.
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
context=$(mktemp -d "$root/build/runtime-check.XXXXXX")
for provider in holonight-config holonight-qt holonight-images; do
  provider_build="$root/build/deps/$provider"
  if [[ "$provider" == holonight-config ]]; then
    provider_build=${HOLONIGHT_CONFIG_BUILD:-$provider_build}
  elif [[ "$provider" == holonight-qt ]]; then
    provider_build=${HOLONIGHT_QT_BUILD:-$provider_build}
  else
    provider_build=${HOLONIGHT_IMAGES_BUILD:-$provider_build}
  fi
  DESTDIR="$context/payload" cmake --install "$provider_build" --prefix /usr
done
DESTDIR="$context/payload" cmake --install "$root/build/release" --prefix /usr
mkdir -p "$context/check/scripts"
cp "$root/scripts/isolated-runtime.sh" "$context/check/scripts/"
cp "$root/packaging/Dockerfile.runtime-check" "$context/Dockerfile"
printf '%s\n' "${context#"$root/"}" > "$root/build/runtime-check-context"
printf 'Runtime context: %s\n' "$context"
