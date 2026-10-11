#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later

set -euo pipefail

source_repo=$1
workspace=$(mktemp -d)
trap 'rm -rf "$workspace"' EXIT

mkdir -p "$workspace/files/scripts" "$workspace/bin" "$workspace/config" "$workspace/qt" "$workspace/images" "$workspace/thumbnails" "$workspace/system-services" "$workspace/search" "$workspace/markdown"
cp "$source_repo/scripts/prepare-deps.sh" "$workspace/files/scripts/prepare-deps.sh"
cp -r "$source_repo/tooling" "$workspace/files/tooling"

for provider in config qt images thumbnails system-services search markdown; do
  printf "project(fixture)\n" > "$workspace/$provider/CMakeLists.txt"
  git -C "$workspace/$provider" init --quiet
  git -C "$workspace/$provider" config user.email test@example.invalid
  git -C "$workspace/$provider" config user.name 'Provider revision test'
  printf '%s\n' "$provider" > "$workspace/$provider/provider.txt"
  git -C "$workspace/$provider" add provider.txt CMakeLists.txt
  git -C "$workspace/$provider" commit --quiet -m 'Initial provider revision'
done

cat > "$workspace/bin/cmake" <<'EOF'
#!/usr/bin/env bash
printf '%s\n' "$*" >> "$CALL_LOG"
if [[ $1 == --install ]]; then
  case ${2##*/} in
    holonight-config) package=HoloNightConfig ;;
    holonight-qt) package=HolonightQt ;;
    holonight-images) package=HolonightImages ;;
    holonight-thumbnails) package=HolonightThumbnails ;;
    holonight-system-services) package=HoloNightSystemServices ;;
    holonight-search) package=HolonightSearch ;;
    qmarkdown) package=QMarkdown ;;
  esac
  mkdir -p "$INSTALL_FIXTURE_PREFIX/lib/cmake/$package"
  printf '# fixture\n' > "$INSTALL_FIXTURE_PREFIX/lib/cmake/$package/${package}Config.cmake"
fi
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
    QMARKDOWN_SOURCE="$workspace/markdown" \
    INSTALL_FIXTURE_PREFIX="$workspace/files/build/deps/prefix" \
    env -u HOLONIGHT_DEPENDENCY_PREFIX bash "$workspace/files/scripts/prepare-deps.sh"
}

call_count() {
  wc -l < "$workspace/cmake-calls" | tr -d ' '
}

run_remembered_deps() {
  PATH="$workspace/bin:$PATH" \
    CALL_LOG="$workspace/cmake-calls" \
    INSTALL_FIXTURE_PREFIX="$workspace/files/build/deps/prefix" \
    env -u HOLONIGHT_DEPENDENCY_PREFIX -u HOLONIGHT_CONFIG_SOURCE \
      -u HOLONIGHT_QT_SOURCE -u HOLONIGHT_IMAGES_SOURCE -u HOLONIGHT_THUMBNAILS_SOURCE \
      -u HOLONIGHT_SYSTEM_SERVICES_SOURCE -u HOLONIGHT_SEARCH_SOURCE -u QMARKDOWN_SOURCE \
      bash "$workspace/files/scripts/prepare-deps.sh"
}

: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 21

: > "$workspace/cmake-calls"
run_deps
test "$(call_count)" = 0

# Normal builds must reuse successful overrides without requiring exported variables.
run_remembered_deps
test "$(call_count)" = 0

# An explicit invalid override must fail instead of falling back to remembered state.
if PATH="$workspace/bin:$PATH" CALL_LOG="$workspace/cmake-calls" \
  QMARKDOWN_SOURCE="$workspace/missing" env -u HOLONIGHT_DEPENDENCY_PREFIX \
  bash "$workspace/files/scripts/prepare-deps.sh"; then
  exit 1
fi
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

printf 'markdown change\n' >> "$workspace/markdown/provider.txt"
git -C "$workspace/markdown" add provider.txt
git -C "$workspace/markdown" commit --quiet -m 'Update Markdown provider'
: > "$workspace/cmake-calls"
run_remembered_deps
test "$(call_count)" = 3
rg -q -- '-DBUILD_SHARED_LIBS=ON -DQMARKDOWN_BUILD_EXAMPLES=OFF -DQMARKDOWN_BUILD_BENCHMARKS=OFF' "$workspace/cmake-calls"
