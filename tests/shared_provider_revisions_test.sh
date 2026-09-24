#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

source_repo=$1
workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT

mkdir -p "$workspace/files/scripts" "$workspace/bin" "$workspace/config" "$workspace/qt" "$workspace/images" "$workspace/system-services"
cp "$source_repo/scripts/prepare-deps.sh" "$workspace/files/scripts/prepare-deps.sh"

for provider in config qt images system-services; do
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
    HOLONIGHT_SYSTEM_SERVICES_SOURCE="$workspace/system-services" \
    HOLONIGHT_DEPENDENCY_PREFIX="$workspace/files/build/deps/prefix" \
    bash "$workspace/files/scripts/prepare-deps.sh"
}

call_count() {
  wc -l < "$workspace/cmake-calls" | tr -d ' '
}

: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 12

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

printf 'storage change\n' >> "$workspace/system-services/provider.txt"
git -C "$workspace/system-services" add provider.txt
git -C "$workspace/system-services" commit --quiet -m 'Update Storage provider'
: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 3
grep -q -- '-DBUILD_AUDIO=OFF -DBUILD_STORAGE=ON' "$workspace/cmake-calls"
