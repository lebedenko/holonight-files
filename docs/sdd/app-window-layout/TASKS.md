# SDD Tasks — app-window-layout

- [x] T-001: Add window.sidebarWidth property and rewire PlacesPanel binding
  - REQs: REQ-F-003, REQ-F-006, REQ-C-003
  - Check: `HnApplicationWindow` in `Main.qml` declares `property real sidebarWidth: 200`, and `PlacesPanel.Layout.preferredWidth` is bound to `window.sidebarWidth` (not a literal `200`), such that changing the property re-flows both the sidebar and any future header inset.

- [x] T-002: Create AppHeaderBar.qml with breadcrumb and sidebar-aware left inset
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-NF-002
  - Check: `apps/files/AppHeaderBar.qml` exists, wraps `HnHeaderBar` with a solid `HoloniightPalette.surface` background, declares `required property DirectoryController controller` and `required property real sidebarWidth`, displays the breadcrumb as read-only text bound to `controller.currentPath` with `Text.ElideMiddle` elision and left margin `= sidebarWidth + HnMetrics.internalSpacing(HnControlSize.Normal)`.

- [x] T-003: Wire AppHeaderBar into Main.qml at the top of the outer ColumnLayout
  - REQs: REQ-F-001, REQ-F-005, REQ-C-004
  - Check: `AppHeaderBar` is instantiated in `Main.qml`'s outer `ColumnLayout` as the first child (above the content `RowLayout`), with controller and sidebarWidth properties passed in; the header renders with opaque background and fixed height, and breadcrumb text updates in real time when navigating directories.

- [x] T-004: Add column-width readonly properties to DirectoryListing.qml root Item
  - REQs: REQ-F-007, REQ-F-008
  - Check: `DirectoryListing.qml`'s root `Item` declares four `readonly property real` values: `nameLeadingWidth: 20`, `sizeColumnWidth: 88`, `modifiedColumnWidth: 160`, and `columnSpacing: HnMetrics.internalSpacing(HnControlSize.Compact)`.

- [x] T-005: Add static directoryColumnHeader row above ListView and adjust ListView anchors
  - REQs: REQ-F-007, REQ-F-008, REQ-C-002
  - Check: A `Rectangle` with `objectName: "directoryColumnHeader"` is positioned at the top of `DirectoryListing` with height `HnMetrics.controlHeight(HnControlSize.Compact)`, contains three `HnLabel` headers ("Name", "Size", "Modified") styled with `HnTypographyRole.MicroHeader` and using `root.*` column-width properties, is separated from delegate rows by an `HnSeparator` at its bottom, and `ListView` anchors are changed from `anchors.fill: parent` to `anchors { top: columnHeader.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }`.

- [x] T-006: Rework DirectoryListing delegate contentItem to column-aligned RowLayout
  - REQs: REQ-F-008, REQ-C-002
  - Check: The delegate's `contentItem` is changed from a stacked card layout to a `RowLayout` with four cells in order (glyph slot with `Layout.preferredWidth: root.nameLeadingWidth`, name with `Layout.fillWidth: true`, size with `Layout.preferredWidth: root.sizeColumnWidth`, modified with `Layout.preferredWidth: root.modifiedColumnWidth`); all column x-positions align within 1px across all delegate rows to match the header row; the `inlineEditor` overlay (`objectName: "inlineNameEditor"`) and `filenameRuns` highlight-run rendering remain functionally unchanged and accessible.

- [x] T-007: Relocate ModeStatusBar from window top to bottom in Main.qml
  - REQs: REQ-F-010, REQ-F-005, REQ-C-002
  - Check: `ModeStatusBar` component is moved in `Main.qml`'s outer `ColumnLayout` to after the content `RowLayout` (as the last child per DESIGN.md rationale, after the existing keybinding-hints `Row`); it displays the same mode/status information and remains responsive to controller state changes.

- [x] T-008: Register AppHeaderBar.qml in CMakeLists.txt QML_FILES list
  - REQs: REQ-NF-002, REQ-C-001
  - Check: `apps/files/CMakeLists.txt` line 6 includes `AppHeaderBar.qml` in the `QML_FILES` list alongside the existing `Main.qml`, `PlacesPanel.qml`, etc.; `task build` succeeds with no CMake errors and the QML module registers the file.

- [x] T-009: Add AppHeaderBar.qml to Taskfile.yml and check-qml-format.sh format scripts
  - REQs: REQ-NF-002
  - Check: `Taskfile.yml` format task (line 57) includes `apps/files/AppHeaderBar.qml` in the qmlformat command, `scripts/check-qml-format.sh` (line 5) includes `apps/files/AppHeaderBar.qml` in the for-loop over QML files, and `task format-check` passes without format errors for the new file.

- [x] T-010: Review and update tests for delegate structure and accessibility preservation
  - REQs: REQ-NF-001, REQ-C-002
  - Check: `tests/smoke.cpp` test `PopulatedWindowKeyboardAndInlineError` passes (verifies `title` property binding for accessibility); `ModalEditingWindowKeyboardAndHighlighting` passes (verifies `inlineNameEditor` and `filenameRun` object names and search highlighting still work); `tests/directory_performance_test.cpp` delegate intersection detection (`hasVisibleDelegate()`) passes at the new single-line row height; no new test failures are introduced by the delegate rework.

- [x] T-011: Regenerate visual regression snapshots for all theme/scale combinations
  - REQs: REQ-NF-001
  - Check: `task visual-check` completes successfully and generates or updates snapshots in `build/visual/` for all theme/scale combinations (light/dark themes, scales 1.0/1.25/1.5) for modal-insert and modal-search states; the generated PNGs reflect the new header bar with breadcrumb, column-aligned file listing, and relocated footer.

- [x] T-012: Final full-suite verification (build + all tests + quality checks)
  - REQs: REQ-NF-001, REQ-F-005, REQ-F-012, REQ-C-001, REQ-C-002, REQ-C-003, REQ-C-004
  - Check: `task check` (full verification suite) passes: debug and release builds succeed, all test suites pass including visual regression comparisons, format-check and lint pass, licenses and installation checks pass, window minimum dimensions remain at 420×280, and the overall window layout matches SPEC REQ-F-005 (header → content {sidebar | listing | preview} → footer) with all explicit non-goals (REQ-C-004) not reintroduced.

- [x] T-013: Fix review findings: preserve filenames in narrow panes, align header
  and delegate columns, and align breadcrumb text with Name.
  - REQs: REQ-F-003, REQ-F-007, REQ-F-008, REQ-F-012
  - Check: focused window tests, geometry measurements at normal/narrow widths,
    formatting, and full verification; record limitations in VERIFICATION.md.

The checks in T-004 through T-007 describe the initial implementation. DESIGN.md
§8 and the review fixes supersede their glyph, typography, width, and hints-row
choices. T-012's historical completion does not constitute verification of the
subsequent review fixes; see the current VERIFICATION.md record.
