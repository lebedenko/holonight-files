# Verification — sidebar icon delay

Dates: 2026-09-28–29. Approved user plan; no publication or umbrella pin changes.

## Focused evidence

- `task deps`: passed; existing project-local providers reused.
- `task format`: passed.
- `task build PRESET=test`: passed.
- Offscreen/software `build/test/tests/files-smoke` with filter
  `PreviewService.*:PreviewConsumers.*:PreviewIntegration.*:Files.PreviewSidebar*:Files.InspectionImageSplitterAndPixelSizing`:
  **57/57 passed**. Log: `build/sidebar-icon-delay/focused.log`.
- New service test checks one initial changed notification with busy already true,
  signal ordering, no notification for identical targets or resizing, and a reset for a new same-path revision.
- New QML state regression checks delayed fallback, pre-deadline hiding, rapid reset,
  elapsed deadline, fast image delivery, early failure, image-free completion,
  timeout state, clear, and image precedence while metadata remains busy.
- New real-worker sidebar regression holds dispatch until fallback appears, verifies
  partial thumbnail visibility before metadata completion, proves cached revisit
  avoids full decoding, and retains the thumbnail on resize.
- Existing service/integration tests cover actual timeout, stale-result suppression,
  rapid navigation, watcher refresh, synchronous folders/stat failures, partial delivery,
  cache reuse, and Quick Look behavior. Existing sidebar window tests cover fixed frame geometry.

The first focused run had 56/57 passes: a new geometry assertion used a windowless
QML fixture whose layout had not been polished. Removed that unrelated assertion;
existing window geometry coverage remains. The corrected focused suite passed.

The first full check stopped on two tests: a Quick Look test explicitly expected
the transient not-busy notification being removed, and a socket fixture was denied
by the sandbox. Updated the Quick Look test to assert the coherent busy selection
with the same pending/loaded geometry expectations; its production classification
logic is unchanged (only its outdated comment changed). `task check` passed outside the sandbox after that correction.

## Full acceptance

- `task check`: **passed**, exit 0 outside the sandbox. All 21 CTest targets passed,
  followed by formatting, C++/QML lint, REUSE, staged installation, import policy and
  generated QML metadata checks. Log: `build/sidebar-icon-delay/check.log`.
- Final cleanup: replaced an ignored `qWaitFor` result in the new test guard with an
  assertion. `cmake --build build/test --target files-smoke` passed without warnings;
  focused suite plus `QuickLookPresentationTest.*`: **67/67 passed**. Final
  `task format-check` passed. Logs: `final-build.log`, `focused-final.log` in the same directory.
- `task isolated-runtime-check`: **blocked by the existing runtime image**, not passed.
  Image creation succeeded, but the unmodified acceptance script stopped because
  `/home/files-test` was root-owned mode 0700, inaccessible to UID/GID 1000.
  A diagnostic rerun using the same image and a disposable writable home
  (`docker run --rm --network none --tmpfs /home/files-test:uid=1000,gid=1000,mode=0700 holonight-files-runtime-check`)
  passed that gate but failed at application startup with missing `libwebp.so.7`
  (exit 127). The existing `files-ci` base is `51d15f69535e`; the built runtime image
  is `9f27811d32a0`. No host install, base-image replacement, packaging edits or
  dependency pin changes were made. Rebuild a CI image with runtime dependencies
  matching the host-built payload before rerunning this acceptance gate.
  Logs: `runtime.log`, `runtime-trace.log`, `runtime-final.log` in the same directory.
- Manual native acceptance: pending. Navigate quickly through previously viewed
  images and back again; confirm cached thumbnails appear without brief icon flashes.
  Also select a folder, inspect a slow image, refresh the same path, and resize an
  existing preview. Slow loads intentionally show the icon after 150 ms.

Automated runs use offscreen rendering and do not claim native visual acceptance.
