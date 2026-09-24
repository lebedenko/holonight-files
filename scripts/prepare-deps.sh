#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "$0")/.." && pwd)
prefix=${HOLONIGHT_DEPENDENCY_PREFIX:-"$root/build/deps/prefix"}
state_file="$root/build/deps/provider-revisions.tsv"
parallel_args=()
if [[ -n ${JOBS:-} ]]; then
  parallel_args=(--parallel "$JOBS")
fi
providers=(holonight-config holonight-qt holonight-images holonight-system-services)
declare -A source_dirs revisions

for provider in "${providers[@]}"; do
  source_dir="$root/../$provider"
  if [[ $provider == holonight-config ]]; then
    source_dir=${HOLONIGHT_CONFIG_SOURCE:-$source_dir}
  elif [[ $provider == holonight-qt ]]; then
    source_dir=${HOLONIGHT_QT_SOURCE:-$source_dir}
  elif [[ $provider == holonight-images ]]; then
    source_dir=${HOLONIGHT_IMAGES_SOURCE:-$source_dir}
  else
    source_dir=${HOLONIGHT_SYSTEM_SERVICES_SOURCE:-$source_dir}
  fi

  source_dir=$(cd "$source_dir" && pwd -P)
  revision=$(git -C "$source_dir" rev-parse --verify HEAD)
  source_dirs["$provider"]=$source_dir
  revisions["$provider"]=$revision
done

needs_refresh=()
for provider in "${providers[@]}"; do
  expected="$provider"$'\t'"${source_dirs[$provider]}"$'\t'"${revisions[$provider]}"
  if [[ ! -f $state_file ]] || ! grep -Fqx "$expected" "$state_file"; then
    needs_refresh+=("$provider")
  fi
done

if ((${#needs_refresh[@]} == 0)); then
  printf 'Provider revisions are current in %s\n' "$prefix"
  exit 0
fi

for provider in "${needs_refresh[@]}"; do
  source_dir=${source_dirs[$provider]}
  component_args=()
  if [[ $provider == holonight-system-services ]]; then
    component_args=(-DBUILD_AUDIO=OFF -DBUILD_STORAGE=ON)
  fi
  cmake -S "$source_dir" -B "$root/build/deps/$provider" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$prefix" \
    -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$prefix" \
    -DBUILD_TESTING=OFF -DBUILD_TESTS=OFF -DBUILD_WAYLAND=OFF "${component_args[@]}"
  cmake --build "$root/build/deps/$provider" "${parallel_args[@]}"
  cmake --install "$root/build/deps/$provider"
done

mkdir -p "$(dirname "$state_file")"
temporary_state=$(mktemp "${state_file}.XXXXXX")
trap 'rm -f "$temporary_state"' EXIT
for provider in "${providers[@]}"; do
  printf '%s\t%s\t%s\n' "$provider" "${source_dirs[$provider]}" "${revisions[$provider]}" >> "$temporary_state"
done
mv "$temporary_state" "$state_file"
trap - EXIT
