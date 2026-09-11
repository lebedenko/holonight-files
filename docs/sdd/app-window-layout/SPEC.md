# Application Window Layout — HoloNight Files Main View Restructuring

## Context

HoloNight Files is a keyboard-driven, Vim-style modal file manager designed for efficient navigation and file operations. The current implementation (`apps/files/Main.qml`) lays out the application window with a status bar at the top, followed by a horizontal split between a sidebar and content pane, with static keybinding hints at the bottom. The UI mockup in `docs/mockups/moc1.png` defines a target layout structure that better aligns with modern file manager conventions: a header bar with breadcrumb navigation, a restructured three-section content area (places sidebar | column-aligned file listing | metadata/preview sidebar), and the mode status bar relocated to the footer.

This specification formalizes a layout restructuring to match the mockup's visual hierarchy and information architecture, within a tightly scoped boundary: pure QML layout and component repositioning using only existing controller/model properties and the Holonight design-system component library. No new C++ backend properties, no new interactive behaviors (click-to-sort, clickable breadcrumb segments, navigation history), and no silent reintroduction of non-goal features are permitted.

## Explicit Non-Goals

The following capabilities are explicitly OUT OF SCOPE for this specification:

1. **Back/forward navigation history and buttons** — Breadcrumb navigation is read-only; history-based navigation is not implemented.
2. **Clickable/interactive breadcrumb segments or path navigation** — The breadcrumb displays the current path as text only; no per-segment click handlers or navigation actions are added.
3. **Click-to-sort column headers** — The Name/Size/Modified header row contains static column labels (no button styling, no click handlers). The existing `DirectoryProxyModel.sortDescending` property is not wired to header clicks; sorting remains accessible only via existing keybindings (keyboard mode, not UI interaction).
4. **Grouped Places/Devices/Network sidebar sections** — The `PlacesModel` remains a flat list; no new section-grouping or collapsible categories are added to the sidebar.
5. **Footer item-count and selection-size summary** — The footer's `ModeStatusBar` is relocated verbatim from the top; no new badges, counters, or size summaries are added to the footer itself (though the mockup illustrates such information—see Note below).
6. **Search and menu icons in the header** — No icon-button controls (magnifying glass, hamburger menu) are added to the header bar. Search functionality is already accessible via the `/` keybinding; menu features are not expanded via UI chrome.
7. **Static keybinding-hints row fate** — The current static Row of keybinding hint labels (line 99–123 in Main.qml) is left unchanged; its status (kept, removed, or merged into the footer) is not specified by this cycle and is treated as an implementation detail.

**Note:** The mockup shows footer information (item count, selection size, file size summary) that is NOT in scope for this cycle per the requirements-gathering session. Future refinements may add such summary information via separate SPEC cycles, but treating them as non-goals here ensures no scope creep.

## Requirements

### REQ-F-001: Header Bar with Solid Background

**EARS:** Ubiquitous

The system shall add a header bar positioned at the top of the window, below any window chrome (title bar, menu bar if any) and above the content area. The header bar shall be a full-width, fixed-height surface with a solid background fill that matches the application's palette (dark mode: dark surface; light mode: light surface) and shall NOT be transparent.

**Acceptance Criteria:**
- The header bar is rendered as a full-width, opaque surface at the top of the content area.
- The background color is palette-driven (e.g., `HoloniightPalette.surfaceBase` or equivalent) and adapts to light and dark theme changes.
- The header height equals `HnMetrics.headerHeight` (the standard header height from the Holonight design system).
- No visible window edge, rounded corner, or other chrome is visible within the header bar's bounds.

---

### REQ-F-002: Breadcrumb Path Display in Header

**EARS:** Ubiquitous

The system shall display a read-only breadcrumb representation of the current directory path in the header bar, derived from `DirectoryController.currentPath` (a plain string property already accessible on the controller).

**Acceptance Criteria:**
- The breadcrumb text is read-only (no text input, no per-segment buttons or click handlers).
- The breadcrumb path is derived from and dynamically updates with `DirectoryController.currentPath`.
- The breadcrumb updates immediately when the user navigates to a different directory.

---

### REQ-F-003: Breadcrumb Left-Edge Alignment with Content

**EARS:** Constraint

The breadcrumb's left edge (the start of the text baseline) shall align horizontally with the left edge of the main file-listing column (the Name column of the DirectoryListing). The alignment must NOT be hardcoded to a fixed pixel offset or window edge; instead, it shall be achieved via a shared "sidebar width" value that is exposed once (e.g., as a property on the window, a shared constant, or a binding helper) and read by BOTH the places sidebar's width AND the header breadcrumb's left-inset, ensuring they cannot drift apart if the sidebar width is changed in future.

**Acceptance Criteria:**
- A single, shared sidebar-width value is defined (e.g., a property or constant accessible to both the places sidebar and header) and is NOT duplicated or hardcoded in multiple locations.
- The places sidebar's `Layout.preferredWidth` is set to this shared value (currently 200, but the binding must allow future changes without manual edits to multiple files).
- The header breadcrumb's left margin or inset (e.g., via `Layout.leftMargin` or `anchors.leftMargin`) is set to the same shared value plus any internal header padding.
- Across window widths from the minimum (420px) to large widths (1600px+), the breadcrumb left edge and the main listing's left edge remain aligned within 1px.
- If the sidebar width is programmatically changed (e.g., via a future config or resize mechanism), both the sidebar and breadcrumb update together without manual re-binding.

---

### REQ-F-004: Breadcrumb Text Elision and Overflow Handling

**EARS:** Ubiquitous

The breadcrumb text shall not overflow or clip destructively; narrow-window elision behavior (text truncation, ellipsis, or alternative elision strategy) is an implementation detail, but the breadcrumb must remain readable and not extend beyond the window's visible bounds.

**Acceptance Criteria:**
- The breadcrumb component includes elision logic (e.g., Qt Quick's `elide` property, or custom truncation) to handle paths that exceed the available horizontal space.
- At the minimum window width (420px), the breadcrumb remains fully visible within the header bar without horizontal scrolling or overflow artifacts.
- At wider widths, the full path is displayed when space permits; when space is constrained, the path is elided (e.g., with ellipsis) rather than clipped.

---

### REQ-F-005: Window Structure — Header, Content, Footer Layout

**EARS:** Ubiquitous

The system shall restructure the main window's overall layout to follow a top-to-bottom hierarchy: header bar → three-section content area → footer bar. The header bar and footer bar are full-width fixed-height surfaces; the content area fills the remaining vertical space.

**Acceptance Criteria:**
- The ColumnLayout hierarchy is: header (new) → content RowLayout → footer (relocated ModeStatusBar).
- The header and footer each occupy their specified fixed heights; the content area expands to fill the remaining space.
- No content elements (places sidebar, listing, preview pane) intrude into the header or footer bounds.

---

### REQ-F-006: Places Sidebar — Consistent Width and Measurement

**EARS:** Ubiquitous

The places sidebar (PlacesPanel) shall retain its existing component, behavior, and appearance, and shall be positioned in the left section of the three-section content area. Its width shall be determined by the shared sidebar-width value (see REQ-F-003) and shall not be duplicated or hardcoded.

**Acceptance Criteria:**
- The places sidebar remains the existing `PlacesPanel` component, unchanged in properties or behavior.
- The sidebar's `Layout.preferredWidth` is set to the shared sidebar-width value (e.g., binding to a named property or constant).
- The sidebar occupies the left section of the content RowLayout, vertically filling the space between the header and footer.

---

### REQ-F-007: DirectoryListing Header Row with Column Labels

**EARS:** Ubiquitous

The DirectoryListing component shall display a header row as the first element within the listing area, containing three static column labels: "Name", "Size", and "Modified". The header row shall be a non-interactive visual guide (no click handlers, no sort buttons).

**Acceptance Criteria:**
- A header row component (or equivalent visual container) is positioned above the file listing delegate rows.
- The header row contains three text labels: "Name", "Size", and "Modified" (or translated equivalents).
- The labels are positioned at the same x-coordinates as the corresponding data fields in the delegate rows (see REQ-F-008).
- The header row is styled to be visually distinguishable from delegate rows (e.g., different background, text weight, or color) but is not interactive.

---

### REQ-F-008: DirectoryListing Delegate Row Column Alignment

**EARS:** Ubiquitous

The DirectoryListing delegate rows (currently rendered as stacked card-style layouts with title/subtitle/metadata) shall be reworked to display each row's Name, Size, and Modified data as three distinct, column-aligned fields. Each field shall be positioned at the same x-coordinate in all rows, aligning vertically under the corresponding header label (Name/Size/Modified).

**Acceptance Criteria:**
- Each delegate row contains three distinct data fields: name (with directory/file icon if applicable), file size, and modified timestamp.
- The x-position of the Name field is identical across all delegate rows (within 1px tolerance).
- The x-position of the Size field is identical across all delegate rows (within 1px tolerance).
- The x-position of the Modified field is identical across all delegate rows (within 1px tolerance).
- The header row labels (REQ-F-007) share identical column x-coordinates with the delegate rows' corresponding fields.
- The current stacked card layout (with title/subtitle/metadata stacked vertically per row) is replaced with a horizontal column layout.
- All three delegate data fields (name, size, modified) are already available as model properties; no new data is fetched or computed.

---

### REQ-F-009: Preview/Info Sidebar Repositioning

**EARS:** Ubiquitous

The PreviewPane component (currently displaying EXIF and metadata information) shall remain unchanged in content and behavior and shall be positioned in the right section of the three-section content area, below the header and above the footer.

**Acceptance Criteria:**
- The PreviewPane remains the existing component, unchanged in properties or behavior.
- The preview pane is positioned in the right section of the content RowLayout, vertically filling the space between the header and footer.
- No changes to the preview pane's EXIF/metadata display or interaction are made.

---

### REQ-F-010: Footer Bar — ModeStatusBar Relocation

**EARS:** Ubiquitous

The ModeStatusBar component, currently positioned at the top of the window, shall be relocated to the bottom of the window as the footer bar. The component's content, behavior, and styling shall remain unchanged (no additions, no removals, no reordering of status elements).

**Acceptance Criteria:**
- The ModeStatusBar component is moved from the top ColumnLayout position to the bottom ColumnLayout position (i.e., after the content RowLayout).
- The component's properties (controller binding, layout properties, text content, styling) are not modified.
- The status bar displays the same information and responds to the same controller state changes as before.

---

### REQ-F-011: Content Area Layout Structure

**EARS:** Ubiquitous

The content area (between header and footer) shall be structured as a three-section horizontal layout: places sidebar (fixed width) | main file listing (flexible) | preview pane (sized to fit). A SplitView or equivalent container shall permit the user to adjust the boundary between the main listing and preview pane via dragging or other interaction, as the existing implementation supports.

**Acceptance Criteria:**
- The content area is a RowLayout or equivalent horizontal-layout container.
- Section 1: PlacesPanel on the left, with `Layout.preferredWidth` set to the shared sidebar-width value.
- Section 2: DirectoryListing in the middle, with `Layout.fillWidth: true` to expand available space.
- Section 3: PreviewPane on the right, sized via `SplitView.preferredWidth` or equivalent (current: 320px).
- The split boundary between the listing and preview pane is draggable (existing behavior preserved).

---

### REQ-F-012: Window Minimum Dimensions Unchanged

**EARS:** Constraint

The window's `minimumWidth` and `minimumHeight` shall remain at their current values (minimumWidth: 420, minimumHeight: 280) to maintain layout stability at narrow widths.

**Acceptance Criteria:**
- The window's `minimumWidth` property is not changed from 420.
- The window's `minimumHeight` property is not changed from 280.
- The layout remains functional and does not overflow at these minimum dimensions.

---

### REQ-NF-001: Delegate Visual Regression and Test Coverage

**EARS:** Non-Functional

When the DirectoryListing delegate rows are reworked (REQ-F-008), existing automated tests and visual regression snapshots must be updated to reflect the new column-aligned layout structure, and all existing tests must continue to pass.

**Acceptance Criteria:**
- Tests in `tests/directory_model_test.cpp`, `tests/directory_controller_test.cpp`, `tests/directory_controller_file_ops_test.cpp`, `tests/directory_performance_test.cpp`, and `tests/vim_mode_controller_test.cpp` (if any assert on delegate structure) are reviewed and updated if necessary.
- Visual regression snapshots in `build/review-visual/`, `build/visual/`, and `build/native-matrix/` directories are regenerated or updated to match the new delegate layout.
- All automated tests pass after the delegate rework is complete.
- The test suite verifies that the Name/Size/Modified fields are correctly column-aligned across all rows and themes (light/dark, normal/fractional scales).

---

### REQ-NF-002: Design-System Component Compliance

**EARS:** Non-Functional

The header bar and footer bar shall use components and styling from the Holonight design system (Holonight.Controls, Holonight.Core) exclusively. No custom styling or unbranded color values are permitted; all palette references shall use named tokens from `HoloniightPalette`.

**Acceptance Criteria:**
- The header bar background uses a named palette token (e.g., `HoloniightPalette.surfaceBase`, not a hardcoded hex color).
- The footer bar (relocated ModeStatusBar) does not require palette changes.
- The header height is derived from `HnMetrics.headerHeight`, not a hardcoded pixel value.
- All text and icons in the header and footer use Holonight design-system components (HnLabel, HnIcon, etc.) if applicable.

---

### REQ-C-001: Existing Controller/Model Data Only

**EARS:** Constraint

The layout restructuring shall use only existing properties and data from `DirectoryController` and `DirectoryModel`. No new C++ backend properties, no new public methods on controllers, and no new data fields are permitted.

**Acceptance Criteria:**
- The breadcrumb derives its path text from `DirectoryController.currentPath` without modification or new property additions.
- The delegate rows' Name, Size, and Modified data are sourced from existing model properties (not new properties).
- No new C++ signals, slots, or properties are required for the layout restructuring.
- Code review confirms no changes to `DirectoryController.h`, `DirectoryModel.h`, or other backend header files beyond what is necessary for the Holonight design-system integration (if any).

---

### REQ-C-002: Existing Component Behavior Preservation

**EARS:** Constraint

All existing components and their behaviors (PlacesPanel, PreviewPane, ModeStatusBar, SplitView interaction) shall remain unchanged except for their position/layout within the window hierarchy. No new interactive behavior, signals, or properties are added to any existing component.

**Acceptance Criteria:**
- PlacesPanel's list navigation, click handlers, and content remain unchanged.
- PreviewPane's EXIF display, metadata formatting, and responsive sizing remain unchanged.
- ModeStatusBar's status text, mode indicator, and controller bindings remain unchanged.
- The SplitView's drag-to-resize interaction between DirectoryListing and PreviewPane remains unchanged.

---

### REQ-C-003: Shared Sidebar Width Binding Strategy

**EARS:** Constraint

The sidebar width value (used by both PlacesPanel and header breadcrumb inset) shall be defined and exposed once in a centralized location (e.g., as a property on the window or in a shared QML constant file) and shall be referenced via QML bindings (not copy-pasted values). This ensures that any future changes to the sidebar width (e.g., via configuration or resize logic) automatically propagate to both the sidebar and header without requiring manual updates to multiple files.

**Acceptance Criteria:**
- A single definition of the sidebar width value exists (e.g., `window.sidebarWidth: 200` or `Holonight.sidebarWidth: 200`).
- The PlacesPanel's `Layout.preferredWidth` is bound to this value via `Layout.preferredWidth: window.sidebarWidth` or equivalent.
- The header breadcrumb's left-inset margin is bound to the same value (e.g., `Layout.leftMargin: window.sidebarWidth`).
- If the centralized value is changed, both the sidebar and breadcrumb update automatically.
- Code review confirms no hardcoded `200` pixel values in the breadcrumb or header layout (all sizes are derived from the shared binding).

---

### REQ-C-004: No Silent Reintroduction of Non-Goals

**EARS:** Constraint

The layout restructuring implementation shall not silently introduce or re-enable any feature listed in the Explicit Non-Goals section. Specifically:
- No clickable breadcrumb segments, back/forward buttons, or navigation history shall be added.
- No click-to-sort interaction shall be wired to the column headers.
- No new footer counters, item-count badges, or size summaries shall be added.
- No search or menu icons shall appear in the header.

**Acceptance Criteria:**
- Code review of Main.qml and all newly created/modified QML files confirms no click handlers, signal connections, or navigation logic are added to the breadcrumb or column headers.
- The footer content remains verbatim (no new elements added unless explicitly required by other spec sections).
- No new icon buttons or menu entries are added to the header bar.

---

## Summary

This specification formalizes the restructuring of the HoloNight Files main application window layout to match the visual hierarchy and structure shown in `docs/mockups/moc1.png`. The restructuring is pure layout/chrome work using only existing components and data: a new header bar with a read-only breadcrumb (whose left edge is bound to a shared sidebar-width value for future-proofing), repositioning of the three-section content area (places sidebar | column-aligned file listing with static header row | preview pane), and relocation of the mode status bar to the footer. Existing components, behaviors, and controller/model properties are unchanged except for their layout positions. Visual regression tests and automated test suites are updated to match the new delegate structure. Six explicit non-goals (back/forward navigation, clickable breadcrumb, sort headers, grouped sidebar, footer counters, header search/menu icons) are excluded from scope. Implementation of these requirements creates a layout foundation that aligns with the project's visual design while preserving all existing functionality.

## Approved review clarification

For REQ-F-007/008, narrow listing panes may hide Modified first and Size second
in both the header and delegate, preserving space for Name. Visible columns must
remain aligned. This responsive presentation is part of the requested review fixes;
it adds no interaction or backend state.
