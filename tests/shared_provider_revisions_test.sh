#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

source_repo=$1
workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT

mkdir -p "$workspace/files/scripts" "$workspace/bin" "$workspace/config" "$workspace/qt" "$workspace/images" "$workspace/thumbnails" "$workspace/system-services" "$workspace/search"
cp "$source_repo/scripts/prepare-deps.sh" "$workspace/files/scripts/prepare-deps.sh"

for provider in config qt images thumbnails system-services search; do
  git -C "$workspace/$provider" init --quiet
  git -C "$workspace/$provider" config user.email test@example.invalid
  git -C "$workspace/$provider" config user.name 'Provider revision test'
  printf '%s\n' "$provider" > "$workspace/$provider/provider.txt"
  git -C "$workspace/$provider" add provider.txt
  git -C "$workspace/$provider" commit --quiet -m 'Initial provider revision'
done

cat > "$workspace/bin/cmake" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$CALL_LOG"
EOF
chmod +x "$workspace/bin/cmake"

run_deps() {
  PATH="$workspace/bin:$PATH" \
    CALL_LOG="$workspace/cmake-calls" \
    HOLONIGHT_CONFIG_SOURCE="$workspace/config" \
    HOLONIGHT_QT_SOURCE="$workspace/qt" \
    HOLONIGHT_IMAGES_SOURCE="$workspace/images" \
    HOLONIGHT_THUMBNAILS_SOURCE="$workspace/thumbnails" \
    HOLONIGHT_SYSTEM_SERVICES_SOURCE="$workspace/system-services" \
    HOLONIGHT_SEARCH_SOURCE="$workspace/search" \
    HOLONIGHT_DEPENDENCY_PREFIX="$workspace/files/build/deps/prefix" \
    bash "$workspace/files/scripts/prepare-deps.sh"
}

call_count() {
  wc -l < "$workspace/cmake-calls" | tr -d ' '
}

: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 18

: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 0

printf 'config change\n' >> "$workspace/config/provider.txt"
git -C "$workspace/config" add provider.txt
git -C "$workspace/config" commit --quiet -m 'Update config provider'

: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 3

printf 'qt change\n' >> "$workspace/qt/provider.txt"
git -C "$workspace/qt" add provider.txt
git -C "$workspace/qt" commit --quiet -m 'Update Qt provider'

: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 3

printf 'images change\n' >> "$workspace/images/provider.txt"
git -C "$workspace/images" add provider.txt
git -C "$workspace/images" commit --quiet -m 'Update image provider'

: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 3

printf 'thumbnails change\n' >> "$workspace/thumbnails/provider.txt"
git -C "$workspace/thumbnails" add provider.txt
git -C "$workspace/thumbnails" commit --quiet -m 'Update thumbnail provider'

: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 3

printf 'storage change\n' >> "$workspace/system-services/provider.txt"
git -C "$workspace/system-services" add provider.txt
git -C "$workspace/system-services" commit --quiet -m 'Update Storage provider'
: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 3
grep -q -- '-DBUILD_AUDIO=OFF -DBUILD_STORAGE=ON' "$workspace/cmake-calls"

printf 'search change\n' >> "$workspace/search/provider.txt"
git -C "$workspace/search" add provider.txt
git -C "$workspace/search" commit --quiet -m 'Update search provider'
: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 3
