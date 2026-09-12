# File-Type Icons in Directory Listing and Preview Pane

## Context

HoloNight Files displays a directory listing (file names, sizes, timestamps) in a column-aligned layout with a preview pane showing metadata and optional thumbnail images for the selected entry. The mockup in `docs/mockups/moc1.png` includes an icon glyph before each file name in the listing (folder icon for directories, generic file icon for files, or specific MIME-type icons), and shows the same icon in the preview pane as a fallback when no thumbnail image is available.

This specification formalizes the addition of file-type icons to the directory listing and preview pane, using the standard freedesktop icon theme mechanism via `QIcon::fromTheme`. Icons are resolved through a custom `QQuickImageProvider` registered under the scheme `image://icon/`, rendered via `HnIcon` (which preserves full color), and backed by a fallback pair of bundled SVG glyphs (folder and generic file) to guarantee every row and preview shows an icon even on machines with no icon themes installed.

The feature operates within the scope of Stage 1 directory listing and Stage 2 preview pane; it does not interact with multi-selection (Stage 3), custom mime type configuration, or applications/launchers.

---

## Explicit Non-Goals

The following capabilities are explicitly OUT OF SCOPE for this specification:

1. **Places/Devices/Network Sidebar Icons** — The sidebar components (`PlacesPanel.qml`) are unchanged; sidebar item icons are not covered by this spec.
2. **Thumbnail Preview Images in Listing Rows** — Directory listing rows display MIME-type icons only, never thumbnail images from the preview service or cache. Thumbnails are used only by the preview pane and Quick Look overlay (Stage 2 scope).
3. **Custom MIME Type Configuration** — Users cannot define or override MIME type mappings; the system uses only `QMimeDatabase` and freedesktop standard types.
4. **Desktop File Application Icons** — Reading `.desktop` files to show their declared application icon is not implemented; only MIME-based resolution is used.
5. **QuickLookOverlay.qml Changes** — The Quick Look overlay is not modified by this spec and shows no icons; its behaviour is unchanged in every state.
6. **Thumbnail Generation or Cache Changes** — `ThumbnailService` and the freedesktop thumbnail cache are untouched; icons never populate, read, or invalidate thumbnail data.
7. **Theme Customization or Theme Search Path Modification** — The system uses whatever icon theme Qt's platform theme has already selected; Files adds no theme, no search path, and no environment inspection of its own.

---

## Requirements

### Icon Resolution Mechanism

#### REQ-F-001: Icon Name Resolution via QIcon::fromTheme

**EARS:** Ubiquitous

The system shall resolve file-type icons using Qt's standard `QIcon::fromTheme(name)` API, which implements freedesktop icon theme lookup: traversing the theme's `Inherits=` chain, locating size-matched PNG and SVG variants in theme directories, and consulting the `icon-theme.cache` file for fast indexing.

**Acceptance Criteria:**
- `QIcon::fromTheme` is the sole mechanism for icon theme lookup; no custom icon theme parser or fallback search is implemented.
- Inherited themes from the active icon theme are consulted in order (following the freedesktop spec).
- Both PNG and SVG variants in size-matched subdirectories (e.g., `48x48/mimetypes/`, `scalable/mimetypes/`) are accessible.
- Theme cache (`.cache/icon-theme.cache`) is used when present for fast lookup; theme directories without cache are still traversable.

---

#### REQ-F-002: Custom QQuickImageProvider for QML Integration

**EARS:** Ubiquitous

The system shall register a custom `QQuickImageProvider` on the QML engine under the scheme name `icon`, allowing QML Image/HnIcon sources to use `image://icon/<icon-name>` URLs. The provider shall call `QIcon::fromTheme(<icon-name>)` to resolve each icon, fetch the pixmap at the requested size, and return it to the QML engine.

**Acceptance Criteria:**
- The provider is registered on the QML engine with `QQmlEngine::addImageProvider("icon", provider)` during engine initialization.
- A QML `Image` or `HnIcon` source such as `image://icon/folder` resolves to the folder icon via `QIcon::fromTheme("folder")`.
- The provider honours the requested size it receives and returns a pixmap whose pixel dimensions match the display's device pixel ratio for the requested logical extent (see REQ-F-019); it never returns a fixed-size pixmap that QML must scale up.
- An unknown or unresolvable name returns a null result rather than an arbitrary substitute, so the fallback path in REQ-F-016 is reachable and observable.

---

#### REQ-F-003: HnIcon Rendering for Full-Color Preservation

**EARS:** Ubiquitous

The system shall render all file-type icons using the `HnIcon` QML component (from the Holonight design system) rather than plain `Image`, ensuring that `image://icon/` sources are rendered untinted, preserving the full color of the resolved icon.

**Acceptance Criteria:**
- Every icon in the directory listing and preview pane is rendered as `HnIcon { source: "image://icon/..." }` or equivalent.
- `HnIcon`'s internal logic recognizes the `image://icon/` scheme and applies no tint or color transformation to the resolved pixmap.
- Icons with embedded color (e.g., application-specific MIME icons, folder variants) display in their full original color, not a monochromatic tint.
- Code review confirms no plain `Image { source: "image://..." }` components are used for file-type icons.

---

#### REQ-F-004: Non-Use of Holonight IconThemeResolver

**EARS:** Constraint

The system shall NOT use holonight-qt's `IconThemeResolver` class or the `image://hnicons` scheme for file-type icons. `IconThemeResolver` is explicitly excluded because it is SVG-only, ignores the freedesktop `Inherits=` chain, and recolors full-color icons to a single design-system tint, none of which are acceptable for MIME-type icon preservation.

**Acceptance Criteria:**
- No `image://hnicons` URLs appear in DirectoryListing.qml or PreviewPane.qml for file-type icons.
- No `IconThemeResolver` class or method calls are added to the C++ backend for icon resolution.
- Code review confirms `image://icon/` is the sole scheme used for MIME-type icon sources.

---

### MIME Detection and Icon Name Derivation

#### REQ-F-005: MIME Detection on Worker Thread

**EARS:** Ubiquitous

The system shall perform per-entry MIME type detection on the existing DirectoryModel worker thread during directory listing, using filename and file-extension glob matching only via `QMimeDatabase::mimeTypeForFile(name, QMimeDatabase::MatchExtension)`. Content sniffing (opening and reading file bytes) is forbidden to avoid breaking responsiveness on large directories and network mounts.

**Acceptance Criteria:**
- MIME detection calls `QMimeDatabase::mimeTypeForFile()` with the `MatchExtension` flag; no call to `MatchDefault`, `MatchContent`, or other sniffing flags is made.
- Detection runs on the DirectoryModel's worker thread, not the GUI thread.
- A file named `Makefile` (no extension) is classified as `text/x-makefile` if the MIME database recognizes the name; otherwise, a generic fallback is applied.
- A file named `image.jpg` is immediately classified as `image/jpeg` without opening the file.
- For directories and special files, detection is skipped entirely (see REQ-F-006).

---

#### REQ-F-006: Icon Name Resolution from stat() Mode and MIME Type

**EARS:** Ubiquitous

The system shall derive the icon name for each directory entry from both the file's `stat()` mode and its MIME type, following a priority-ordered candidate chain. For non-regular files (directories, FIFOs, sockets, devices), the icon name is determined from mode alone. For regular files, the icon name is determined from the MIME type's own `iconName()` and `genericIconName()` plus those of any parent MIME types in the inheritance chain.

**Acceptance Criteria:**
- For directories: always resolve to `folder` first, then `inode-directory` as fallback.
- For FIFOs (S_ISFIFO): always resolve to `inode-fifo`.
- For sockets (S_ISSOCK): always resolve to `inode-socket`.
- For character devices (S_ISCHR): always resolve to `inode-chardevice`.
- For block devices (S_ISBLK): always resolve to `inode-blockdevice`.
- For a symlink whose target resolves (`stat` follows links): if the target is a directory the name is `folder`; otherwise the candidate chain is derived from the symlink's own filename, since detection is filename-based.
- For dangling symlinks or entries where stat fails: use the generic fallback chain (REQ-F-007).
- For regular files: attempt icons in order: (1) MIME type's own `iconName()`; (2) MIME type's own `genericIconName()`; (3) for each parent MIME type in order, its `iconName()` then `genericIconName()`; (4) finally `application-x-generic`.
- Example: a `.py` file (`text/x-python`) yields the ordered candidates `text-x-python`, `text-x-generic`, `text-plain`, `application-x-generic`. Duplicate names are emitted at most once, in first-occurrence order.
- A unit test asserts the exact candidate list, in order, for each of: a directory, a FIFO, a socket, a character device, a block device, a `.py` file, a `.jpg` file, an extensionless `README`, and a file with an unregistered `.xyz123` extension.

---

#### REQ-F-007: Generic Fallback Icon Name Derivation

**EARS:** Ubiquitous

The system shall use the generic fallback icon name when the entire candidate chain for a file resolves no available icon in the theme. Dangling symlinks, broken reads, or files with unknown MIME types shall all resolve to the generic fallback; the fallback name is `application-x-generic` for files and `folder` for directories.

**Acceptance Criteria:**
- A file with an unknown extension (e.g., `.xyz123` not in any MIME database) that produces no MIME type defaults to the generic fallback.
- A dangling symlink (symlink target does not exist) or an entry where stat fails completely defaults to the generic fallback.
- The generic fallback name is attempted via `QIcon::fromTheme`, following the same icon theme search as any other name.
- If the theme does not provide an icon for the fallback name, the bundled SVG glyph is shown (REQ-F-008).

---

### Icon Display in Directory Listing

#### REQ-F-008: Leading Icon Column in DirectoryListing

**EARS:** Ubiquitous

The system shall add a new fixed-width icon column as the first cell of each directory listing delegate's `contentItem` RowLayout, positioned before the Name column and separated by `columnSpacing`. The icon column width is a shared read-only property on the DirectoryListing root, following the established pattern of `sizeColumnWidth`, `modifiedColumnWidth`, and `columnSpacing`.

**Acceptance Criteria:**
- A new RowLayout cell is the first child of the delegate's `contentItem`, before the Name cell.
- The cell contains an `HnIcon` component displaying the resolved icon name via `image://icon/<name>`.
- The cell's width is derived from a shared `readonly property real iconColumnWidth` on the DirectoryListing root (e.g., 20 logical pixels), not a hardcoded value.
- The spacing between the icon column and Name column is exactly `columnSpacing` (existing property).
- The icon column width remains constant across all rows and does not resize with content.

---

#### REQ-F-009: Header Row Icon Column Alignment

**EARS:** Ubiquitous

The system shall indent the "Name" label of the existing column header (`objectName: "directoryColumnHeader"`) by exactly `iconColumnWidth + columnSpacing`, so that its left edge aligns with the Name cell's left edge in the delegate rows, keeping header and row columns aligned within 1 pixel.

**Acceptance Criteria:**
- The existing header row keeps its current structure, height, and separator below it; only the Name label's leading inset changes.
- The header's "Name" label has a leading inset of exactly `iconColumnWidth + columnSpacing`, read from the same shared properties the delegate uses, not restated as literals.
- Across all window widths from 420px (minimum) to 1600px+, the header "Name" label and the delegate row Name cell's left edges align within 1 pixel.
- No vertical separator or divider line appears between the icon column and the Name column in the header row (unlike the separator between Size and Modified columns, which is retained).

---

#### REQ-F-010: Icon Extent and Rendering

**EARS:** Ubiquitous

The system shall render each icon at an extent of 20 logical pixels (width and height), centered vertically within the icon column cell, and scaled to the window's device pixel ratio to maintain sharpness on high-DPI displays and fractional-scale environments (e.g., Hyprland at 1.5x).

**Acceptance Criteria:**
- Each icon's implicit width and height are set to 20 logical pixels (e.g., `implicitWidth: 20; implicitHeight: 20`).
- Icons are centered within the column cell (vertically and horizontally) using layout alignment or anchoring.
- When the window's `devicePixelRatio` changes (e.g., on display reconnection or Wayland fractional-scale update), the icon is re-rendered at the new DPI without blurriness or upscaling artifacts.
- Visual inspection on a 1.5x fractional-scale display confirms icons are sharp and not pixelated.

---

#### REQ-F-011: INSERT-Mode Placeholder Row Icon

**EARS:** Ubiquitous

The system shall give the INSERT-mode placeholder row the same icon column as every other row, resolved from the generic-file candidate chain (`application-x-generic`, then the bundled glyph).

**Acceptance Criteria:**
- The placeholder row's icon column exists and has the same width as every other row (REQ-F-008), so rows above and below it do not shift horizontally while editing.
- The placeholder row's icon name is the generic-file chain, never a folder icon, regardless of the anchor row's type.
- The inline editor (`objectName: "inlineNameEditor"`) keeps its current full-row anchoring and margins; it covers the icon column while visible, and this spec requires no change to its geometry.

---

### Icon Display in Preview Pane

#### REQ-F-012: Icon as Thumbnail Fallback in Preview Pane

**EARS:** Conditional

Where the preview pane displays an entry and no thumbnail image is available (covers folders, text files, unsupported types, broken symlinks, permission-denied, decode failures and timeouts), the system shall display the resolved icon in the thumbnail slot instead.

**Acceptance Criteria:**
- When a folder is selected and no thumbnail image exists, the folder icon is shown in the preview pane's thumbnail area.
- When a text file is selected, the icon is shown above the text preview (REQ-F-014).
- When an image file's preview decode fails or times out (Stage 2 spec), the icon is shown instead.
- When an unsupported file type is selected (e.g., a `.bin` file), the icon is shown in the thumbnail slot.
- When a symlink target is broken or permission-denied, the icon is shown alongside an error notice.

---

#### REQ-F-013: Icon Occupies Fixed Thumbnail Slot

**EARS:** Ubiquitous

The system shall display the icon in the preview pane's existing thumbnail slot (height constrained by `min(width, 240)` logical pixels), drawn at a fixed 128 logical pixels (width and height), centered, never upscaled to fill available space.

**Acceptance Criteria:**
- The icon is positioned in the same vertical region as thumbnail images (the "preview image area" above the metadata labels).
- The icon is 128 pixels wide and 128 pixels tall (logical pixels), fixed size.
- The icon is centered horizontally and vertically within the preview area.
- The rendered icon extent is exactly `min(128, available slot width)` logical pixels and never exceeds 128, so a narrow pane shrinks the icon and a wide pane never enlarges it.
- Icons displayed in the preview pane are rendered at the window's device pixel ratio (fractional scale included) to maintain sharpness.

---

#### REQ-F-014: Icon with Text Preview

**EARS:** Ubiquitous

The system shall display both the icon AND the existing text preview when the selected entry is a text file. The icon is shown in the thumbnail slot, followed by the text preview in its existing position below the metadata labels.

**Acceptance Criteria:**
- A text file entry displays the resolved icon (128px) in the preview's image area.
- Below the icon and metadata, the text content preview is shown, unchanged from Stage 2 spec (first 64 KB, scrollable, truncation notice if >64 KB).
- The name/size/date/metadata rows remain in their existing position between the icon and text preview; no layout shift occurs when text preview is present.
- No special icon selection occurs; the icon shown is the same icon name as displayed in the listing row for that entry.

---

#### REQ-F-015: Icon Uses Listing Row Icon Name, Not Content-Sniffed MIME

**EARS:** Ubiquitous

The system shall display the exact same icon name in the preview pane as is displayed in the directory listing row for the selected entry. The pane shall NOT perform independent content-sniffing or MIME detection to choose a different icon; the preview pane's MIME-type label text remains content-sniffed and independent, but the icon displayed is always the listing row's icon.

**Acceptance Criteria:**
- A file such as an extensionless `README` is displayed in the listing with its resolved icon (e.g., `text-x-generic` if the MIME database recognizes it by name, or `application-x-generic` fallback).
- When that same entry is selected, the preview pane shows the identical icon, not a different icon chosen by content-sniffing.
- The preview pane's displayed MIME-type label (e.g., "Text document") remains a content-sniffed value, independent of the icon choice (unchanged from Stage 2).
- Code review confirms PreviewPane.qml binds to the same icon name role from DirectoryModel, not a separate MIME detection step.

---

### Fallback Icon Glyphs

#### REQ-F-016: Bundled SVG Fallback Glyphs

**EARS:** Ubiquitous

The system shall bundle two SVG glyph files as Qt application resources: a folder glyph (for directories) and a generic-file glyph (for files). These glyphs are used ONLY when the entire theme candidate chain for an icon name resolves nothing, ensuring every row and preview displays an icon on any machine, including a bare CI container with no icon themes installed.

**Acceptance Criteria:**
- Two SVG files ship as Qt resources of the `files-ui` module, each carrying the project's REUSE licensing headers so `task license-check` stays green.
- The glyphs are line-art in the HoloNight design-system style, legible at both 20 and 128 logical pixels.
- With every icon theme made unavailable to the process, every listing row and every no-thumbnail preview still shows a visible glyph: a folder glyph for directories and the generic-file glyph for everything else.
- With a theme that does provide an icon for a name, the bundled glyph is not used for that name.
- The mechanism by which a failed theme lookup reaches the bundled glyph is left to design; this requirement fixes only the observable outcome.

---

#### REQ-F-017: Fallback Glyphs Rendered Tinted via HnIcon

**EARS:** Ubiquitous

The system shall render the bundled SVG fallback glyphs in the design system's palette colours, while theme-provided icons (REQ-F-003) remain untinted in their original colours.

Note: `HnIcon` decides tinting from the source URL — a source beginning with `image://icon/` is always passed through untinted. A bundled glyph therefore cannot be both delivered through that scheme and tinted. Design must resolve this; the requirement below fixes the outcome, not the route.

**Acceptance Criteria:**
- A displayed bundled glyph is drawn in the current palette's icon/text colour and changes with the dark and light appearance fixtures in `tests/fixtures/`.
- A displayed theme icon retains its own colours; a multi-colour theme icon is not reduced to a single colour.
- Both statements are checked against the same entry list under a theme-present run and a theme-absent run.

---

#### REQ-F-018: No Fallback Theme or Distro Heuristics

**EARS:** Constraint

The system shall NOT add a fallback icon theme (such as Adwaita, Hicolor, or others) to the search path, nor shall it use environment variables, desktop detection, or distro-specific heuristics to augment icon resolution. The bundled SVG glyphs are the sole fallback mechanism.

**Acceptance Criteria:**
- Files makes no call to `QIcon::setThemeName()`, `QIcon::setFallbackThemeName()`, or `QIcon::setThemeSearchPaths()`. Whatever the platform theme plugin already configured is used as-is.
- Files reads no environment variable, compositor name, or distribution marker to influence icon resolution. Qt's own platform-theme handling of such variables is outside Files and unaffected.
- Icon resolution consists of exactly two steps: `QIcon::fromTheme()` over the candidate chain, then the bundled glyph.

---

### Performance and Rendering

#### REQ-F-019: Device Pixel Ratio and Fractional-Scale Rendering

**EARS:** Event-driven

When the window's `devicePixelRatio` changes (e.g., on display reconnection, Wayland fractional-scale update, or programmatic DPI change), the system shall re-render all displayed icons at the new DPI without blurriness, upscaling artifacts, or visual degradation.

**Acceptance Criteria:**
- On a Wayland display with fractional scaling (e.g., 1.5x), a window created at integer DPI (e.g., 2.0) receives a `devicePixelRatioChanged` signal upon being shown; icons are re-rendered at the new DPI.
- Visual inspection on a real fractional-scale display (e.g., Hyprland at 1.5x) confirms icons are sharp and not pixelated.
- No screenshot-based tests or offscreen rendering is relied upon to verify DPI correctness; only native on-display rendering is accepted as verification.
- Toggling between integer and fractional scales does not leave icons blurry or at a stale resolution.

---

#### REQ-F-020: Icon Theme Cache and Per-Name Lookup

**EARS:** Ubiquitous

The system shall cache icon lookups keyed by (icon name, pixel size) to avoid repeated theme traversal. A bounded cache (implementation detail: size limit is not specified) prevents unbounded memory growth while ensuring repeated lookups in steady-state browsing (e.g., a 10k-entry directory with only a few dozen distinct icons) are served from cache.

**Acceptance Criteria:**
- Icon lookups for the same name and size are served from cache on subsequent requests without re-scanning the theme directory.
- The cache is implemented in the `QQuickImageProvider` or as a wrapper around `QIcon::fromTheme`.
- A directory with 10k files but only 50 distinct icon names results in ~50 theme lookups per size, not 10k lookups.
- Memory profiling confirms cache memory usage is bounded and does not grow unbounded as more icons are displayed.

---

#### REQ-F-021: No Performance Regression on Large Directories

**EARS:** Ubiquitous

The system shall display icons in a 10k+ entry directory with no measurable regression in scrolling speed, frame rate, or keyboard responsiveness compared to the current baseline (before icons are added). Per-row icon retrieval must be a cache hit in steady-state (cached icons are fast).

**Acceptance Criteria:**
- The existing 10k-entry fixture is listed and traversed end to end (`gg`, `G`, repeated paging) with icons enabled, and the run's wall-clock time stays within 10% of the same run recorded immediately beforehand on the pre-icon build.
- The existing `files-memory-benchmark` test stays within its current threshold with icons enabled.
- Distinct theme lookups during a full traversal of a 10k-entry directory number at most the count of distinct icon names in that directory, proving the cache is consulted rather than the theme re-walked per row.

---

#### REQ-F-022: Failed or Missing Icon Handling

**EARS:** Unwanted Behaviour

If an icon lookup fails (the theme resolves no match and the bundled SVG is missing or corrupt), the system shall not blank a row, leave a gap, or stall delegate rendering. A visible fallback glyph or placeholder is shown instead.

**Acceptance Criteria:**
- A corrupt or unreadable theme icon file does not crash the application, and the affected row still shows its filename, size, and date.
- The icon cell keeps its fixed width in every failure case, so a failed lookup never shifts the Name, Size, or Modified columns of that row relative to its neighbours.
- Scrolling and keyboard navigation remain interactive while icon lookups are failing.
- Icon failures raise no dialog and log at most one message per distinct icon name, never one per row.

---

### Existing Behavior Preservation

#### REQ-F-023: Keyboard Navigation and Search Highlighting Unchanged

**EARS:** Ubiquitous

The system shall make no change to existing keyboard navigation behavior, search-match per-character highlighting in the Name cell, or other interactive features. The addition of the icon column shall not affect keybinding dispatch, cursor movement, or search filtering.

**Acceptance Criteria:**
- Keyboard navigation (`j`, `k`, page-up, page-down, `G`, etc.) moves the cursor through rows as before; the icon column is not interactive and does not intercept keypresses.
- Search/filter highlighting (matching characters in the Name cell) remains unchanged; the icon column is not highlighted.
- Filter/search results are not affected by the icon; only the Name cell text is matched.
- Jumping to the name cell via existing keybindings (if any) is unaffected.

---

#### REQ-F-024: Inline INSERT-Mode Editing Unchanged

**EARS:** Ubiquitous

The system shall make no change to inline INSERT-mode editing behavior. The inline editor's text input and overlay shall work as before; the icon column is present but not editable or interactive during inline editing.

**Acceptance Criteria:**
- Entering INSERT mode and typing to create a new file or edit an existing file name works unchanged.
- The inline editor keeps its existing full-row overlay geometry; no anchor, margin, or width change is made to accommodate the icon column.
- Cancelling or completing the inline edit works as before; no icon-related state interferes.
- Code review confirms no new properties or signal connections are added to the inline editor for icon handling.

---

#### REQ-F-025: Stat-Error Warning Indicator Unchanged

**EARS:** Ubiquitous

The system shall preserve the existing stat-error warning indicator (if present in the Name cell or row) and shall not remove or reposition it due to the addition of the icon column.

**Acceptance Criteria:**
- Files with stat errors (e.g., permission denied, broken symlinks) display the existing warning indicator (e.g., a ⚠ symbol or visual mark) in the row, unchanged in position or appearance.
- The icon column and Name cell layout do not reduce the Name cell's width such that the stat-error indicator is cut off or hidden.
- Width subtraction for the warning indicator (existing logic in the Name cell) remains in place and functional.

---

#### REQ-F-026: Column Alignment and Header Stability Unchanged

**EARS:** Ubiquitous

The system shall maintain the existing column alignment between header labels and delegate row cells (within 1 pixel tolerance). The header row's Size and Modified column separators (if present) shall remain unchanged; no new separators are added between the icon column and Name column.

**Acceptance Criteria:**
- The Size and Modified column headers remain visually separated by a vertical line or other separator, unchanged from before icons are added.
- No vertical separator appears between the icon column and Name column in the header row.
- Columns remain aligned when the window is resized from 420px to 1600px+ width.

---

## Non-Functional Requirements

#### REQ-NF-001: Theme Resolution on GUI Thread

**EARS:** Ubiquitous

Theme resolution (via `QIcon::fromTheme` and the `QQuickImageProvider` pixmap fetch) shall run on the GUI thread, as `QIconLoader` is not documented as thread-safe. The cached results shall permit high-frequency access without GUI thread stalls.

**Acceptance Criteria:**
- `QIcon::fromTheme` calls occur only in the `QQuickImageProvider::requestPixmap()` method or other GUI-thread-safe contexts, never in the DirectoryModel worker thread.
- MIME type detection (which feeds icon name derivation) occurs on the worker thread, but the actual theme icon lookup occurs on the GUI thread.
- GUI thread latency for icon lookups is <1ms for cache hits and <5ms for uncached lookups (amortized across many rows).

---

#### REQ-NF-002: Visual Regression Testing

**EARS:** Ubiquitous

The project's existing verification suite shall pass unchanged in intent after the icon column is added, and the offscreen appearance matrix shall be regenerated so the new column is represented.

**Acceptance Criteria:**
- `task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint`, and `task license-check` all pass.
- `scripts/check-visual.sh` runs clean and produces captures for both appearance fixtures at scales 1, 1.25, and 1.5, each showing the icon column.
- Any existing test asserting on delegate or header structure is updated deliberately, with the reason recorded, rather than loosened to accommodate the new cell.
- Sharpness at fractional scale is NOT claimed from these offscreen captures; it is verified only per REQ-F-019.

---

## Constraints

#### REQ-C-001: Icon Name Role in DirectoryModel

**EARS:** Constraint

The DirectoryModel shall expose a new role (e.g., `IconNameRole`) alongside existing roles (NameRole, PathRole, IsDirRole, SizeRole, ModifiedRole, ModeRole, IsHiddenRole, StatFailedRole, StatErrorRole). This role shall return the derived icon name (string) for each entry, computed on the worker thread during listing.

**Acceptance Criteria:**
- A new enum value `IconNameRole` is added to DirectoryModel's role enumeration.
- The value is documented in DirectoryModel header comments.
- For every entry in the model, `data(index, IconNameRole)` returns a string representing the icon name (e.g., "folder", "image-jpeg", "application-x-generic").
- Code review confirms the role is set during worker-thread listing, not lazily computed on-demand.

---

#### REQ-C-002: Minimal C++ Surface

**EARS:** Constraint

The C++ additions for icons shall be limited to the new DirectoryModel role (REQ-C-001), the icon image provider and its cache, the pure icon-name derivation helper, and the single read-only path needed to give the preview pane the selected row's icon name (REQ-F-015).

**Acceptance Criteria:**
- No icon-related mutable state is added to `DirectoryController`, `PreviewService`, or `TaskManager` beyond that read-only selected-entry icon name.
- The icon-name derivation is a free function or namespace with no QObject, no I/O beyond `QMimeDatabase`, and no dependency on the model — so it is directly unit-testable.
- No existing public signal or property changes shape.

---

#### REQ-C-003: QML Integration via Image Source URLs

**EARS:** Constraint

Icon resolution from QML shall be entirely through `image://icon/<name>` URLs bound to `HnIcon.source` properties. No C++ backend expose of icon objects, pixmaps, or theme logic to QML is used; all integration is URL-based.

**Acceptance Criteria:**
- DirectoryListing.qml and PreviewPane.qml contain QML bindings like `HnIcon { source: "image://icon/" + model.iconName }`.
- No `Q_INVOKABLE` methods, Q_PROPERTY pointers-to-objects, or QML-exposed C++ objects are used for icon handling (beyond the provider itself).
- Code review confirms QML/C++ integration is purely URL-based.

---

#### REQ-C-004: Reuse of Existing DirectoryModel Worker Pattern

**EARS:** Constraint

MIME type detection shall follow the existing DirectoryModel worker pattern: performed on the dedicated worker thread during listing, no GUI thread involvement in detection, and results exposed as a model role (REQ-C-001). No new worker thread or async pattern is introduced.

**Acceptance Criteria:**
- MIME detection uses the existing worker thread (DirectoryListing runs via QThread in DirectoryModel).
- The worker calls `QMimeDatabase::mimeTypeForFile()` for each entry alongside existing file stat and name extraction.
- Results are marshalled back to the GUI thread as model data, consistent with existing roles (Size, Modified, etc.).
- No new `QThread`, `QThreadPool`, or async mechanism is added for icon-related work.

---

#### REQ-C-005: Qt 6 and QML Component Consistency

**EARS:** Constraint

Icon display shall be implemented as QML components and Qt 6 integration, consistent with existing DirectoryListing, PreviewPane, and PlacesPanel architecture. No Qml v3, non-standard bindings, or legacy Qt 5 patterns are used.

**Acceptance Criteria:**
- The provider is a synchronous `QQuickImageProvider` subclass, not `QQuickAsyncImageProvider`, because REQ-NF-001 requires theme resolution on the GUI thread.
- QML bindings use standard property binding syntax (`HnIcon { source: ... }`).
- The new QML and C++ files are registered in `apps/files/CMakeLists.txt`, and any new `.qml` file is also added to `Taskfile.yml` and `scripts/check-qml-format.sh`, which do not discover files automatically.

---

## Summary

This specification formalizes the addition of file-type icons to the HoloNight Files directory listing and preview pane, using Qt's standard freedesktop icon theme mechanism (`QIcon::fromTheme`) via a custom `QQuickImageProvider` and `HnIcon` rendering for full-color preservation. MIME type detection runs on the DirectoryModel worker thread (extension/name-matching only, no content sniffing), producing an icon name exposed as a new DirectoryModel role. Icons are displayed as a leading column in the listing (20 logical px fixed width, with the header's Name label indented to match and no separator between the two), and in the preview pane whenever no thumbnail image is available (at most 128 logical px, centred in the existing thumbnail slot). Icon theme lookup is backed by a per-name/size cache and two bundled SVG fallback glyphs (folder and generic file) to guarantee every row displays an icon on any machine, including a bare CI container. Performance and rendering requirements keep large directories scrolling without regression, require sharpness at fractional scale to be verified on a real display rather than offscreen, and leave keyboard, search, and inline-editing behaviour unchanged. Seven explicit non-goals (sidebar icons, thumbnails in rows, custom MIME config, `.desktop` app icons, Quick Look changes, thumbnail-cache changes, theme customization) are excluded from scope. Implementation uses only Qt 6 APIs, QML components, and the existing DirectoryModel async pattern, with no new C++ backend properties beyond the icon-name role.

---

## Fixtures and Test Data

### Required Test Cases
- **MIME Detection Mapping:** Gtest in `files-smoke` test binary covering directories, FIFOs/sockets/devices, extensionless files (e.g., `Makefile`, `README`), unknown extensions, and parent-type chains (e.g., `text/x-python` → `text/plain` → generic).
- **Fallback Resolution:** Test forcing an empty/absent icon theme (simulating CI container case) and verifying bundled SVG fallback is shown.
- **Large Directory Performance:** Traverse the existing 10k-entry fixture with icons enabled; confirm the run stays within 10% of the pre-icon baseline recorded immediately beforehand (REQ-F-021).
- **Fractional-Scale Rendering:** Native test on real Wayland display at 1.5x (or 1.25x if available); visual inspection confirms icons are sharp.

---

## Testing and Acceptance

| Requirement | Verifiable As |
|-------------|---------------|
| REQ-F-001 to REQ-F-007 | Unit tests for MIME-to-icon-name derivation; code review of icon name chain |
| REQ-F-008 to REQ-F-015 | Visual regression snapshots; manual inspection of layout alignment |
| REQ-F-016 to REQ-F-018 | Unit test with empty theme; visual inspection of bundled SVGs in fallback mode |
| REQ-F-019 to REQ-F-022 | Native fractional-scale rendering test; performance benchmark; fallback handling test |
| REQ-F-023 to REQ-F-026 | Existing test suite (keyboard, search, edit, alignment) re-run and verified |
| REQ-NF-001 to REQ-NF-002 | Profiling during large-directory scroll; visual regression snapshots updated |
| REQ-C-001 to REQ-C-005 | Code review: DirectoryModel role addition, QQuickImageProvider implementation, QML bindings |
