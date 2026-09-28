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

## Thin divider correction — 2026-09-29

- Replaced the wide listing/preview handle slot with the existing vertical
  `HnSeparator`, retaining the token-sized drag target in a centered containment
  mask. Hover/pressed colors now read attached properties on the handle root.
- Extended `Files.WindowSeparatorJunctions` to check one-physical-pixel occupancy,
  pane adjacency, the full-height centered mask and containment on both sides,
  plus header/footer/column-header joins. Allow deferred SplitView layout to settle
  after resizing; the previous immediate capture could inspect stale geometry.
- Focused software rendering passes at DPR 1, 1.25, 1.5, 1.5625, 1.75 and 2,
  resizing through 1000/850/740/640/1000 logical pixels with the preview at its
  220px minimum. The existing 420px pane-allocation limitation above remains out
  of scope; actual settled layout at that width also exposes a far-window-edge
  pixel mismatch at DPR 1.5625. This regression uses usable narrow widths.
- Inspected normal and fractional captures at default/narrow widths under
  `build/divider-visual`: the horizontal rule meets the divider and the selected
  row retains rounded corners without an added rectangular gap.
- `task check` built Debug/Release and passed 20 of 21 CTest entries; the smoke
  entry failed only because the sandbox denied its Unix-socket fixture. Rerunning
  `ctest --test-dir build/test --output-on-failure -R '^files-smoke$'` outside the
  sandbox passed (opt-in native/performance checks remain skipped).
- Isolated runtime image build passed after granting Docker access. Acceptance
  stops at the base image's unwritable `/home/files-test`; a retry with a disposable
  writable home reaches launch but fails because `libwebp.so.7` is absent. Isolated
  runtime acceptance remains incomplete; no container or packaging changes made.
- Native acceptance: after being asked to check dragging from both sides,
  hover/pressed colors, adjacent scrollbar interaction and normal/fractional scales,
  the user confirmed: “Rendered and works correctly.” No native pointer/focus
  automation was used.
- Remaining `task check` stages completed individually: formatting, QML lint,
  licensing (outside the sandbox for Python worker sockets), staged installation,
  QML import policy and metadata all pass. Full C++ lint inspected 106 files and
  found one missing-parentheses diagnostic in the added mask-center assertion;
  corrected it and reran `clang-tidy` on `tests/separator_window_test.cpp`: pass.
