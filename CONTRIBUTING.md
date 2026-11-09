# Contributing

Use `task build` and `task test` for incremental native CMake/CTest work. Native builds
consume installed packages; use `CMAKE_PREFIX_PATH` for an explicit development prefix.
Files requires installed shared QMarkdown 1.18 or newer, including its QML metadata.
Use `task format` for the pinned formatter and `task check` for container acceptance.
Run `task check:integration` for installation/runtime changes. Container builds use
fresh build/prefix directories, separate from `build/dev` and `build/release`.
clangd reads `build/dev/compile_commands.json` directly.

Routine changes need tests and a commit explanation; update user documentation when
behavior changes. Substantial designs need one local SDD describing intent, decisions,
interfaces and acceptance. Cross-project changes need one umbrella note linking local
designs and stating contracts, dependency order and integration checks. Historical SDDs
remain historical. No separate planning, readiness, handoff or closure commits are required.
Keep routine logs and generated evidence in ignored build directories or CI artifacts.
Provider and consumer implementation may proceed together; publish providers before
consumer commits that reference them. Update umbrella pins per coherent milestone.

Use C++23, Qt 6.11+, shared HoloNight controls and tokens, translated UI strings,
and native decorations. Preserve GPL-3.0-or-later metadata and third-party attribution.
System installation/removal belongs to the umbrella; standalone DESTDIR staging remains supported.

Image consumer acceptance has two lanes. Ordinary CTest and `task check` run the
portable selection in `tests/lanes.json`, plus the direct-provider unknown-size
checks. The inventory is compared with executable enumeration: unassigned,
duplicate and stale selections fail. Container success does not accept isolated
inspection, source/cache decoding, cancellation or cleanup.

Build against explicit host-native installed providers, then prepare a fresh,
non-installed test bundle (the helper and production format policy are unchanged):

```sh
python3 scripts/prepare-test-runtime.py \
  --prepare ../holonight-images/scripts/prepare-isolated-runtime.py \
  --helper "$PREFIX/libexec/holonight-images/runtime/usr/libexec/holonight-images-helper" \
  --plugins /usr/lib/qt6/plugins \
  --codec build/dev/tests/unknown-size-plugins/imageformats/libfiles-unknown-size-handler.so \
  --output build/unknown-size-runtime
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  QML_IMPORT_PATH="$PREFIX/lib/qt6/qml" \
  task test:isolated RUNTIME="$PREFIX/libexec/holonight-images/runtime" \
  TEST_RUNTIME="$PWD/build/unknown-size-runtime"
```

The synthetic TIFF recognizes only its private signature. Its known-size variant
proves helper codec loading; pixel decoding the unknown-size variant exits 86.
Successful unknown-size rejection requires `RuntimeStatus::Success`, `Damaged`,
no pixels and no cache. A crashing helper cannot satisfy this test.

Before publication, run the umbrella `scripts/check-isolated-previews.py` gate
as documented in `docs/initiatives/image-isolation.md` in the umbrella repository.
It requires the real user session manager, namespaces and cgroups and covers
staged and relocated prefixes. Required host skips and missing results fail.
