# Provider verification — 2026-10-08

Work package I-001. Assigned upstream baseline: 3806c96c014e1897336c854cfb0ca490cec1d50d.
This verifies the browsing provider and Files adoption; it does not verify or integrate a portal backend.

## Environment and provider artifacts

Host: GCC 16.2.1 (20260810), Qt 6.12.0, C++23, Ninja. The source minimum remains Qt 6.11.
Dependencies were refreshed with `JOBS=4 python3 tooling/workflow.py deps`. The installed revision
ledger records Config d6a392b41991f70a004d58f7694c7b6115cb7280, SystemServices
39472e6dcafc93acea218a234213c346be256586, Qt 6c7ac33004702e166b8c152dcde918296be54286,
Images d834984dc413dc9e56f7f3157fa605d6a8667088, Thumbnails
2284b1822b0f8f856677e14d21d91e8fa10bd98c and Search 26067775e2eac3d9a779b3114dfbddc96834c7de.
Unchanged public-API provider archives use the same GCC toolchain; changed Qt/SystemServices providers
were rebuilt. This is build evidence, not an alternative to umbrella gitlinks.

## Builds and tests

Commands below are normalized to the Files checkout as working directory. Provider-only builds used
`/tmp/hn-file-browser-core` and `/tmp/hn-file-browser-quick`; application acceptance used a fresh
`build/filechooser-acceptance` directory. `provider_prefix` means the absolute `build/deps/prefix` path.

```sh
cmake -S . -B /tmp/hn-file-browser-core -G Ninja \
  -DBUILD_FILES_APP=OFF -DBUILD_FILE_BROWSER_QUICK=OFF -DBUILD_TESTING=ON
cmake --build /tmp/hn-file-browser-core -j 4
ctest --test-dir /tmp/hn-file-browser-core --output-on-failure

cmake -S . -B /tmp/hn-file-browser-quick -G Ninja -DBUILD_FILES_APP=OFF -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$provider_prefix" -DQML_IMPORT_PATH="$provider_prefix/lib/qt6/qml"
cmake --build /tmp/hn-file-browser-quick -j 4
ctest --test-dir /tmp/hn-file-browser-quick --output-on-failure

cmake -S . -B build/filechooser-acceptance -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_PREFIX_PATH="$provider_prefix" -DQML_IMPORT_PATH="$provider_prefix/lib/qt6/qml"
cmake --build build/filechooser-acceptance -j 4
ctest --test-dir build/filechooser-acceptance --output-on-failure
ctest --test-dir build/filechooser-acceptance -R '^files-smoke$' --output-on-failure

cmake -S . -B build/debug -G Ninja -DBUILD_TESTING=OFF \
  -DCMAKE_PREFIX_PATH="$provider_prefix" -DQML_IMPORT_PATH="$provider_prefix/lib/qt6/qml"
cmake --build build/debug -j 4
```

- Core-only build and both CTest entries pass without HoloNight providers or Qt Quick.
- Quick provider build and all four CTest entries pass. Installed consumers cover C++ Core, linked
  C++/QML Quick, and a QML plugin consumer that does not link Quick. No source-tree imports are used.
- The final Core suite has nine tests: sorting/hidden/Unicode/stat metadata, native non-UTF-8 folder and
  filename bytes, symlink-parent target metadata, stale generation rejection, invalid-location recovery,
  shutdown completion, cancellation after the first batch, reentrant loads and standard-place deduplication.
- Fresh Release acceptance completed 25 CTest entries. Initially 24 passed in the sandbox; the smoke
  suite's Unix socket bind fixture was denied by sandbox permissions. Repeating that suite outside the
  sandbox passed all 759 tests, with existing opt-in performance skips. The other 24 entries passed.
- Focused extraction regressions initially passed 211 tests. After prerequisite lint fixes, 173
  keyboard/sidebar/controller regressions passed. After the parent-entry correction, 154 browsing,
  controller, sorting and icon regressions passed, plus the four installed-provider CTest entries.
- Final Core-only tests pass after the parent-entry correction. Debug and Release builds pass;
  complete build logs were inspected and contain no actionable compiler warnings.

The final focused Files filters were:

```text
DirectoryController.*:VimModeController.*:PlacesWindow.*:Files.Places*:Files.Devices*:Files.Sidebar*:Files.PopulatedWindowKeyboardAndInlineError:Files.RuntimeStyleEditingShowsValidationAndCommits:FilesStorage.*
DirectoryModel.*:DirectoryProxyModel.*:DirectoryController.*:IconNameResolver.*:IconImageProvider.*:Files.PopulatedWindowKeyboardAndInlineError
```

They ran with `QT_QPA_PLATFORM=offscreen`, `QT_QUICK_BACKEND=software` and the installed provider QML path.
After corrections, only affected checks were repeated.

## Quality and installed runtime

```sh
python3 scripts/format-sources.py --check
python3 scripts/check-qml-import-policy.py
python3 scripts/check-qml-import-policy.py libs/file-browser/qml
python3 tooling/workflow.py tidy --scope all
cmake --build build/filechooser-acceptance --target qml-lint qmltypes-check
reuse lint
bash scripts/check-install.sh build/filechooser-acceptance
JOBS=4 CMAKE_BUILD_PARALLEL_LEVEL=4 python3 scripts/check-container-runtime.py
```

Formatting, import policy, full C++ lint, application/provider QML lint and generated QML metadata pass.
The final changed scanner and reader-test files passed focused C++ lint again after the parent correction.
REUSE passes for all 520 files; its multiprocessing check required access outside the sandbox.
These are the individual checks equivalent to `task check`, with the fresh Release test build replacing
preset test/release builds. A separate Debug build also passed.

Staged installation validates desktop payloads, Core/Quick libraries, CMake package and QML plugin/metadata,
then launches Files with only installed imports and a clean XDG fixture. It passes at the final source state.

Isolated runtime evidence is retained under `build/container-runtime.dnh7rgs1`. The original check built
current working sources and dependencies in a pinned image, staged installed payloads and launched Files
without networking or source imports. Its immutable CI image is
`sha256:aa92ed1425f097e6bb625ee8d9a4ce2034831e2d9ed57dd815991c355d3b0f9d` (Qt 6.12.0).
Corrections were overlaid in that same isolated source fixture; the compatible provider artifacts and
immutable image were reused to rebuild only affected Files/provider targets, restage and repeat the runtime
check. Final runtime image:
`sha256:6073021f12c050fe0f7da43c8436e0d74fcdfc8bcf833783f71cb1452e45ed17`.
The final desktop launch passed and was observed for three seconds. No desktop focus or pointer automation
was performed by the agent; interactive rendering regression tests ran offscreen.

## Remaining handoffs

No changes have been pushed or pinned. The standalone backend, Shell routing, isolated D-Bus/chooser tests,
manual native Hyprland/Sway/labwc acceptance, real broker tests and sandbox document access remain in
I-002–I-004. The initiative is Accepted, not Integrated. Reader shutdown is asynchronous, but destroying a
reader before its worker finishes joins any pending kernel filesystem call; consumers must retain it through
shutdownFinished when blocking mounts are possible.
