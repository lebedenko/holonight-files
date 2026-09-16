# Line-Number Gutter in Directory Listing

## Context

HoloNight Files displays a directory listing (file names, sizes, timestamps, icons) in a column-aligned, keyboard-driven layout. The mockup in `docs/mockups/moc1.png` shows a Vim-style modal interface with NORMAL mode bindings for navigation. To enhance keyboard-driven navigation and file reference (common in terminal/editor workflows), the listing shall display a line-number gutter as the first column of each row, following the Vim hybrid numbering mode (absolute number at cursor row, relative offsets for all others).

This specification formalizes the addition of a line-number gutter to the DirectoryListing component, using the design system's monospace font, with reactive updates on cursor movement, sorting, filtering, and directory changes. The gutter width is measured dynamically to accommodate the entry count, and the gutter sits flush at the row's left edge, replacing the row's leading columnPadding.

The feature is scoped to the DirectoryListing component plus one alignment change in AppHeaderBar (the breadcrumb aligns with the list view's left edge, REQ-F-015); it does not interact with multi-selection (VISUAL mode display uses relative numbers unchanged), PlacesPanel, PreviewPane, or QuickLookOverlay.

---

## Explicit Non-Goals

The following capabilities are explicitly OUT OF SCOPE for this specification:

1. **Togglable Gutter via `:set nu` / `:set rnu`** — The gutter is always displayed; there is no user setting, toggle, or Vim command to enable/disable it.
2. **Absolute-Only or Relative-Only Numbering** — The gutter displays only Vim hybrid mode (absolute at cursor, relative elsewhere); no mode variant is offered.
3. **Mouse Interaction with Gutter** — The gutter is not clickable; mouse clicks on the gutter behave the same as clicks on any other part of the row (e.g., selection, delegation to existing click handlers).
4. **User-Resizable Gutter** — The gutter width is computed automatically; no mouse drag, edge-handle, or setting adjusts it.
5. **Count-Prefix Key Handling Changes** — Vim-style count prefixes (e.g., `5j`) for navigation are unaffected; the gutter displays numbers but does not add new count-prefix semantics.
6. **Changes to PlacesPanel, PreviewPane, or QuickLookOverlay** — These components remain unchanged; only DirectoryListing (and AppHeaderBar's breadcrumb inset, REQ-F-015) is affected.
7. **VISUAL-Mode Display Changes** — VISUAL (multi-selection) mode highlighting and colour scheme are unchanged; selected rows still show relative numbers in textMuted, cursor row in accentViolet.
8. **Narrow-Window Column Hiding** — The `showSize`/`showModified` width thresholds in DirectoryListing are kept exactly as they are, even though the gutter consumes horizontal space. Narrow-window layout is a separate, more complex problem addressed in a follow-up SDD cycle.

---

## Requirements

### Gutter Display and Numbering

#### REQ-F-001: Line-Number Gutter as First Column

**EARS:** Ubiquitous

The system shall add a line-number gutter as the first cell of each directory listing delegate's `contentItem` RowLayout, positioned before the icon column (if present) or Name column, and separated by the existing `columnSpacing`. The gutter width is a shared read-only property on the DirectoryListing root, measured dynamically based on row count.

**Acceptance Criteria:**
- A new RowLayout cell containing the line-number label is the first child of the delegate's `contentItem`, before all other columns.
- The cell contains an `HnLabel` component displaying the computed line number (absolute or relative, per REQ-F-002).
- The cell's width is derived from a shared `readonly property real lineNumberGutterWidth` on the DirectoryListing root, not a hardcoded value.
- The gutter width remains constant across all rows and does not resize with content.
- The gutter is Accessible.ignored (marked as a visual aid, not a semantic element).

---

#### REQ-F-002: Vim Hybrid Numbering Mode

**EARS:** State-driven

While the cursor row displays at index `cursorRow`, the gutter shall display the absolute 1-based line number `cursorRow + 1` in the design system's accent violet colour; every other row shall display the absolute distance `|rowIndex − cursorRow|` (visual offset) in the design system's text muted colour. When the cursor moves, all visible gutter numbers shall update reactively.

**Acceptance Criteria:**
- For a directory with 5 entries (indices 0–4) and cursor at index 2: row 0 displays "2" (textMuted), row 1 displays "1" (textMuted), row 2 displays "3" (accentViolet), row 3 displays "1" (textMuted), row 4 displays "2" (textMuted).
- The accentViolet colour is `HoloniightPalette.accentViolet`; the textMuted colour is `HoloniightPalette.textMuted`.
- When the cursor moves one row down (to index 3), all visible numbers update: row 0 → "3", row 1 → "2", row 2 → "1", row 3 → "4" (accentViolet), row 4 → "1".
- VISUAL-selected rows that are not the cursor row still display relative numbers in textMuted; no special colouring is applied to selected rows.
- A unit test asserts the computed number for each row given cursor position and row count.

---

#### REQ-F-003: Monospace Font and Alignment

**EARS:** Ubiquitous

The system shall render all gutter numbers using the design system's monospace font (via `HnLabel { role: HnTypographyRole.Code }`), right-aligned within the gutter cell, and vertically centred in the row.

**Acceptance Criteria:**
- Every gutter label is an `HnLabel` with `role: HnTypographyRole.Code`, which binds to `HolonightTheme.monospaceFont` and `HolonightTheme.monospaceFontSize`.
- The label's text is right-aligned (horizontally) within the gutter cell using `HnLabel { horizontalAlignment: Text.AlignRight }`.
- The label is vertically centred in the RowLayout cell (using RowLayout alignment or anchoring).
- Code review confirms no hardcoded font family or size is used.

---

#### REQ-F-004: Gutter Width Measurement

**EARS:** Ubiquitous

The system shall measure the gutter width dynamically based on the number of entries visible to the ListView (including any `o`/`O` placeholder row), using a hidden reference `HnLabel` with monospace font and repeated '9' characters. The width is `max(3, digits(rowCount))` nines (default minimum width is that of "999"), plus 8 pixels left padding and 8 pixels right padding, where `digits(n)` is the number of decimal digits in `n`.

**Acceptance Criteria:**
- For a directory with 0–999 rows: gutter width is the measured text width of "999" plus 16 pixels (8px left + 8px right).
- For a directory with 1,000–9,999 rows: gutter width is the measured text width of "9999" plus 16 pixels.
- For a directory with ≥10,000 rows: gutter width is the measured text width of the appropriate count of nines plus 16 pixels.
- The reference HnLabel is hidden (e.g., `visible: false`) and contains `rawText: "9" × max(3, digits(rowCount))`, matching the role and font of the displayed numbers.
- The gutter width updates reactively when the row count changes (e.g., after directory listing refresh, after `o`/`O` insert or delete).
- A test asserts the digit count for counts 0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, and one end-to-end test asserts the measured width grows when a 999-entry directory gains its 1000th entry.

---

#### REQ-F-005: Gutter Left Padding and Column Spacing

**EARS:** Ubiquitous

The system shall position the gutter flush at the row's left edge, with 8 pixels internal left padding (inside the gutter cell), and shall replace the row's leading `columnPadding`. The gutter's right padding (8 pixels) plus the existing `columnSpacing` separate the gutter from the icon column or Name column that follows.

**Acceptance Criteria:**
- The gutter's left edge aligns with the row's left edge (at x = 0 relative to the row).
- The gutter cell includes 8 pixels left padding (margin or internal layout property).
- The gutter cell includes 8 pixels right padding (margin or internal layout property).
- The spacing between the gutter's right edge and the next column's left edge is exactly `columnSpacing`.
- The row's original leading `columnPadding` (if present before the icon column) is eliminated for rows with the gutter.
- Rows with icon column: gutter (with 8px right padding) + columnSpacing + icon column + columnSpacing + Name column.

---

### Gutter Updates and Reactivity

#### REQ-F-006: Reactive Updates on Cursor Movement

**EARS:** Event-driven

When the cursor row changes (via keyboard navigation, scrolling, or programmatic `cursorRow` change), all visible gutter numbers shall update reactively through bindings, without requiring a model refresh or delegate recreation.

**Acceptance Criteria:**
- After pressing `j`, every visible gutter label shows the value computed from the new `cursorRow`.
- After pressing `k`, every visible gutter label shows the value computed from the new `cursorRow`.
- No delegate is recreated; only the gutter label text and colour properties change.
- A smoke test confirms the gutter label's text and colour binding responds to cursorRow changes without requiring an explicit model.reset() call.

---

#### REQ-F-007: Reactive Updates on Sort and Filter

**EARS:** Event-driven

When the directory listing is re-sorted or re-filtered (sort key/order change, hidden-files toggle), all visible gutter numbers shall update to reflect the new row positions.

**Acceptance Criteria:**
- Toggling hidden files changes the visible row count; every visible gutter label shows the value computed from its new proxy index and the post-toggle `cursorRow`.
- Changing sort order changes row order; every visible gutter label shows the value computed from its new proxy index and the post-sort `cursorRow`.
- A smoke test toggles hidden files and asserts the gutter values of all visible rows.

---

#### REQ-F-008: Reactive Updates on Directory Changes

**EARS:** Event-driven

When the directory listing is refreshed due to filesystem changes (watcher event, manual refresh), added, or removed entries, all visible gutter numbers shall update reactively. The `o`/`O` placeholder row is numbered like any other row; when it is inserted, neighbours renumber.

**Acceptance Criteria:**
- Inserting an entry via `o` (new file) assigns it a line number and renumbers all rows below it.
- Deleting an entry renumbers remaining rows.
- A directory refresh (e.g., Ctrl+R or watcher event) that adds or removes entries renumbers the gutter.
- A smoke test inserts a placeholder row and asserts all gutter numbers updated.

---

### Header Alignment

#### REQ-F-009: Header Spacer Column

**EARS:** Ubiquitous

The system shall add a blank spacer column in the header row, immediately before the Name column header, with width equal to `lineNumberGutterWidth` (matching the first column width). The spacer is a visual placeholder; no label or separator appears in it.

**Acceptance Criteria:**
- The header row RowLayout has a new first child spacer Item with `width: lineNumberGutterWidth`.
- The spacer has no text, label, or visible content; it is blank space.
- No vertical separator line appears between the spacer and the Name column (unlike separators between Size and Modified columns, which are retained).
- The Name column header label's left edge aligns with the Name column cell's left edge in all rows, within 1 pixel.

---

#### REQ-F-010: Header Column Alignment Within 1 Pixel

**EARS:** Ubiquitous

The system shall maintain column alignment between the header row and all delegate rows such that the Name, Size, and Modified column headers' left edges align with their corresponding delegate cells' left edges, within 1 pixel tolerance.

**Acceptance Criteria:**
- Across all window widths (420 px to 1600 px+), the header Name column's left edge and the delegate Name cell's left edge differ by at most 1 pixel.
- Across all window widths, the header Size column's left edge and the delegate Size cell's left edge differ by at most 1 pixel.
- Across all window widths, the header Modified column's left edge and the delegate Modified cell's left edge differ by at most 1 pixel.
- A smoke test measures column offsets for window widths 420, 800, 1200, and 1600 pixels and asserts ≤ 1 pixel difference.

---

### Inline Editing and Placeholder Rows

#### REQ-F-011: Inline Editor Positioned After Gutter

**EARS:** Ubiquitous

The system shall position the INSERT-mode inline name editor (`objectName: "inlineNameEditor"`) such that its left edge is at or after the gutter's right edge, keeping gutter numbers visible while editing. The editor's anchoring and margins are adjusted as necessary to clear the gutter.

**Acceptance Criteria:**
- The inline editor's left anchor is set to `parent.left` with a left margin equal to `lineNumberGutterWidth` (the gutter's right edge), or the editor's `x` property is set to `lineNumberGutterWidth`.
- The gutter numbers remain visible and not covered by the editor while editing.
- A smoke test enters INSERT mode, starts editing, and asserts the inline editor's left edge is ≥ gutter width.
- The editor's height, vertical centering, and text input behavior remain unchanged.

---

#### REQ-F-012: Placeholder Row Numbering

**EARS:** Ubiquitous

The system shall assign a gutter line number to the `o`/`O` INSERT-mode placeholder row, computed the same way as all other rows: absolute number at cursor row, relative distance for non-cursor rows. When the placeholder is committed or cancelled, the resulting rows are numbered from their resulting proxy indices (a committed entry may re-sort to a different position).

**Acceptance Criteria:**
- Creating a new file via `o` inserts a placeholder row; its gutter number is computed from its index and the current cursor row.
- If the cursor is at row 5 and `o` inserts a placeholder at row 6, the cursor moves onto the placeholder, so it displays its absolute number 7 in accentViolet, and the former cursor row displays 1 (relative distance).
- Entering the name and confirming commits the row; every visible gutter label matches its (possibly re-sorted) proxy index and `cursorRow`.
- Cancelling the edit (`Escape`) removes the placeholder; remaining rows renumber.
- A smoke test creates, confirms, and deletes a placeholder row and asserts gutter numbers updated correctly.

---

#### REQ-F-015: Breadcrumb Aligned with List View

**EARS:** Ubiquitous

The system shall position the header bar's breadcrumb label so that its left edge aligns with the directory list view's left edge (the gutter's left edge), rather than with the icon column.

**Acceptance Criteria:**
- At window widths 420, 700, 1000, and 1600 pixels, the breadcrumb label's scene x and the `directoryListView` scene x differ by at most 1 pixel.
- The breadcrumb's x does not change when the row count crosses a digit boundary (e.g. 999 → 1000 entries).
- The existing smoke assertion comparing breadcrumb x to the icon column x is replaced by the list-view comparison, not deleted.
- `AppHeaderBar` derives the inset from existing metrics (`sidebarWidth` and `HnMetrics`), not from a hardcoded pixel literal and not from the gutter width.

---

### Empty and Error States

#### REQ-F-013: Empty Directory Gutter Behavior

**EARS:** State-driven

While a directory is empty (no entries, no error), the ListView displays no rows and thus no gutter numbers. However, the header row remains visible with its blank spacer column, maintaining visual consistency.

**Acceptance Criteria:**
- An empty directory displays no rows in the ListView; no gutter numbers appear.
- The header row is still visible; the spacer column is present with width `lineNumberGutterWidth`.
- A directory that transitions from non-empty to empty (due to filter or delete-all) removes all gutter numbers but keeps the header.
- A smoke test asserts no gutter labels exist when a directory is empty.

---

#### REQ-F-014: Directory Error State Gutter Behavior

**EARS:** State-driven

While the directory listing encounters an error (permission denied, symlink loop, etc.) and no rows are displayed, the header row remains visible with its blank spacer column. No error message or placeholder rows appear in the gutter.

**Acceptance Criteria:**
- A listing error (e.g., permission denied on a directory) prevents rows from loading; no gutter numbers appear.
- The header row is still visible with the spacer column.
- An error state that clears (user navigates away and back) repopulates the gutter when rows reappear.
- A smoke test simulates a permission-denied error and asserts the header spacer is visible but no gutter labels appear.

---

### Non-Functional Requirements

#### REQ-NF-001: Gutter Width Measurement on Initialization and Update

**EARS:** Ubiquitous

The gutter width shall be measured once during DirectoryListing initialization and updated reactively whenever the row count changes. Measurement shall use a hidden `HnLabel` with the actual design-system monospace font, avoiding hand-derived font metrics.

**Acceptance Criteria:**
- The hidden reference label is created with `role: HnTypographyRole.Code` and `visible: false`.
- The label's `implicitWidth` (after text layout) is read to derive the gutter width, not from `QFontMetricsF` or hardcoded values.
- Gutter width updates when row count changes (e.g., after directory refresh).
- Font changes (theme updates) automatically update the gutter width because the reference label's font updates.

---

#### REQ-NF-002: Cursor-Move Latency Budget

**EARS:** Ubiquitous

Per-cursor-move rebinding of visible delegates shall not cause `tests/directory_performance_test.cpp` to exceed any of its existing latency budgets. The gutter numbers update reactively without recreating delegates or forcing full ListView redraws.

**Acceptance Criteria:**
- `directory_performance_test` passes unmodified (no budget relaxed) with the gutter present.
- A smoke test records a gutter label object before a `j` press and asserts the same object instance (not a recreated delegate) reflects the new value after it.

---

#### REQ-NF-003: Fractional-Scale Rendering

**EARS:** Ubiquitous

Gutter numbers shall render crisply at fractional display scales (e.g., Wayland at 1.5x on Hyprland). Text positioning and antialiasing must match the design system's monospace rendering elsewhere in the application.

**Acceptance Criteria:**
- Visual inspection on a native Hyprland 1.5x display confirms gutter numbers are sharp, not blurry or pixelated.
- No screenshot-based test or offscreen rendering is relied upon; only native on-display inspection is accepted for verification.
- Gutter numbers render with the same sharpness as other monospace text in the application (e.g., in code previews if present).

---

#### REQ-NF-004: Accessible Gutter Marking

**EARS:** Ubiquitous

The gutter shall be marked as a visual-aid-only element in the accessibility tree, not read aloud or included in focus navigation. The gutter label shall have `Accessible.ignored: true`.

**Acceptance Criteria:**
- The gutter label's Accessible.ignored property is set to true.
- The gutter label has no `activeFocusOnTab` and never receives focus.
- The delegate's overall accessibility (row name, file info) remains unchanged; only the gutter number is ignored.

---

### Constraints

#### REQ-C-001: Design-System Styling

**EARS:** Constraint

The gutter shall use only the HoloNight design system's colors and fonts. Text and background colors are derived from `HoloniightPalette` and `HolonightTheme` singletons; no hardcoded colors (#RRGGBB), CSS, or custom fonts are used.

**Acceptance Criteria:**
- Cursor row text colour: `HoloniightPalette.accentViolet` (via `HnLabel { color: ... }`).
- Non-cursor row text colour: `HoloniightPalette.textMuted`.
- Font: `HolonightTheme.monospaceFont` and `HolonightTheme.monospaceFontSize` (via `HnLabel { role: HnTypographyRole.Code }`).
- No CSS or inline colour literals (e.g., `color: "#7c3aed"`).
- Code review confirms all colors and fonts are palette/theme properties.

---

#### REQ-C-002: Shared Read-Only Property Pattern

**EARS:** Constraint

The gutter width shall be exposed as a shared `readonly property real lineNumberGutterWidth` on the DirectoryListing root, following the established pattern of `iconColumnWidth`, `sizeColumnWidth`, `modifiedColumnWidth`, `columnSpacing`, and `columnPadding`. This allows other components (e.g., the header, inline editor) to reference the width without computing it independently.

**Acceptance Criteria:**
- A new property `readonly property real lineNumberGutterWidth` is declared on the DirectoryListing root Item.
- The property is computed from the hidden reference label's width plus 16 pixels (8px left + 8px right padding).
- The header row's spacer column binds to `width: lineNumberGutterWidth`.
- The inline editor's left margin binds to `lineNumberGutterWidth`.
- The gutter cell's width binds to `width: lineNumberGutterWidth`.
- Code review confirms no hardcoded width values are duplicated in multiple locations.

---

#### REQ-C-003: Qt6 and QML Only

**EARS:** Constraint

The gutter implementation shall use only Qt6 QML components and the holonight-qt design system. No new C++ backend classes, roles, or properties are added to DirectoryModel or DirectoryController; gutter computation is pure QML logic.

**Acceptance Criteria:**
- The gutter number is computed in QML using bindings and property calculations (e.g., `text: (index === root.controller.cursorRow) ? (index + 1).toString() : Math.abs(index - root.controller.cursorRow).toString()`).
- No new role (e.g., LineNumberRole) is added to DirectoryModel.
- No C++ singleton, function, or property is created for line-number computation.
- All gutter cells are `HnLabel` components with bindings to `root.controller.cursorRow` and `model.index`.

---

#### REQ-C-004: No Hardcoded Row Counts or Magic Numbers

**EARS:** Constraint

The gutter width computation and number formatting shall not use hardcoded assumptions (e.g., "assume max 999 rows"). The digits count must be dynamically computed from the ListView's row count, including placeholder rows.

**Acceptance Criteria:**
- A helper function (QML or property) computes `digits(n) = Math.max(3, String(n).length)` (string length, not floating-point `log10`).
- The reference label text is generated as `"9".repeat(digits(rowCount))` where `rowCount` is the ListView's `count` property.
- The gutter width adapts when row count crosses a power of 10 (e.g., from 999 to 1000).
- A unit test asserts correct digits for counts 0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 99999.

---

#### REQ-C-005: Integration with Existing Column Alignment

**EARS:** Constraint

The gutter shall integrate with the existing DirectoryListing column-alignment pattern without modifying how iconColumnWidth, sizeColumnWidth, or modifiedColumnWidth are computed. The gutter width is independent and additive (shifting existing columns to the right by its width).

**Acceptance Criteria:**
- The gutter is the first column; all existing columns (icon, Name, Size, Modified) remain in the same relative order, shifted right by `lineNumberGutterWidth + columnSpacing`.
- The iconColumnWidth property is not recalculated or resized.
- The sizeColumnWidth and modifiedColumnWidth properties are not changed.
- The `showSize` and `showModified` threshold expressions are byte-for-byte unchanged (non-goal 8).
- The columnSpacing between gutter and icon (or between icon and Name if no icon) is exactly `columnSpacing`.
- Code review confirms no change to existing column width computation or header-row column structure except the added spacer.

---

## Summary

This specification formalizes the addition of a line-number gutter to the HoloNight Files directory listing, displaying Vim hybrid numbering (absolute at cursor row in `accentViolet`, relative elsewhere in `textMuted`) using the design system's monospace font. The gutter is the first column, sits flush at the row's left edge (replacing leading columnPadding), and has a dynamically measured width (at least 3 digits of '9's plus 16 pixels for padding, growing with row count). The gutter updates reactively on cursor movement, sorting, filtering, and directory changes, including placeholder rows created via `o`/`O` insert. The header row includes a matching blank spacer column (no label, no separator) to maintain column alignment within 1 pixel. The inline editor's left edge is positioned after the gutter to keep numbers visible during editing. The gutter is marked as accessibility-ignored and renders sharply at fractional display scales (verified natively on 1.5x Hyprland, not via screenshots). Implementation uses only Qt6 QML and the holonight-qt design system; no new DirectoryModel roles or C++ backend logic are added. Eight explicit non-goals (no toggle/setting, no absolute/relative modes, no mouse interaction, no resize, no count-prefix changes, no sidebar/preview changes, no VISUAL-mode changes, no narrow-window column-hiding changes) are excluded from scope. Cursor-move gutter rebinding keeps `directory_performance_test` within its existing budgets.

---

## Fixtures and Test Data

### Required Test Cases

- **Gutter Numbering:** Unit test in `tests/smoke.cpp` covering cursor row display (absolute, accentViolet), non-cursor rows (relative, textMuted), and cursor movement updates.
- **Width Measurement:** Digit-count test for row counts 0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 99999; one end-to-end 999→1000 entry crossing asserting the width grows.
- **Placeholder Row:** Smoke test creating an `o` placeholder row, confirming it is numbered, committing it, and asserting neighbours renumber.
- **Breadcrumb Alignment:** Smoke test asserting the breadcrumb label's left edge equals the list view's left edge within 1 pixel.
- **Column Alignment:** Smoke test measuring header and delegate column offsets for window widths 420, 800, 1200, 1600 pixels; all within 1 pixel.
- **Fractional-Scale Rendering:** Manual native test on Hyprland 1.5x display; visual inspection of sharpness (no screenshot comparisons).
- **Performance:** Existing `directory_performance_test` passes with its budgets unchanged.

---

## Testing and Acceptance

| Requirement | Verifiable As |
|---|---|
| REQ-F-001 to REQ-F-005 | Smoke test: gutter cell structure, width property, padding; code review of RowLayout arrangement |
| REQ-F-006 to REQ-F-008 | Smoke test: cursorRow binding updates; filter/sort/insert test; performance test confirms <16 ms updates |
| REQ-F-009 to REQ-F-010 | Smoke test: header spacer width and column offset measurement |
| REQ-F-011 to REQ-F-015 | Smoke test: inline editor x position; placeholder row numbering; empty/error directory header presence; breadcrumb vs list view x |
| REQ-NF-001 to REQ-NF-004 | Smoke test: width measurement; `directory_performance_test` budgets; native 1.5x display test; smoke test: Accessible.ignored |
| REQ-C-001 to REQ-C-005 | Code review: design-system colour/font bindings, shared property pattern, QML-only logic, digit computation, column order preservation |
