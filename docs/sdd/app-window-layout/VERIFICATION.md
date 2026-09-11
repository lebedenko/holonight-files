# Application window layout verification

Review remediation, 2026-09-11 (T-013):

- Used existing installed provider packages under `build/deps/prefix`; no provider
  sources were rebuilt or modified.
- `task check`: Debug and Release builds passed. The sandboxed test run stopped
  at the existing socket-copy test because its Unix socket bind was denied.
- `ctest --preset test` outside the sandbox: all eight CTest entries passed.
  Native inspection and rendered-directory performance acceptance remain skipped.
- Added `Files.WindowColumnAlignmentAndNarrowNames`: checks filename space,
  matching header/data visibility and geometry, and breadcrumb alignment while
  resizing through 1000, 850, 700, and back to 1000 logical pixels.
- `scripts/check-visual.sh` with installed provider paths: all four window tests
  passed in dark/light themes at scales 1, 1.25, and 1.5; captures in `build/visual`.
- A temporary geometry probe in `build/layout-review` measured breadcrumb and
  Name at x=214; Size header/data at x=467; Modified header/data at x=559 at
  default width. At 700px, the filename cell is 153px wide with metadata hidden.
  Default and narrow captures were inspected; filenames are visible and columns
  align. Logs and probe captures remain under `build/layout-review`.
- `task format-check`, `task qml-lint`, `task license-check`,
  `task install-check`, and `task uninstall-check`: passed. REUSE required an
  unsandboxed rerun because Python's worker pool binds a local socket.
- C++ lint: first run terminated with signal 15; the retry of
  `cmake --build build/test --target tidy` passed across all 42 translation units.
  All individual `task check` stages therefore passed, with the sandbox-related
  reruns noted above.

## Remaining acceptance

The pre-existing fixed 200px sidebar and 220px minimum preview leave the listing
with zero width at the 420px window minimum. This is a separate pane-allocation
limitation, not resolved by responsive metadata columns. REQ-F-012's full
minimum-window acceptance remains open. Native compositor visual acceptance also
remains pending; offscreen checks do not replace it.
