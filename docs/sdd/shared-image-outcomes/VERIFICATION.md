# Verification

Date: 2026-09-23. Automated implementation acceptance completed before publication. The subsequent user request authorizes commit and pinning; publication and exact pins are recorded in the umbrella ledger.

## Environment

GCC 16.2.1 (20260810), Qt 6.11.2, Ninja. Release providers, tests disabled and Wayland provider build disabled. Consumer acceptance uses project-local installed providers:

- Images `3633865d2f39e4f163f0159a0f252f88245379f0` (unchanged).
- Qt `863af4183bdf09ce05199b37e8f5dfb46a311ba1`.
- Config `fe69a59e6b73167fd5349223a4d265d75386c139`.

Provider sources are clean; Files revision ledger matches, Viewer `task deps` refreshed/verified installed artifacts. Provider Qt-private ABI warnings require the same Qt build at runtime; isolated acceptance uses the existing CI base image and installed payloads only.

## Checks

- `cmake --build build/test --parallel 4`: passed; focused regressions below passed after updating internal-result assertions.
- `cmake --preset release` then `cmake --build --preset release --clean-first --parallel 4`: passed, full clean Release compilation without compiler warnings.
- `CMAKE_BUILD_PARALLEL_LEVEL=4 task check`: all stages passed, completed incrementally after one test-helper correction. Full CTest: 25/25 entries (main smoke: 582 passed, six existing native/opt-in performance skips). Full clang-tidy inspected 97 translation units; its sole actionable finding was unnamed parameters in the new failed-read test device. Named those parameters, then reran `clang-tidy -p=build/test -removed-arg=-mno-direct-extern-access --config-file=.clang-tidy tests/exif_reader_test.cpp tests/preview_service_test.cpp` successfully. Rebuilt `files-smoke`, reran `ExifReader.PreservesDeviceIoFailure` and `task format-check`, then completed `task qml-lint`, `task license-check`, `task install-check`, `task qml-import-check`, and `task qmltypes-check`: all passed.
- `bash scripts/prepare-runtime-check.sh`; `docker build --network none -t holonight-files-outcomes-runtime <generated-context>`; `docker run --rm --network none holonight-files-outcomes-runtime`: passed. No workspace mounts; installed payloads only.
- `git diff --check`: passed. Documentation links reviewed locally.

Focused command: `QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software build/test/tests/files-smoke --gtest_filter='ThumbnailService.*:*ThumbnailOrientation*:*ThumbnailCancellation*:PreviewDecodeLimits.*:ExifReader.*:PreviewService.*'`.
The initial run passed 96/97; its malformed PNG has invalid CRCs and correctly returns Damaged, not ResourceLimit. Corrected that expectation and added a valid oversized BMP header. The subsequent `--gtest_filter='PreviewDecodeLimits.*'` run passed 3/3. The full CTest run passed 25/25 entries, including the retained cancellation, orientation, timeout and cache-policy regressions.

New tests cover every outcome/category/message, cache miss versus cancellation, source I/O/unsupported/damaged/resource limits, controlled metadata device I/O failure, skipped metadata, successful empty metadata, malformed optional tags, quiet metadata limits with successful pixels, and selection reset. No artificial sleeps or global hooks were added.

## Evidence and review

Complete local logs are retained under `build/verification/shared-image-outcomes/` (ignored build artifacts). Build, test, static-analysis and runtime logs reviewed; expected corrupt-fixture libpng diagnostics and suppressed external-header tidy warnings do not indicate consumer failures. Final source diffs and local documentation links reviewed. No implementation or provider changes followed clean Release/runtime acceptance; later corrections affected test assertions and parameter names only.

## Limitations

Automated checks only. Native sharp-preview T5, mixed-monitor qualification and other previously deferred gates remain open. No performance claims. Commit and pinning are now authorized; umbrella integration remains deferred.

## Current qualification continuation — 2026-09-23

The original implementation evidence above is historical. The approved single-monitor
plan resumes umbrella integration and Files T5 against current published pins;
physical second-monitor qualification remains deferred until hardware arrives and
is not a closure gate for this iteration. Clipboard-service, unrelated release and
unknown-dimension fixture deferrals remain unchanged. Qualification initially
left new changes local; the subsequent **“publish and pin”** request authorizes
publication and the umbrella checkpoint.

Current acceptance complete — 2026-09-23: [umbrella evidence](../../../../docs/initiatives/shared-image-outcomes/SINGLE-MONITOR.md)
records passing provider/consumer/installer checks and the user report
**“walkthrough passed”**. Files T5 is complete on the approved actual 1/1.25/1.6/2
matrix, with original scale restored. The user subsequently authorized publication
and pinning; the umbrella ledger records exact published revisions, its final
integration decision and the single CI snapshot. Existing second-monitor,
clipboard-service, release and unknown-dimension deferrals remain unchanged.

Publication acceptance: the final single-monitor runner extension also passed a
complete `CMAKE_BUILD_PARALLEL_LEVEL=4 task check` before publication. See
[the current native report](../native-preview-acceptance/SINGLE-MONITOR.md) for
that log and unchanged clean-build/isolated-runtime evidence reuse.
