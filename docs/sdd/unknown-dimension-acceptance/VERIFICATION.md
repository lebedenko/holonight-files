# Verification

Date: 2026-09-23. Result: automated unknown-dimension acceptance passed locally.
No production/provider/API/format-support changes. The subsequent user request
**“commit, publish, pin”** authorizes publication and umbrella pinning.

## Environment and provider

GCC 16.2.1, Qt 6.11.2, Ninja. Tests link the installed
`build/deps/prefix/lib/libholonight_images.a`, not a mocked provider outcome.
`build/deps/provider-revisions.tsv` and clean provider working trees confirm:
Images `3633865d2f39e4f163f0159a0f252f88245379f0`, Qt
`863af4183bdf09ce05199b37e8f5dfb46a311ba1`, Config
`fe69a59e6b73167fd5349223a4d265d75386c139`.
Provider caches use the same system GCC/Qt, Release, BUILD_TESTING=OFF and
BUILD_WAYLAND=OFF; no provider sources or installed artifacts changed.

## Commands and results

1. `cmake --preset test`; build `files-unknown-dimensions`; `ctest --test-dir build/test -R '^files-unknown-dimensions$' --output-on-failure`: pass, four cases.
2. `QT_QPA_PLATFORM=offscreen build/test/tests/files-smoke --gtest_filter='ThumbnailService.*:*ThumbnailOrientation*:*ThumbnailCancellation*:PreviewDecodeLimits.*:ExifReader.*:PreviewService.*'`: pass, 98/98.
3. `CMAKE_BUILD_PARALLEL_LEVEL=4 task check`: debug/release builds pass. Of 26 CTest entries, 19 passed inside the sandbox; the existing socket-bind test and six OpenGL tests failed on sandbox permissions. `ctest --preset test --rerun-failed --output-on-failure` outside the sandbox passes all seven. All 26 entries therefore have passing results; this is not claimed as one uninterrupted task-check pass.
4. Continued the remaining task-check stages: full formatting, full clang-tidy (99 source files), QML lint, REUSE, staged installation, QML import policy and QML metadata. Tidy found only missing nodiscard annotations and protected data in the new fixture. Fixed those and reran `clang-tidy -p=build/test -removed-arg=-mno-direct-extern-access --config-file=.clang-tidy tests/unknown_dimensions/handler.cpp tests/unknown_dimensions_test.cpp`: pass. No unaffected expensive checks repeated. REUSE required an unsandboxed retry for its Python worker socket; licensing and staged-install/import/metadata checks pass.
5. Fresh Release test build: `cmake -S . -B build/unknown-dimension-acceptance -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix" -DQML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml"`; build only `files-unknown-dimensions --parallel 4`; isolated CTest: pass. Rebuilt/reran affected Release and Debug targets after the annotation/accessor correction: pass. Complete clean configure/build logs contain no warnings/errors.
6. Staged install `build/install-check.43lTjw` contains no synthetic handler or test executable. The private plugin is loaded only by the isolated process's explicit library-path addition. No install rules or normal runtime plugin paths changed.
7. `git diff --check`, changed C++ format check and local Markdown link validation: pass.

All raw build/check/test logs are retained in
`build/verification/unknown-dimensions/`. Initial link setup and positive-control
format-identification failures were corrected before acceptance; initial sandbox
and tidy failures remain in the logs. Only external-header tidy diagnostics are
suppressed by existing policy; no actionable warning remains.

The four cases prove the synthetic codec is readable and its read marker works;
real Images inspection/decode and both Files thumbnail routes reject with Damaged
and empty pixels without read() or disk publication; asynchronous preview reports
a decode error then recovers on valid selection; pre-cancelled calls return
Cancelled with empty pixels/no cache and the real presentation mapping is silent.
This closes the unknown-dimension runtime-fixture deferral. It adds no supported
format and does not reopen the accepted native scale matrix. Existing application
clean-build/isolated-runtime evidence remains applicable because product and install
payload sources are unchanged; the new test has its own fresh Release evidence.

Publication preparation: final source review confirms product and install payload
sources are unchanged from the accepted baseline. Existing compatible clean-build
and isolated-runtime evidence is reused; test/tooling changes have the focused
verification recorded above. Exact publication and CI snapshots belong to the
[umbrella ledger](../../../../docs/initiatives/shared-image-outcomes/TASKS.md).
