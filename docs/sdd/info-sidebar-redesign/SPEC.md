# SDD Spec — info-sidebar-redesign

## Overview

Status: Implemented with review remediation; manual acceptance pending.
See [verification evidence](VERIFICATION.md) for completed checks and limitations.

At short window heights, vertical scrolling shall keep all populated metadata and
EXIF rows reachable at the 220 px minimum pane width (REQ-NF-002/005). This does
not reintroduce the removed text-content preview.

This specification describes a redesign of the right-hand information sidebar (`PreviewPane.qml`) to match the project mockup (`docs/mockups/moc1.png`). The sidebar is restructured from a single-column layout with text previews to a two-column metadata/EXIF table layout, presenting file metadata in a more compact, scanning-friendly format.

Key changes:
- **Layout restructure:** from generic metadata + image + text preview, to thumbnail + filename + type + metadata table + EXIF table
- **New EXIF fields:** lens model and aperture (f-number), added to the backend `ExifReader::ExifSummary` and exposed as `PreviewService` Q_PROPERTYs
- **New type property:** human-readable MIME type description via `QMimeType::comment()`, not the raw MIME string
- **Text preview removal from sidebar UI only:** the Flickable/TextEdit preview block and truncation notice are removed from `PreviewPane.qml`, but the backend (`text_preview_service.{h,cpp}`, `PreviewService` text properties, and `QuickLookOverlay.qml`) remain unchanged to preserve `QuickLookOverlay`'s text preview capability
- **Permissions removal from sidebar:** the `permissions` Q_PROPERTY remains in C++ (asserted by existing tests) but is no longer rendered in the UI
- **Empty row hiding:** rows with empty values are hidden entirely; the entire EXIF section (header + separator) hides when there is no EXIF data

The redesign preserves all existing functionality for `QuickLookOverlay.qml`, which continues to render text previews unchanged, and preserves the file-watcher integration test's ability to verify live refresh via content changes.

---

## Functional Requirements

### Layout and Presentation

#### REQ-F-001: Thumbnail Display
**Ubiquitous:** The system shall display a thumbnail image at the top of the preview pane, spanning the full panel width with rounded corners. The frame height shall follow the source image's aspect ratio, capped at 240 pixels. When no image is available, the system shall fall back (in order): (1) the themed file-type icon from the directory listing's IconNameRole, (2) a bundled fallback SVG (folder-fallback.svg or generic-file-fallback.svg), (3) a "?" placeholder. All three tiers shall be preserved from the existing implementation.

**Acceptance Criterion:** A test image displays a visible thumbnail within the preview pane; frame dimensions do not exceed 240px height; frame aspect ratio matches the image's aspect ratio; a text file shows the fallback icon instead; a missing icon file shows the bundled SVG; a missing SVG shows the "?" placeholder; no crashes occur if any tier is unavailable.

#### REQ-F-002: Filename Display
**Ubiquitous:** The system shall display the selected entry's filename (basename) below the thumbnail in bold, primary text color. Text shall elide in the middle (ElideMiddle) if it exceeds the available panel width.

**Acceptance Criterion:** A file with a short name displays fully; a file with a name longer than 50 characters shows "start...end" elision; bold font weight is applied; primary text color is used.

#### REQ-F-003: Human-Readable Type Description
**Ubiquitous:** The system shall display the entry's human-readable type description in muted color, derived from `QMimeType::comment()` (e.g., "JPEG image", "Plain text file"). This property shall be sourced from the new `PreviewService` property `mimeTypeDescription`.

**Acceptance Criterion:** A JPEG file displays "JPEG image" or equivalent (locale-dependent, see H3); a text file displays "Plain text" or equivalent; an entry with no MIME type comment shows nothing (the row is not rendered); the displayed string is from `QMimeType::comment()`, not the raw MIME string (e.g., not "image/jpeg").

#### REQ-F-004: Metadata Table (Two-Column Layout)
**Ubiquitous:** The system shall display a two-column metadata table after the type description, with rows for `Size`, `Dimensions`, and `Modified`. The left column (labels) shall be muted in color, left-aligned, and auto-sized to the widest translated label across both tables; the right column (values) shall be primary color and left-aligned with a common left edge across all values.

**Acceptance Criterion:** A fixture file displays the three rows; labels are left-aligned in their shared column, sized to the widest translated label across both tables; values are left-aligned in their column and share a common vertical edge; label column width adapts to the longest label without truncation; both tables use the existing compact spacing token between columns; wrapped rows remain top-aligned and values wrap to multiple lines on narrow panels with vertical scrolling (REQ-NF-005).

#### REQ-F-005: Size Row Formatting
**Conditional:** For a regular file, the Size row shall display the file size in the mockup's compact form — one decimal place and a `B`/`KB`/`MB`/`GB`/`TB` suffix, e.g. `8.3 MB` — with NO parenthesized exact byte count. For a directory, the Size row shall display the literal text `Dir`.

Note: the directory listing currently renders an EMPTY size cell for directories (`DirectoryListing.qml:195` and `:309`), not `Dir`. `Dir` is taken from the mockup, and this requirement does NOT change the listing's behaviour — the two surfaces are permitted to differ here.

**Acceptance Criterion:** A 5 MiB file displays `5.0 MB` and the rendered string contains no `(` character; a 999-byte file displays `999 B`; a zero-byte file displays `0 B`; a directory displays `Dir`; the same file selected in the listing and in the sidebar shows byte-identical size text (REQ-F-030).

#### REQ-F-006: Dimensions Row Formatting
**Conditional:** Where the entry is an image file, the Dimensions row shall display the source image's pixel dimensions from the existing `sourcePixelSize` property, formatted as "WIDTH × HEIGHT" (note: multiplication sign U+00D7, not letter x), derived from the source metadata rather than the decoded image dimensions (see H1).

**Acceptance Criterion:** An image with source dimensions 6000×4000 displays "6000 × 4000"; the multiplication sign is U+00D7 (verified by Unicode inspection or character code check); non-image files show no Dimensions row (empty row hides per REQ-F-008).

#### REQ-F-007: Modified Row Formatting
**Ubiquitous:** The system shall display the entry's last-modified timestamp in human-readable locale-aware format (via `QDateTime::toLocaleString()` with `Locale.ShortFormat`), falling back to ISO 8601 format if locale formatting is unavailable.

**Acceptance Criterion:** A file modified on 2026-09-16 displays in the system locale (e.g., "9/16/26 14:30" in en-US locale, "16/09/2026 14:30" in en-GB locale); if locale formatting fails, displays "2026-09-16T14:30:00Z" or similar ISO format; invalid/missing timestamps display "Unavailable".

#### REQ-F-008: Empty Row Hiding
**State-driven:** While any metadata row (Size, Dimensions, Modified) has an empty or unavailable value, the system shall not display that row at all. The row is completely omitted from the layout (no space reserved, no "—" placeholder, no crossed-out text).

**Acceptance Criterion:** A text file (non-image) shows Size and Modified rows but no Dimensions row; a file with missing modified time shows Size and Dimensions rows but no Modified row; row spacing does not reserve hidden rows.

#### REQ-F-009: Separator After Metadata
**Ubiquitous:** After the metadata table, the system shall display a horizontal separator (HnSeparator component) to visually separate metadata from the EXIF section below.

**Acceptance Criterion:** A separator line is visible between the Modified row and the EXIF header; the separator spans the full width of the pane; it uses the standard HnSeparator styling.

#### REQ-F-010: EXIF Section Header
**State-driven:** While EXIF metadata is present for the selected entry (i.e., `exifPresent == true`), the system shall display an EXIF section header with the text "EXIF" in bold, primary text color, positioned after the separator.

**Acceptance Criterion:** A JPEG with EXIF data displays "EXIF" in bold; a JPEG without EXIF data shows no "EXIF" header; a text file shows no "EXIF" header; the header text is in primary color.

#### REQ-F-011: EXIF Metadata Table
**State-driven:** While EXIF metadata is present, the system shall display a two-column EXIF table with the same column geometry as the metadata table (left column muted, left-aligned labels sharing the widest translated label width, right column primary color values with common left edge). The EXIF table shall include the following rows (in order): `Camera`, `Lens`, `Aperture`, `Exposure`, `ISO`, `Focal length`.

**Acceptance Criterion:** A JPEG with complete EXIF data displays all six rows; rows with empty EXIF fields are hidden entirely (REQ-F-012); the two columns are aligned identically to the metadata table above; the "Camera" row combines camera make and model (REQ-F-013); the "Lens" row displays the lens model (D2); the "Aperture" row displays the f-number (D2); the "Exposure" row displays exposure time; the "ISO" row displays ISO speed; the "Focal length" row displays focal length.

#### REQ-F-012: EXIF Empty Row Hiding
**State-driven:** While an EXIF field is absent or empty, the system shall not display that row. If ALL EXIF rows are empty (i.e., all six fields are unavailable), the entire EXIF section (header + separator + table) shall be hidden, freeing the space for other content.

**Acceptance Criterion:** A JPEG with only Camera and Exposure EXIF data displays two rows (Camera, Exposure); a JPEG with no EXIF data shows no EXIF section at all; a JPEG with an empty Lens field does not reserve space for a blank Lens row.

#### REQ-F-013: Camera Row Formatting
**Conditional:** Where EXIF Camera data is available (make and/or model), the Camera row shall display the make and model separated by a space (e.g., "Canon Canon EOS 5D Mark IV"). If only make or only model is available, display that one field. If both are empty, hide the row per REQ-F-012.

**Acceptance Criterion:** A camera with make="Canon" and model="EOS 5D Mark IV" displays "Canon EOS 5D Mark IV"; make-only displays just the make; model-only displays just the model; both empty displays no row.

#### REQ-F-014: Lens Row Formatting
**Conditional:** Where EXIF lens model data is available (EXIF_TAG_LENS_MODEL), the Lens row shall display the lens model string (e.g., "Canon EF 50mm f/1.8"). If empty, hide the row per REQ-F-012.

**Acceptance Criterion:** A lens model "Canon EF 50mm f/1.8" displays fully; empty lens model hides the row; the lens field is extracted from the correct EXIF tag and is a new addition to the spec (D2).

#### REQ-F-015: Aperture Row Formatting
**Conditional:** Where EXIF aperture data is available (EXIF_TAG_FNUMBER), the Aperture row shall display the f-number in the format "f/X.X" (e.g., "f/8.0"). If empty, hide the row per REQ-F-012.

**Acceptance Criterion:** An aperture value of 8.0 displays "f/8.0"; 2.8 displays "f/2.8"; 1.4 displays "f/1.4"; empty aperture hides the row; the aperture field is extracted from the correct EXIF tag and is a new addition to the spec (D2).

#### REQ-F-016: Exposure Row Formatting
**Conditional:** Where EXIF exposure time data is available, the Exposure row shall display the time in human-readable format (e.g., "1/250s", "0.5s", "2s"). If empty, hide the row per REQ-F-012.

**Acceptance Criterion:** An exposure of 1/250s displays "1/250s" or similar; 0.5s displays "0.5s"; empty exposure hides the row.

#### REQ-F-017: ISO Row Formatting
**Conditional:** Where EXIF ISO speed data is available, the ISO row shall display the ISO value (e.g., "400", "3200"). If empty, hide the row per REQ-F-012.

**Acceptance Criterion:** An ISO of 400 displays "400"; 3200 displays "3200"; empty ISO hides the row.

#### REQ-F-018: Focal Length Row Formatting
**Conditional:** Where EXIF focal length data is available, the Focal length row shall display the focal length in human-readable format (e.g., "50mm", "35mm", "24-70mm"). If empty, hide the row per REQ-F-012.

**Acceptance Criterion:** A focal length of "50mm" displays "50mm"; "24-70mm" displays "24-70mm"; empty focal length hides the row.

#### REQ-F-019: Word Wrapping in Metadata and EXIF Tables
**State-driven:** While any value in the metadata or EXIF table exceeds the panel's available right-column width, the system shall wrap the text to additional lines within that cell (Text.WordWrap). Nothing in the metadata or EXIF tables shall be elided or truncated.

**Acceptance Criterion:** A modified timestamp that is very long in the user's locale wraps to a second line within the value cell; a long lens model wraps without truncation; at minimum panel width (220 px per H5, plus margins), all values remain legible with wrapping; the filename (in the header) still uses ElideMiddle and does not wrap.

#### REQ-F-020: Empty Selection / No Entry Placeholder
**State-driven:** While `hasEntry == false` (no file is selected, or the directory is empty), the system shall display the existing HnEmptyState placeholder with title "No selection" and description "Select a file or folder to see its details here."

**Acceptance Criterion:** An empty directory shows the placeholder instead of blank space; after deleting the last file in a directory, the placeholder appears; cursor moving back to an entry clears the placeholder and shows the preview.

#### REQ-F-021: Loading State Display
**State-driven:** While `busy == true` (a decode or EXIF extraction is in progress), the system shall display a "Loading…" notice below the metadata/EXIF, maintaining responsive UI and all visible metadata/EXIF rows so far.

**Acceptance Criterion:** Navigating to a large image shows "Loading…" while the thumbnail is being decoded; the metadata rows are visible even while decoding; once decoding completes, the "Loading…" notice disappears.

#### REQ-F-022: Error State Display
**State-driven:** While `previewErrorKind != PreviewService.None` (a decode failure, permission error, or timeout occurred), the system shall display a visible error notice (e.g., "Cannot decode image", "Permission denied") in a warning color below the metadata/EXIF, using the text from `previewErrorMessage`.

**Acceptance Criterion:** A corrupt JPEG displays "Cannot decode image" in a warning/accent color; a permission-denied file displays "Permission denied"; a timeout displays a timeout message; generic metadata is still visible alongside the error.

#### REQ-F-030: Single Shared Size Formatter
**Ubiquitous:** The size-formatting logic shall exist in exactly one place, shared by `DirectoryListing.qml` and `PreviewPane.qml`, so that the same file can never render a different size string in the listing and in the sidebar. `PreviewPane.qml`'s current local `formatSize()` (which emits the divergent `"%1 %2 (%3 bytes)"` KiB/MiB form) shall be deleted rather than edited in place.

Note: if the shared helper is extracted into a `.js` file, `scripts/check-qml-format.sh` will NOT cover it — that script iterates a hardcoded list of seven `.qml` paths and formats none of the existing `.js` files (`IconFallbacks.js` is already unchecked). See REQ-C-002.

**Acceptance Criterion:** `grep -c 'function formatSize' apps/files/*.qml` returns a total of 0 or 1 across all QML files, not 2; the string `bytes)` appears nowhere in `apps/files/PreviewPane.qml`; a test selects a fixture file and asserts the sidebar's Size text equals the listing's metadata text for that same row, character for character.

### Backend Data Model

#### REQ-F-023: New EXIF Fields in ExifReader::ExifSummary
**Ubiquitous:** The `ExifReader::ExifSummary` struct (in `exif_reader.h`) shall be extended with two new fields: `QString lensModel` and `QString aperture`. The `ExifReader::read()` function shall extract these fields from the appropriate EXIF tags (EXIF_TAG_LENS_MODEL and EXIF_TAG_FNUMBER, respectively), with graceful omission if the tags are absent or corrupted.

**Acceptance Criterion:** Code review confirms `lensModel` and `aperture` fields are added to the struct; the struct layout is unchanged for existing fields (backward-compatible view); EXIF extraction routine attempts to read the new tags; missing tags leave the fields empty (no error).

#### REQ-F-024: New PreviewService Property for Type Description
**Ubiquitous:** The `PreviewService` class shall expose a new Q_PROPERTY `QString mimeTypeDescription()` that returns the human-readable MIME type description derived from `QMimeType::comment()`. This property shall be notified alongside existing `mimeType` changes.

**Acceptance Criterion:** Code review confirms `mimeTypeDescription` Q_PROPERTY is defined; when a JPEG is selected, the property returns "JPEG image" or equivalent (locale-dependent); when a text file is selected, it returns "Plain text file" or equivalent; the property is readable from QML binding.

#### REQ-F-025: New PreviewService Properties for New EXIF Fields
**Ubiquitous:** The `PreviewService` class shall expose two new Q_PROPERTYs: `QString exifLensModel()` and `QString exifAperture()`, corresponding to the new fields in `ExifReader::ExifSummary`. Both properties shall be notified on `changed()` signal.

**Acceptance Criterion:** Code review confirms `exifLensModel` and `exifAperture` Q_PROPERTYs are defined and readable from QML; when EXIF data with lens model is decoded, the property returns the lens model string; when aperture is present, it returns the f-number; both remain empty strings if the EXIF tags are absent.

#### REQ-F-026: Text Preview Backend Retention (Non-Regression D9)
**Ubiquitous:** The text preview backend (`text_preview_service.{h,cpp}` and the PreviewService properties `hasText`, `textContent`, `textTruncated`, `textTotalSize`) shall remain fully functional and unchanged. These properties shall continue to be notified and usable by any consumer (such as `QuickLookOverlay.qml`).

**Acceptance Criterion:** Code review confirms text preview backend files are unchanged; `QuickLookOverlay.qml` still renders text previews in full (Flickable + TextEdit); no deletion or disabling of text properties occurs; tests that verify `QuickLookOverlay` text functionality continue to pass.

#### REQ-F-027: Permissions Q_PROPERTY Retention (Non-Regression D5)
**Ubiquitous:** The `PreviewService` property `permissions()` (existing `QString permissions()` accessor) shall remain in the C++ class, continue to be extracted and cached, and shall continue to emit `changed()` notifications. The property is NOT removed from the class, only not rendered in `PreviewPane.qml` UI.

**Acceptance Criterion:** Code review confirms the `permissions` Q_PROPERTY and accessor remain in `preview_service.h`; existing unit tests that assert `permissions` behavior continue to pass; the property can still be queried programmatically, it is simply not visible in the sidebar UI.

### File-Watcher Test Non-Regression

#### REQ-F-028: Watcher Integration Test Re-Expression (Non-Regression D10)
**Ubiquitous:** The integration test `PreviewIntegration.SelectedFileEditsPermissionsReplacementRenameAndDeletionRefreshInPlace` (in `tests/preview_integration_test.cpp`) shall continue to verify all four live-refresh paths it covers today — in-place edit, permission change, atomic `rename()` replacement, and watch reattachment — with its three content-change assertions re-expressed against `size()` in place of `textContent()`. The test shall not be deleted, skipped, or reduced in the number of refresh paths it exercises.

**Acceptance Criterion:** `grep -c textContent tests/preview_integration_test.cpp` returns 0 for that test body; the test still contains four `qWaitFor` assertions (edit, permissions, atomic replacement, reattachment) plus the rename assertion on `name()`; the three payloads written are 23, 18 and 22 bytes so `size()` distinguishes them unambiguously; deliberately breaking the watcher (e.g. commenting out the reattach-after-rename call in `preview_service.cpp`) makes the test FAIL — confirming it still has teeth rather than passing vacuously.

### No Removal of PreviewPane.qml Features

#### REQ-F-029: Text Preview Sidebar Removal Only
**Ubiquitous:** `PreviewPane.qml` shall remove the Flickable/TextEdit text preview block and the "Showing the first part of a larger file" truncation notice. `QuickLookOverlay.qml` shall retain its own Flickable/TextEdit text preview block unchanged.

**Acceptance Criterion:** `grep -c 'TextEdit' apps/files/PreviewPane.qml` returns 0; `git diff --stat apps/files/QuickLookOverlay.qml` shows no changes; at runtime, selecting a `.txt` file shows metadata only in the sidebar, and pressing `Space` on that same file opens Quick Look with its full text content rendered.

---

## Non-Functional Requirements

#### REQ-NF-001: Aspect Ratio Derived from Source Metadata (Binding Loop Prevention H1)
**Ubiquitous:** The thumbnail frame height (and thus aspect ratio) shall be derived from the `sourcePixelSize` property (decoded image metadata) rather than the actual rendered image dimensions. The requested-size report (`reportImageAreaSize()`) shall be driven by panel WIDTH only, not height, to prevent a feedback cycle: image → frame height → requested size → new decode → image.

**Acceptance Criterion:** The code path for thumbnail frame sizing is reviewed; frame height is calculated from `sourcePixelSize.height / sourcePixelSize.width * frameWidth`, not from the QImage dimensions; `reportImageAreaSize()` is invoked from the frame's `onWidthChanged` and from `Screen.onDevicePixelRatioChanged` ONLY, never from `onHeightChanged`; the height passed to `setRequestedSize()` is computed from `sourcePixelSize`'s aspect ratio multiplied by the frame width, never read back from the frame's own `height`; running the app with `QML_IMPORT_TRACE`/binding-loop warnings enabled and arrowing through a directory of mixed portrait and landscape images produces zero `QML Binding loop detected` warnings on stderr.

#### REQ-NF-002: Column Layout Minimum Width Legibility
**State-driven:** While the preview pane is resized to its minimum width (220 px per H5), the system shall ensure all metadata and EXIF values remain legible via text wrapping, without elision or truncation (excepting the filename, which uses ElideMiddle per design).

**Acceptance Criterion:** The pane is resized to 220 px; all metadata/EXIF rows are inspected; values that exceed column width wrap to multiple lines and remain fully readable; no horizontal scrolling is required; the layout does not overflow or hide content.

#### REQ-NF-003: EXIF Completeness Not Guaranteed (Hazard H2)
**State-driven:** While real-world image files are being previewed, the system shall handle files where EXIF lens model, aperture, and other fields are frequently absent (particularly in files with MakerNote-stored lens data). Missing fields shall be handled gracefully per REQ-F-012 (empty rows hide), with no error or warning.

**Acceptance Criterion:** A test folder of diverse real-world JPEG files (cameras from different manufacturers) is previewed; some files show 2–3 EXIF rows (incomplete), others show all six; no crashes or error messages appear; rows are hidden cleanly when data is absent.

#### REQ-NF-004: QMimeType::comment() Locale Dependency (Hazard H3)
**State-driven:** While the system is running in a non-English locale, `QMimeType::comment()` shall return a translated type description (e.g., "Image JPEG" in French may be "Image JPEG"). Acceptance criteria asserting a specific literal string (e.g., "JPEG image") SHALL PIN THE LOCALE to en_US or check for non-emptiness instead.

**Acceptance Criterion:** The type description is tested in en_US locale and expected string is asserted (e.g., "JPEG image"); the test runs in a localized environment (e.g., fr_FR) and the property is non-empty (not asserting a specific French string, which is fragile); no crash occurs if the translation is missing.

#### REQ-NF-005: Panel Resizability and Minimum Width
**State-driven:** While the preview pane is a user-resizable SplitView pane (with a 220 px minimum width, per existing code), the system shall maintain legible layout at that minimum, with no content overflow, no hidden rows, and values wrapping as needed per REQ-NF-002.

**Acceptance Criterion:** The pane is manually dragged to its minimum width; layout is fully legible; rows are visible and not scrolled out of view; values wrap cleanly; no "scroll horizontally to see more" behavior is needed.

---

## Constraints

#### REQ-C-001: EXIF Backend Extension (Hazard H2, Decision D2)
**Constraint:** The EXIF extraction backend shall gracefully handle frequently-absent lens and aperture fields (per H2). The `ExifReader::read()` function shall attempt to extract EXIF_TAG_LENS_MODEL and EXIF_TAG_FNUMBER; missing tags shall leave the corresponding fields empty, never triggering an error or aborting EXIF extraction.

**Acceptance Criterion:** Code review of `exif_reader.cpp` confirms lens and aperture extraction are wrapped in error-checking code; a test JPEG with no lens tag is decoded; `exifLensModel` remains empty; the function returns successfully with `exifPresent == true` (partial EXIF data is valid).

#### REQ-C-002: QML Format Checking Registration (Hazard H4)
**Constraint:** If this stage introduces a new `.qml` file (e.g., a redesigned `PreviewPane.qml` or a separate component), that file MUST be manually registered in `Taskfile.yml` and `scripts/check-qml-format.sh` or it will silently escape format checking.

**Acceptance Criterion:** A new QML file (if introduced) is listed in Taskfile.yml under the `qml-format` target and in `check-qml-format.sh`; running `task qml-format-check` includes the file in format validation.

#### REQ-C-003: Visual Acceptance on Fractional Scaling (Hazard H5)
**Constraint:** Visual acceptance testing must be performed on the real display with the user's configured fractional scaling (1.5x per H5). Offscreen or integer-scale screenshots miss device-pixel-ratio rendering bugs.

**Acceptance Criterion:** Visual inspection of the redesigned sidebar occurs on the physical Hyprland display at 1.5x scaling; screenshot is captured at native resolution (not downscaled); layout, text rendering, and thumbnail scaling are verified to be crisp, not blurry.

#### REQ-C-004: No Directory Child-Counting
**Constraint:** For a directory, the Size row shall display `Dir` (literal string), NOT an aggregate child count or total child size (e.g. "4 items", "1.2 GB"). Counting children is out of scope and shall not be implemented.

**Acceptance Criterion:** A directory is selected and previewed; the Size row displays `Dir`; no "N items" or aggregate byte figure is shown; selecting a directory containing 500 entries returns from `PreviewService` with no directory-enumeration syscall attributable to the size row (verifiable by the absence of any child-iteration code path in review, and by preview latency for that directory matching an empty directory's within noise).

#### REQ-C-005: No MakerNote Parsing
**Constraint:** EXIF extraction shall use only standard EXIF tags, read from IFD0 (Make, Model) and the Exif sub-IFD (`EXIF_IFD_EXIF`: ExposureTime, FNumber, ISO, FocalLength, LensModel). Vendor-specific MakerNote data shall NOT be parsed or displayed. If lens information is stored only in MakerNote (common on some cameras), that field shall remain empty.

**Acceptance Criterion:** Code review of EXIF extraction confirms no MakerNote handling; a test image with lens info only in MakerNote (and no standard EXIF lens tag) shows empty Lens row; no new library dependencies for MakerNote parsing are introduced.

#### REQ-C-006: Quick Look Text Preview Layout Unchanged (Decision D9)
**Constraint:** The `QuickLookOverlay.qml` component shall have NO CHANGES to its text preview rendering. The Flickable/TextEdit text preview block and all text-related functionality remain unchanged, guaranteeing text preview continues to work in Quick Look.

**Acceptance Criterion:** `QuickLookOverlay.qml` is inspected; the Flickable text preview section is unchanged from Stage 2 baseline; text files open in Quick Look and display full text content; no modifications to text display logic occur.

#### REQ-C-007: Integration with Existing Preview Service
**Constraint:** All new properties and backend changes shall integrate with the existing `PreviewService` class and its async worker pattern (SPEC.md REQ-C-004 from Stage 2). No new threading, queuing, or service classes shall be introduced for the new fields.

**Acceptance Criterion:** Code review confirms `exifLensModel`, `exifAperture`, and `mimeTypeDescription` are added as Q_PROPERTYs to the existing `PreviewService` class; all properties are notified via the existing `changed()` signal; no new threads or services are created.

---

## Non-Goals

The following features are **explicitly out of scope** for this redesign:

- **Directory child-counting:** No "N items", "X files", or aggregate size for directories. Display "Dir" only.
- **MakerNote parsing:** No vendor-specific EXIF data; only standard EXIF tags.
- **Partial EXIF handling as error:** Missing EXIF fields are graceful omissions, not errors. Partial EXIF data (some fields present) is valid and successful.
- **Text preview in sidebar:** The Flickable/TextEdit text preview block is removed from `PreviewPane.qml` (only). The backend and Quick Look preview remain.
- **Thumbnail cache tier changes:** No new "large" or custom tiers beyond the existing "normal" 128px tier.
- **GPS coordinate display:** GPS EXIF tags (if present) shall NOT be extracted or displayed.
- **Settings persistence for sidebar width:** Sidebar resizing is session-only; no cross-restart persistence in v1.
- **Multi-entry preview:** The sidebar previews the single cursor entry only; no multi-selection or aggregate preview in this stage.

---

## Fixtures and Test Data

This repository does **not** check in binary image fixtures. `tests/preview_fixtures.h` synthesizes
every preview fixture at runtime into a `QTemporaryDir`, and builds EXIF blobs with libexif's own
writer (`buildSampleExifBlob()`), splicing them into JPEG via an APP1 segment (`spliceJpegExif()`)
or into PNG via an `eXIf` chunk (`splicePngExifChunk()`). `tests/fixtures/` holds only `dark.toml`
and `light.toml`. No fixture work in this cycle may introduce checked-in image files, `exiftool`,
ImageMagick, or downloaded sample images.

### Reused as-is from `tests/preview_fixtures.h`
- `writeJpegWithExif()` — JPEG carrying Make/Model (IFD0) and ExposureTime/FocalLength/ISO (Exif sub-IFD).
- `writeJpegWithoutExif()` — verifies the EXIF section and its separator hide entirely (REQ-F-010, REQ-F-012).
- `writePngWithExif()` — verifies the PNG `eXIf` path still resolves the new fields.
- `writeCorruptJpeg()` — drives the error state (REQ-F-022).
- `writeSmallText()` — verifies no Dimensions row, no sidebar text preview, and that Quick Look still renders text (REQ-F-008, REQ-F-029).
- `writeGifSample()` / `writeSvgSample()` / `writeBmpSample()` / `writeTiffSample()` / `writeWebpSample()` — non-EXIF formats.
- `writeBrokenSymlink()` — unreadable-entry handling.

### Extensions required by this cycle
- **`buildSampleExifBlob()` gains two tags**, keeping every existing tag unchanged so current
  assertions continue to pass:
  - `detail::setAsciiEntry(data, EXIF_IFD_EXIF, EXIF_TAG_LENS_MODEL, "HN 24-70mm F2.8")`
  - `detail::setRationalEntry(data, EXIF_IFD_EXIF, EXIF_TAG_FNUMBER, ExifRational{8, 1}, order)` → renders as `f/8.0`
- **A partial-EXIF builder** (e.g. `buildSampleExifBlobWithoutLens()`) omitting LensModel and FNumber,
  proving the Lens and Aperture rows hide independently of the rest (REQ-F-012, REQ-NF-003).
- **A long-lens-model variant** whose LensModel exceeds the value column at the 220 px minimum pane
  width, exercising word wrap (REQ-F-019, REQ-NF-002).
- **A long-filename case** — created inline via `dir.filePath(QString(120, 'a') + ".jpg")`, no new builder needed (REQ-F-002).
- **A directory case** — any `QTemporaryDir` subdirectory, for the `Dir` size row (REQ-F-005, REQ-C-004).

### Locale pinning
Per REQ-NF-004, any assertion on a literal `QMimeType::comment()` string must pin the locale for the
duration of the test (e.g. `QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates))`),
or assert non-emptiness instead. The same applies to REQ-F-007's `Locale.ShortFormat` timestamp.


## Acceptance Criteria Summary

| Requirement | Acceptance Gate | Verifiable In |
|-------------|-----------------|---------------|
| REQ-F-001 | Thumbnail displays, fallback chain works | Manual UI inspection, fixture: jpeg_with_exif.jpg, text file |
| REQ-F-002 | Filename bold, ElideMiddle on overflow | Fixture: long filename file |
| REQ-F-003 | Type description displayed, locale-aware | JPEG displays "JPEG image" (en_US), non-empty (locale-dependent) |
| REQ-F-004 | Two-column metadata table, aligned | `Files.PreviewSidebarTablesShareLeftAlignedColumns`; rendered comparison at 320/220 px |
| REQ-F-005 | Size row: compact `8.3 MB` form for files (no byte count), `Dir` for directories | Fixture: 5 MiB file, directory fixture |
| REQ-F-030 | One shared size formatter; listing and sidebar agree character-for-character | Cross-surface string equality test |
| REQ-F-006 | Dimensions "WIDTH × HEIGHT" for images, U+00D7 character | Image fixture: 6000×4000 displays "6000 × 4000" |
| REQ-F-007 | Modified timestamp locale-aware | Locale test: en_US and en_GB show different formats |
| REQ-F-008 | Empty rows hide | Text file (no Dimensions row), image without Modified time |
| REQ-F-009 | Separator visible after metadata | Manual UI inspection |
| REQ-F-010 | "EXIF" header visible when exifPresent == true | JPEG with EXIF, JPEG without EXIF, text file |
| REQ-F-011 | EXIF table with six rows, two-column aligned | Fixture: jpeg_with_lens_aperture.jpg |
| REQ-F-012 | EXIF empty rows hide, entire section hides if all empty | Fixture: jpeg_partial_exif.jpg, jpeg_no_exif.jpg |
| REQ-F-013 | Camera row "make model", individual fields, empty hides | EXIF extraction test |
| REQ-F-014 | Lens row displays lens model or hides | Fixture: jpeg_with_lens_aperture.jpg, jpeg_partial_exif.jpg |
| REQ-F-015 | Aperture row displays "f/X.X" or hides | EXIF extraction test: aperture 8.0 → "f/8.0" |
| REQ-F-016 | Exposure row formatted correctly | EXIF extraction test |
| REQ-F-017 | ISO row formatted correctly | EXIF extraction test |
| REQ-F-018 | Focal length row formatted correctly | EXIF extraction test |
| REQ-F-019 | Values wrap at narrow widths, no elision in tables | Panel resized to 220 px, fixture: jpeg_long_lens.jpg |
| REQ-F-020 | Empty selection shows placeholder | Empty directory, delete all files, no selection |
| REQ-F-021 | "Loading…" displayed while busy | Large image, observe during decode |
| REQ-F-022 | Error notice displayed on failure | Corrupt JPEG, permission denied file |
| REQ-F-023 | ExifSummary extended with lensModel, aperture | Code review of exif_reader.h |
| REQ-F-024 | mimeTypeDescription Q_PROPERTY exists, returns QMimeType::comment() | QML binding test, JPEG → "JPEG image" |
| REQ-F-025 | exifLensModel, exifAperture Q_PROPERTYs exist, readable from QML | QML binding test |
| REQ-F-026 | Text preview backend unchanged, QuickLookOverlay still renders text | QuickLookOverlay test: text file in Quick Look |
| REQ-F-027 | permissions Q_PROPERTY remains, not removed | Code review of preview_service.h, existing unit tests pass |
| REQ-F-028 | Watcher test re-expressed against size() | Test file inspection: SelectedFileEditsPermissionsReplacementRenameAndDeletionRefreshInPlace uses preview.size() |
| REQ-F-029 | PreviewPane text preview removed, QuickLookOverlay text preview unchanged | Code inspection: PreviewPane.qml has no Flickable text, QuickLookOverlay.qml unchanged |
| REQ-NF-001 | Frame height from sourcePixelSize, no binding loop | Code review: height = sourcePixelSize.height / width * frameWidth; reportImageAreaSize() width-only |
| REQ-NF-002 | Legible at 220 px minimum width | Panel manually resized to 220 px, all rows visible and readable |
| REQ-NF-003 | Partial EXIF handled gracefully | Test folder of diverse camera JPEGs, rows hide for missing fields |
| REQ-NF-004 | Locale dependency acknowledged | Test in en_US (specific string), test in fr_FR (non-empty check) |
| REQ-NF-005 | Resizable pane, 220 px minimum, legible layout | Manual dragging to minimum, layout preserved |
| REQ-C-001 | Lens/aperture extraction graceful on missing tags | Test JPEG with partial EXIF |
| REQ-C-002 | New QML files registered in Taskfile.yml and check-qml-format.sh | If new file introduced, task qml-format-check includes it |
| REQ-C-003 | Visual acceptance on 1.5x fractional scaling | Screenshot on real Hyprland display at 1.5x |
| REQ-C-004 | No directory child-counting | Directory preview shows "Dir", not "N items" |
| REQ-C-005 | No MakerNote parsing | Code review: EXIF tags only, no MakerNote code |
| REQ-C-006 | QuickLookOverlay text preview unchanged | QuickLookOverlay.qml inspection: Flickable/TextEdit unchanged |
| REQ-C-007 | New properties in existing PreviewService, one signal | Code review: exifLensModel, exifAperture, mimeTypeDescription as Q_PROPERTYs |

---

## Implementation Notes

- **Preview Pane Restructure:** Refactor `PreviewPane.qml` to remove the text preview Flickable and truncation notice, and add the two-column metadata and EXIF table layout. The thumbnail, filename, and type display are retained and restyled to match the mockup.

- **EXIF Backend:** Extend `ExifReader::ExifSummary` to include `lensModel` and `aperture` fields. Update `ExifReader::read()` to extract EXIF_TAG_LENS_MODEL and EXIF_TAG_FNUMBER with graceful missing-field handling.

- **PreviewService Extensions:** Add three new Q_PROPERTYs to `PreviewService`:
  - `QString mimeTypeDescription()` — derived from `QMimeType::comment()` in `setTarget()`
  - `QString exifLensModel()` — from the extended `ExifSummary`
  - `QString exifAperture()` — from the extended `ExifSummary` (rendered as "f/X.X")

- **QML Layout:** Use a `ColumnLayout` with `HnLabel` components for structured rows. Left-align all labels in a column of fixed width (auto-sized to widest label). Use `Text.WordWrap` for right-column values. Insert an `HnSeparator` between metadata and EXIF sections.

- **Backward Compatibility:** The `permissions` property remains in C++ but is not rendered in the UI. Text preview backend remains fully functional and unused by the sidebar but available to `QuickLookOverlay.qml`. Existing tests continue to pass.

- **Hazard Mitigation:**
  - **H1 (binding loop):** Aspect ratio from `sourcePixelSize`, width-only resize reporting.
  - **H2 (incomplete EXIF):** Empty rows hide, partial EXIF is valid.
  - **H3 (locale):** Test locale-aware with pinned en_US assertion or non-emptiness check.
  - **H4 (QML format check):** Register new QML file in build scripts if introduced.
  - **H5 (fractional scaling):** Visual acceptance on real 1.5x display.

---

## Non-Regression Verification

- **Text Preview in Quick Look:** The `QuickLookOverlay.qml` test suite must continue to pass, verifying that text files open in Quick Look and display full content.
- **File Watcher Test:** The `PreviewIntegration.SelectedFileEditsPermissionsReplacementRenameAndDeletionRefreshInPlace` test must be re-expressed against `preview.size()` and continue to verify live file refresh correctness.
- **Permissions Property:** Existing unit tests asserting `PreviewService::permissions()` behavior must continue to pass without modification to the C++ implementation.
