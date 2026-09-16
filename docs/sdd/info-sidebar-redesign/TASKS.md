# SDD Tasks — info-sidebar-redesign

- [x] T-001: Extend EXIF test fixtures in `tests/preview_fixtures.h`
  - REQs: REQ-F-014, REQ-F-015, REQ-F-019, REQ-NF-002, REQ-NF-003
  - Check: `buildSampleExifBlob()` additionally writes `EXIF_TAG_LENS_MODEL` "HN 24-70mm F2.8" and `EXIF_TAG_FNUMBER` `ExifRational{8, 1}` with every existing tag unchanged, and new builders exist for (a) no lens/no FNumber, (b) lens+FNumber only, (c) a caller-chosen FNumber rational, and (d) a long lens model wider than the value column at 220 px; the existing `files-smoke` suite still passes after this change alone.

- [x] T-002: Extract lens model and aperture in `ExifReader`, with tests
  - REQs: REQ-F-023, REQ-F-014, REQ-F-015, REQ-C-001, REQ-C-005, REQ-NF-003
  - Check: `ExifSummary` gains `lensModel`/`aperture` after the existing fields, `parseExifBlob()` reads them from `EXIF_IFD_EXIF` via `entryValue()` and the raw-rational `rationalEntryValue()`/`formatAperture()` helpers (DESIGN §4.3) with the emptiness gate extended, no MakerNote code is added, and `exif_reader_test.cpp` passes with: the extended `ExtractsAllFieldsFromAJpegWithExif` ("HN 24-70mm F2.8", "f/8.0"), `PartialExifWithOnlyLensAndApertureIsStillPresent`, `MissingLensAndApertureLeaveThoseFieldsEmptyWithoutFailingTheRead`, `ApertureFormatsNonIntegerFStopsToOneDecimalPlace` ({28,10}→"f/2.8", {63,10}→"f/6.3"), and `ZeroDenominatorApertureIsTreatedAsAbsent`.

- [x] T-003: Add `mimeTypeDescription`, `exifLensModel`, `exifAperture` to `PreviewService`, with tests
  - REQs: REQ-F-003, REQ-F-024, REQ-F-025, REQ-C-007, REQ-NF-004
  - Check: the three `Q_PROPERTY`s are `NOTIFY changed`, `mime_type_description_` is filled only from the worker's existing `QMimeType::comment()` (empty for directories per DESIGN conflict item 3) and cleared in `resetDisplayState()`, no new thread/service is added, and `preview_service_test.cpp` passes with: an empty description for a directory target, a non-empty description after a JPEG settles, an en_US-pinned (`qScopeGuard`-restored) exact "JPEG image" assertion, a fr_FR-default run asserting non-emptiness only, lens/aperture surfacing from `writeJpegWithExif()`, and both lens/aperture clearing after retargeting to a text file.

- [x] T-004: Extract the shared `SizeFormat.js` formatter
  - REQs: REQ-F-030, REQ-C-002
  - Check: `apps/files/SizeFormat.js` (`.pragma library`) holds `formatSize()` moved verbatim from `DirectoryListing.qml`, which now imports it at both call sites with no local copy; `SizeFormat.js` is listed next to `IconFallbacks.js` in `apps/files/CMakeLists.txt` `QML_FILES`; `Taskfile.yml`/`check-qml-format.sh` are untouched (no new `.qml`, DESIGN §9); and the app builds with directory-listing size text unchanged in the existing smoke tests.

- [x] T-005: Rounded-corner `radius` property on `PreviewImageItem`
  - REQs: REQ-F-001
  - Check: `PreviewImageItem` exposes `Q_PROPERTY(qreal radius ... NOTIFY radiusChanged)` whose setter no-ops on a fuzzy-equal value, and `paint()` enables `Antialiasing` and clips to `addRoundedRect(destination, radius_, radius_)` only when `radius_ > 0` (no `layer.enabled`, no clipping `Rectangle`), building cleanly under `task build` and `task tidy`.

- [x] T-006: Aspect-ratio thumbnail frame without a binding loop
  - REQs: REQ-F-001, REQ-NF-001
  - Check: in `PreviewPane.qml`, `imageArea.aspectRatio` comes from `preview.sourcePixelSize` (square fallback), `frameHeight = Math.min(240, Math.round(width * aspectRatio))` drives `Layout.preferredHeight`, `onHeightChanged` is gone, `reportImageAreaSize()` sends `imageArea.frameHeight` (never `imageArea.height`), the image item binds `radius` to `HnAppearance.roundedRadius(HnSurfaceRole.Card, width, height)`, all three icon-fallback tiers are untouched, and `Files.InspectionImageSplitterAndPixelSizing` passes unmodified apart from the `formatSize` block removed in T-012.

- [x] T-007: Header block — filename, type row, and removal of the old caption and text preview
  - REQs: REQ-F-002, REQ-F-003, REQ-F-027, REQ-F-029, REQ-F-030
  - Check: `PreviewPane.qml` keeps the bold `ElideMiddle` filename, adds a muted `mimeTypeDescription` label hidden when empty, and no longer contains the `"%1  ·  %2  ·  %3"` caption, the raw-mimeType caption, the permissions display, the local `formatSize()`, the `Flickable`/`TextEdit` text block, or the truncation notice — so `grep -c 'TextEdit\|bytes)\|function formatSize' apps/files/PreviewPane.qml` is 0 while `permissions` stays in `preview_service.h`.

- [x] T-008: Metadata table (Size, Dimensions, Modified)
  - REQs: REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-019, REQ-C-004
  - Check: an `objectName: "previewMetadataTable"` `GridLayout { columns: 2 }` has label/value pairs that hide together: `sizeText` shows "Dir" for `inode/directory`, otherwise `SizeFormat.formatSize(size)` (value label `objectName: "previewSizeValue"`); `dimensionsText` uses U+00D7; `modifiedText` hides the row when `formatModified()` yields "Unavailable" (DESIGN conflict item 1); labels are muted, left-aligned and sized to the offstage `labelColumnWidth`; values use `Text.WordWrap` with no elision; and no child-enumeration code path is added for directories.

- [x] T-009: EXIF section (separator, header, six-row table)
  - REQs: REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019
  - Check: `HnSeparator`, a bold "EXIF" label and an `objectName: "previewExifTable"` `GridLayout` are all gated by `preview.exifPresent` (DESIGN conflict item 2); the table has Camera (`[make, model].filter(...).join(" ")`), Lens, Aperture, Exposure, ISO and Focal length rows in that order, each hiding when empty, and shares `labelColumnWidth`, `columnSpacing` and `rowSpacing` with the metadata table; and the old per-field `"Camera: %1 %2"` EXIF lines are gone.

- [x] T-010: Loading, error and empty-state placement
  - REQs: REQ-F-020, REQ-F-021, REQ-F-022
  - Check: the "Loading…" (`busy`) and warning-coloured `previewErrorMessage` labels sit below the metadata/EXIF tables with unchanged visibility bindings, the `HnEmptyState` "No selection" placeholder is unchanged for `hasEntry == false`, and an existing or new smoke assertion shows metadata rows still visible alongside the error notice for `writeCorruptJpeg()`.

- [ ] T-011: Re-express the file-watcher integration test against `size()`
  - Status: implementation and normal test pass verified; mutation/reattachment teeth check has no recorded evidence and remains pending.
  - REQs: REQ-F-028
  - Check: `SelectedFileEditsPermissionsReplacementRenameAndDeletionRefreshInPlace` contains no `textContent` and waits on `size() == 23`, `18` and `22` with its permission, rename (`name()`) and deletion assertions intact, and it passes — but fails when the reattach-after-rename call in `preview_service.cpp` is temporarily commented out (manual teeth check, reverted afterwards).

- [x] T-012: Smoke tests — shared size string and removed `formatSize` call
  - REQs: REQ-F-005, REQ-F-030
  - Check: `tests/smoke.cpp` no longer invokes `formatSize` on the pane, and a new test asserts that `sizeColumnField.text == previewSizeValue.text` character-for-character for the same fixture row, that the sidebar text contains no `(`, and that a selected directory shows exactly "Dir".

- [x] T-013: Smoke tests — row hiding, wrapping, and binding-loop warnings
  - REQs: REQ-F-001, REQ-F-002, REQ-F-008, REQ-F-012, REQ-F-019, REQ-NF-001, REQ-NF-002, REQ-NF-005
  - Check: new smoke assertions show that (a) `previewMetadataTable.implicitHeight` is smaller for a `.txt` fixture than for an image and `previewExifTable` is invisible for `writeJpegWithoutExif()` while only the Lens and Aperture rows hide for the without-lens fixture; (b) at 220 px pane width the long-lens fixture's value label has `lineCount > 1`, is not elided, and lies inside the pane; (c) a 120-char filename is middle-elided; and (d) a `qInstallMessageHandler` capture shows zero "Binding loop detected" messages while stepping the cursor through portrait and landscape images, with frame height ≤ 240 and matching each image's aspect ratio.

- [x] T-014: Regression gate
  - Evidence: final `task check` exited 0; see VERIFICATION.md and `build/sidebar-fix-check-final.log`.
  - REQs: REQ-F-026, REQ-F-027, REQ-F-029, REQ-C-002, REQ-C-006
  - Check: `git diff --stat main` shows no changes to `QuickLookOverlay.qml`, `text_preview_service.{h,cpp}` or the text/permissions `Q_PROPERTY`s, and `task build`, `task test`, `task format-check`, `task tidy` and `task qml-lint` all pass with the existing Quick Look text-preview and permissions tests unmodified.

- [ ] T-015: Native 1.5× Hyprland visual acceptance (manual)
  - REQs: REQ-C-003, REQ-NF-005, REQ-F-001
  - Check: on the real Hyprland display at 1.5×, the user confirms crisp antialiased thumbnail corners, aligned label/value columns across both tables, a single square→aspect reflow per image, all three icon-fallback tiers, legible wrapping when dragged to the 220 px minimum, and Space still opening full text in Quick Look for a `.txt` file.

- [ ] T-016: Real-world camera JPEG acceptance (manual)
  - REQs: REQ-NF-003, REQ-C-005
  - Check: previewing a folder of real JPEGs from at least two manufacturers (including one with MakerNote-only lens data) shows only the populated EXIF rows, an empty Lens row hidden rather than blank, and no crashes, warnings or error notices.

- [x] T-017: Keep wrapped metadata reachable in short windows
  - REQs: REQ-NF-002, REQ-NF-005
  - Check: at 1000×500 window size and 220 px pane width, the long-lens fixture overflows a clipped vertical viewport and scrolling reaches the complete final EXIF row. Switching to a short text entry restores reachable content. Existing sidebar geometry tests still pass.

- [x] T-018: Record verification evidence and align completion status
  - Check: VERIFICATION.md records automated commands and results; native-display, real-camera and mutation checks remain pending unless evidence is available. README and backlog link to the record.

- [x] T-019: Match mockup table alignment (REQ-F-004/011/019, REQ-NF-005)
  - Check: shared left edges for labels and values across both tables, full and partial EXIF at default and 220 px widths, top-aligned wrapped rows, visual comparison, scrolling regression and `task check`.
  - Scope: alignment only; preserve existing typography, colors, thumbnail sizing and responsive column widths; no public API changes.
  - Evidence: geometry and scrolling tests passed; all four captures visually compared with the mockup; full `task check` exited 0. See VERIFICATION.md.
