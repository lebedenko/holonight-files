# Verification — 2026-09-22

Implementation and Files acceptance are complete. The user confirmed native comparison matches in all cases.

## Baselines and artifacts

- Files started clean on `main` at `152cb98078e973fce74e020fd0e3b63bb4b5f046`.
- Images remains `efe3e780327fa793fb76c82b18fddde15298120b`.
- Viewer remains `149b86a908689477d1ba0cb4e7d486d091bf1024`.
- Reused Files dependency prefix: config `fe69a59e6b73167fd5349223a4d265d75386c139`,
  Qt `863af4183bdf09ce05199b37e8f5dfb46a311ba1`, and the Images revision above.
  Provider revision ledger and clean provider worktrees checked. GCC 16.2.1, Qt 6.11.2,
  Release providers, BUILD_TESTING=OFF and BUILD_WAYLAND=OFF; test and clean builds
  use the same system compiler/Qt and project-local installed prefix. Files and Viewer
  link byte-identical installed `libholonight_images.a` archives (SHA-256
  `96f69b2c4570763c22ae39f060d3cc118273f8a586c26924c5a56a1ac607b11e`).

## Automated evidence

- Before the production fix, `files-smoke --gtest_filter='ExifValues/*'` ran the
  16 new thumbnail regressions: 15 failed, proving stored-orientation and missing-marker defects.
- After the fix, `QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software
  build/test/tests/files-smoke --gtest_filter='ExifValues/*:ThumbnailService.*:PreviewService.*:PreviewStress.*:ExifReader.*'`
  passed 78/78. Separate `PreviewDecodeLimits.*` passed 2/2.
- Coverage includes independent quadrant permutations for EXIF 1–8, missing/invalid tags,
  rectangular bounds, no upscaling, every thumbnail tier, cold decoding, rejection of
  missing/wrong markers (including external/revisionless lookup), atomic regeneration,
  persisted markers and identical disk reads without a readable source. Existing source
  replacement, stale results, permissions and write-failure regressions remain in place.
- Pane/memory/disk/Quick Look tests use orientations 2, 6 and 7 and isolated temporary
  cache homes. Every image-bearing change notification and metadata completion preserves
  full-resolution oriented dimensions; memory reuse and upgrade pixel retention are checked.
- `QMLFORMAT=/usr/lib/qt6/bin/qmlformat task check` built Debug, Release and test presets.
  Its CTest phase hit sandbox restrictions: socket bind permission denied in
  `FileOperationService.SocketAndDeviceCopiesAreRejectedWithoutReading`, plus six OpenGL
  separator subprocess aborts. The seven failed CTest entries passed outside the sandbox with
  `ctest --preset test --rerun-failed --output-on-failure` (7/7), giving passing
  evidence for all 22 entries. Two opt-in native/performance smoke cases remain skipped.
  Remaining task-check components were run individually to avoid repeating unaffected builds/tests.
- Clean configure/build: `cmake -S . -B build/orientation-acceptance -G Ninja
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DCMAKE_INSTALL_PREFIX=/usr
  -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix"
  -DQML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml"`, then `cmake --build build/orientation-acceptance`.
  Full build log reviewed: no compiler warnings. Clean CTest initially passed 15/16,
  with the same sandbox socket failure; the failed test passed outside the sandbox with
  `ctest --test-dir build/orientation-acceptance --rerun-failed --output-on-failure`,
  giving passing evidence for all 16 default clean acceptance entries. The existing test
  preset additionally enables the six accelerated separator checks covered above.
- `task isolated-runtime-check` staged successfully but Docker socket access was denied
  in the sandbox. Authorized Docker build/run of that exact staged context passed with
  `--network none`, no workspace mount, correct directory handler and three-second desktop launch.
- Unchanged Images: `cmake --build ../holonight-images/build/acceptance` and
  `ctest --test-dir ../holonight-images/build/acceptance --output-on-failure` passed 2/2,
  including installed-package consumption.
- Unchanged Viewer: rebuilt `build/test`; offscreen `viewer-smoke` filtered to
  `Document.*:Canvas.*:Viewer.*Inspection*` passed 29/30, including all 26 Document tests
  (orientation, formats, limits, cancellation, stale results) and both Canvas tests.
  `Viewer.InspectionControlsAndLifecycle` failed at image_inspection_test.cpp:156 waiting
  for `openDialog` after Ctrl+O. No Viewer/provider source changes were made; this broader
  dialog-lifecycle failure is outside the orientation correction.

## Final check results

- Full clang-tidy analyzed 94 translation units. Four diagnostics were confined to
  the new thumbnail test (array indexing and a copied range-loop value); corrected
  with checked array access and a const reference. Focused clang-tidy recheck passed
  with no user-code diagnostics. Other units did not require another scan.
- Rebuilt the affected tests in Debug and the clean Release acceptance tree; final
  `ExifValues/*:ThumbnailService.*` runs passed 30/30 in each. Production artifacts
  were unchanged by these test-only corrections, so installation/runtime evidence remains valid.
- `QMLFORMAT=/usr/lib/qt6/bin/qmlformat task format-check`, `task qml-lint`,
  `task license-check`, `task install-check`, `task qml-import-check`, and
  `task qmltypes-check` passed. REUSE initially hit a sandbox multiprocessing socket
  restriction; the authorized unsandboxed retry passed. The existing Qt 6 formatter
  override was used; no formatter or infrastructure changes were made.
- Reviewed complete build/check logs, final diff and local SDD links; `git diff --check`
  passed. Only Files implementation, tests and documentation are included.
- All required Files automated check components have passing evidence. The original
  `task check` invocation stopped at sandbox failures; this record does not claim a
  single uninterrupted task-check pass. Viewer retains the unrelated dialog test failure above.

## Manual acceptance — passed

The user manually compared representative rotated/mirrored photos in Files and Viewer,
including the pane, Quick Look, oriented width × height labels, resizing and warm-cache
reopening, and reported: “Matches in all cases.” Updated binary: `build/release/apps/files/hn-files`.
Generated comparison fixtures are in `build/orientation-manual/exif-{1..8}.jpg`: stored
1200 × 800, displayed 1200 × 800 for values 1–4 and 800 × 1200 for values 5–8.
They use the same independent red/green/blue/yellow quadrant fixture as the tests. No native pointer or focus
interaction was automated. This completes the native acceptance requirement.

Publication and umbrella pin updates are not authorized and were not performed.
