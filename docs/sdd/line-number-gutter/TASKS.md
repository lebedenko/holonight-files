# SDD Tasks — line-number-gutter

- [x] T-001: Add root properties and hidden metric label
  - REQs: REQ-F-004, REQ-NF-001, REQ-C-002, REQ-C-004
  - Check: DirectoryListing.qml root declares readonly properties lineNumberGutterDigits and lineNumberGutterWidth, with lineNumberGutterDigitsFor() function returning Math.max(3, String(rowCount).length), and a hidden HnLabel with role:Code and rawText bound to "9".repeat(lineNumberGutterDigits).

- [x] T-002: Add delegate gutter label cell and remove delegate left padding
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-005, REQ-C-001
  - Check: First child of delegate contentItem RowLayout is HnLabel (objectName "lineNumberGutterField") with Layout.preferredWidth: lineNumberGutterWidth, displaying (index === cursorRow) ? (index+1) : |index-cursorRow| in accentViolet (cursor) or textMuted (non-cursor), role: Code, horizontalAlignment: AlignRight, verticalAlignment: AlignVCenter, leftPadding: 8, rightPadding: 8, and delegate leftPadding: 0.

- [x] T-003: Add header spacer and adjust header anchors
  - REQs: REQ-F-009, REQ-F-005
  - Check: columnHeader RowLayout first child is Item (objectName "lineNumberGutterHeader") with Layout.preferredWidth/minimumWidth/maximumWidth all bound to lineNumberGutterWidth, and header RowLayout anchors.leftMargin changed to 0 (was: horizontalPadding).

- [x] T-004: Update inline editor and AppHeaderBar breadcrumb positioning
  - REQs: REQ-F-011, REQ-F-015
  - Check: inlineNameEditor anchors.leftMargin set to lineNumberGutterWidth (changed from uniform margins:8), and AppHeaderBar.breadcrumbLeftInset formula = sidebarWidth + internalSpacing(Normal) with no + horizontalPadding term.

- [x] T-005: Build and run existing tests with geometry adjustments
  - REQs: REQ-NF-002, REQ-C-005
  - Check: task build succeeds, existing smoke.cpp tests pass, directory_performance_test.cpp passes with all budgets unchanged, and WindowColumnAlignmentAndNarrowNames window-width expectations adjusted to new layout if needed (showSize/showModified formulas unchanged).

- [x] T-006: Add gutter numbering, cursor reactivity and no-recreation tests
  - REQs: REQ-F-002, REQ-F-003, REQ-F-006, REQ-C-001, REQ-NF-002
  - Check: smoke.cpp asserts lineNumberGutterField text/color for the cursor row (cursorRow+1, accentViolet) and two non-cursor rows (|index−cursorRow|, textMuted), including a VISUAL-selected non-cursor row, plus font.family == HolonightTheme.monospaceFont; after `j` and then `k` every visible label matches the new cursorRow, and row 0's lineNumberGutterField is the same QQuickItem* before and after `j` with changed text.

- [x] T-007: Add width and digit measurement tests including 999→1000 crossing
  - REQs: REQ-F-004, REQ-C-004, REQ-NF-001, REQ-F-015
  - Check: Standalone QMetaObject::invokeMethod calls to lineNumberGutterDigitsFor verify correct digit counts for 0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 99999; fixture asserts lineNumberGutterWidth = ceil(metric.implicitWidth)+16 with metric rawText "999"; one end-to-end test with 999-file fixture records width, creates 1000th file via watcher, and asserts metric rawText changed to "9999", width grew, and breadcrumbLabel scene x is unchanged (REQ-F-015).

- [x] T-008: Add sort, filter, and directory-change reactivity tests
  - REQs: REQ-F-007, REQ-F-008, REQ-F-006
  - Check: smoke.cpp (extending an existing hidden-files/sort test where one exists, otherwise a new small-fixture TEST) asserts every visible lineNumberGutterField matches its proxy index and cursorRow after toggling hidden files, after changing sort order, and after a watcher refresh that adds an entry above the cursor.

- [x] T-009: Add header spacer, column alignment, and breadcrumb positioning tests
  - REQs: REQ-F-009, REQ-F-010, REQ-F-015
  - Check: smoke.cpp extends WindowColumnAlignmentAndNarrowNames to assert lineNumberGutterHeader width = lineNumberGutterWidth, all Name/Size/Modified header-vs-delegate alignment remains ≤1px across 420/700/1000/1600px window widths, breadcrumb mapToScene.x() equals directoryListView mapToScene.x() ±1px across same widths, with the existing breadcrumb-vs-icon assertion replaced (not deleted) and its comment updated.

- [x] T-010: Add placeholder row numbering and inline editor positioning tests
  - REQs: REQ-F-011, REQ-F-012, REQ-C-003
  - Check: smoke.cpp extends o/O insert test to assert placeholder row's gutter shows correct value at insertion, after commit (post re-sort), and that remaining rows renumber after cancel; INSERT-mode test asserts inlineNameEditor's x mapped into its delegate (mapToItem) is >= that delegate's lineNumberGutterField right edge after the edit starts.

- [x] T-011: Add empty/error state and accessibility tests
  - REQs: REQ-F-013, REQ-F-014, REQ-NF-004
  - Check: smoke.cpp asserts an empty directory and a permission-denied directory each have zero lineNumberGutterField items and a visible lineNumberGutterHeader, and QQmlProperty(label, "Accessible.ignored", qmlContext(label)).read() is true for a populated row's gutter label.

- [x] T-012: Run full task check verification
  - REQs: REQ-NF-002, REQ-C-001, REQ-C-003, REQ-C-005
  - Check: task build, task test (all tests including directory_performance_test with existing budgets unchanged), task format-check, task tidy, and task qml-lint all pass with zero errors.

- [x] T-013: Manual native acceptance on Hyprland 1.5x display
  - REQs: REQ-NF-003
  - Check: Visual inspection on native Hyprland 1.5x fractional-scale display confirms gutter numbers render crisply (not blurry or pixelated), cursor row shows violet absolute line number, non-cursor rows show muted relative offsets, alignment consistent across directory sizes (0, 5, 999, 1000 entries), and gutter redraws sharply with same quality as other monospace text.
