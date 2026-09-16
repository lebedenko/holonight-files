# Info sidebar redesign — Design

Status: Implemented with review remediation; manual acceptance pending.
Spec: `docs/sdd/info-sidebar-redesign/SPEC.md` (REQ-F-001..030, REQ-NF-001..005, REQ-C-001..007)

## Spec conflicts and open interpretations

Three tensions in SPEC.md's literal wording are resolved below rather than silently designed
around; a fourth item is a previously-undocumented test breakage this design surfaces.

1. **REQ-F-007 vs. REQ-F-008 on a missing Modified timestamp.** REQ-F-007's acceptance criterion
   says invalid/missing timestamps "display 'Unavailable'"; REQ-F-008's acceptance criterion says a
   file with a missing modified time "shows... no Modified row" (omitted entirely, no placeholder
   text). These cannot both be followed literally for the same row. **Resolution:** REQ-F-008's
   "omit the row" wins for the Modified row specifically — no row in the new table ever renders a
   literal "Unavailable"/"—" placeholder (REQ-F-008's own text: "no space reserved, no '—'
   placeholder, no crossed-out text"). The existing `formatModified()` helper (`PreviewPane.qml:43-47`)
   is kept unchanged and still *computes* the string `"Unavailable"` internally — REQ-F-007's
   Ubiquitous requirement text is satisfied at the helper-function level — but §4.2 uses that
   sentinel only to decide the Modified row's `visible` binding, never to render it.
2. **REQ-F-009 vs. REQ-F-012 on whether the separator is unconditional.** REQ-F-009 is written as
   "Ubiquitous" ("After the metadata table, the system shall display a horizontal separator"),
   implying the separator always appears. REQ-F-012 explicitly enumerates "the entire EXIF section
   (header + separator + table)" as hidden together when EXIF is entirely absent. A separator with
   nothing below it (every non-EXIF-bearing file: every `.txt`, every directory) would be a visible
   dangling rule contradicting the "no dead space" spirit of REQ-F-008/012 and would fail REQ-F-012's
   own acceptance criterion ("a JPEG with no EXIF data shows no EXIF section at all"). **Resolution:**
   the separator is gated by `exifPresent`, bundled with the header and table as REQ-F-012 literally
   states; REQ-F-009's acceptance criterion ("a separator line is visible between the Modified row and
   the EXIF header") is satisfied whenever the header is shown, which is the only case that
   requirement's own example concerns.
3. **Directory "type description" is out of scope by omission, not by an explicit non-goal.**
   REQ-F-003's Ubiquitous text and every one of its acceptance-criterion examples are file-oriented
   (JPEG, plain text); nothing requires a directory to show a "Folder" type row, and REQ-C-004's
   "no directory-enumeration syscall attributable to the size row" spirit argues against adding new
   directory-specific I/O elsewhere in the pane. §5.2 documents the resulting decision: `mimeTypeDescription`
   is populated only by the async worker path (regular files), and stays empty — so the Type row hides,
   per REQ-F-003's own "no comment shows nothing" clause — for directories, FIFOs, and stat-failed
   entries. This is a scope choice, not a bug, and is called out here since SPEC.md does not decide it.
4. **`tests/smoke.cpp:649-651` breaks outright and is not mentioned anywhere in SPEC.md.**
   `TEST(Files, InspectionImageSplitterAndPixelSizing)` calls
   `QMetaObject::invokeMethod(pane, "formatSize", Q_RETURN_ARG(QString, formattedSize), Q_ARG(double, 1440054))`
   and asserts the result `contains("MiB")` — a direct, by-name invocation of the exact
   `PreviewPane.qml` function REQ-F-030 requires deleted. This is a required test-file edit, not an
   optional one; see §7 (Risks) and §8 (Test plan) for the precise fix.

---

## 1. Overview

Review remediation: wrap the complete sidebar content in a clipped, vertical-only
`Flickable` with a vertical `ScrollBar`. The content column follows the viewport
width and its own implicit height, so wrapped rows are never compressed into the
window height. The viewport retains the existing token margins. The thumbnail
continues to report sizes only on width/DPR changes. A 1000×500 window with a
220 px sidebar and the long-lens fixture must allow scrolling to the last EXIF
row; selecting a shorter entry must leave its content reachable. See
`VERIFICATION.md` for evidence and remaining manual acceptance.

`PreviewPane.qml` (`apps/files/PreviewPane.qml`, currently 237 lines) is restructured from
"metadata caption line + image + EXIF lines + text preview" into the mockup's
`thumbnail → filename → type → metadata table → separator → EXIF header → EXIF table` layout,
still reactively bound to the single `controller.preview` (`PreviewService`) instance shared with
`QuickLookOverlay.qml` — no second decode pipeline, no new async mechanism (REQ-C-007).

Three things move at the same time, because they are entangled:

- **The size formatter is deduplicated.** `DirectoryListing.qml`'s existing `formatSize()`
  (`apps/files/DirectoryListing.qml:38-51`) is relocated verbatim into a new `.pragma library` file,
  `apps/files/SizeFormat.js`, and both `DirectoryListing.qml` and `PreviewPane.qml` import it.
  `PreviewPane.qml`'s own, divergent `formatSize()` (`PreviewPane.qml:20-33`, the
  `"%1 %2 (%3 bytes)"` KiB/MiB form) is deleted outright (REQ-F-030).
- **The thumbnail frame's height stops being read back from itself.** Today
  `Layout.preferredHeight: Math.min(width, 240)` (`PreviewPane.qml:59`) is a pure function of width
  already — no cycle exists yet — but the redesign's REQ-F-001 ("frame height shall follow the
  source image's aspect ratio") requires deriving height from the image, which *would* close a
  cycle back into `setRequestedSize()` if implemented naively. §5.1 is the load-bearing section of
  this design.
- **Two new backend fields (lens, aperture) and one new derived field (MIME type description)**
  are threaded through `ExifReader::ExifSummary` and `PreviewService` on the existing single
  `changed()` signal, with no new `QThread`, no new service class (REQ-C-007).

No new `.qml` file is introduced. One new `.js` file is introduced (`SizeFormat.js`); §6.2 explains
why it deliberately is **not** added to `Taskfile.yml`/`scripts/check-qml-format.sh`.

---

## 2. Component inventory

| File | Status | Change |
|---|---|---|
| `apps/files/PreviewPane.qml` | Modified | Full body restructure per §4.2: thumbnail (rounded, aspect-capped), filename, type row, metadata `GridLayout`, `HnSeparator` + EXIF header + EXIF `GridLayout` (both gated on `exifPresent`), Loading/Error notices, empty-state. Text `Flickable`/`TextEdit` block (lines 178-197) and truncation notice (199-205) deleted (REQ-F-029). Local `formatSize()` (lines 20-33) deleted (REQ-F-030). |
| `apps/files/SizeFormat.js` | **New** | `.pragma library` holding one function, `formatSize(bytes)`, moved verbatim from `DirectoryListing.qml:38-51`. No behavior change — REQ-F-030 is a dedup, not a reformat. |
| `apps/files/DirectoryListing.qml` | Modified | Deletes its local `formatSize()` (lines 38-51); adds `import "SizeFormat.js" as SizeFormat`; the two call sites (`root.formatSize(size)` at line 195, `root.formatSize(delegate.size)` at line 309) become `SizeFormat.formatSize(...)`. No other change. |
| `apps/files/exif_reader.h` | Modified | `ExifSummary` gains `QString lensModel;` and `QString aperture;`, appended after the existing fields (REQ-F-023). |
| `apps/files/exif_reader.cpp` | Modified | `parseExifBlob()` extracts `EXIF_TAG_LENS_MODEL` (via the existing `entryValue()` helper) and `EXIF_TAG_FNUMBER` (via a new raw-rational path, §4.3, formatted `"f/X.X"`); the struct's "all empty → not present" gate is extended to include both new fields. |
| `apps/files/preview_service.h` | Modified | Three new `Q_PROPERTY`s — `mimeTypeDescription`, `exifLensModel`, `exifAperture` — all `NOTIFY changed` (REQ-C-007); one new private member `QString mime_type_description_;`. |
| `apps/files/preview_service.cpp` | Modified | `PreviewResult` gains `QString mime_type_description;`; `runPreviewJob()` sets it from the `QMimeType` object it already holds (§5.2); `resetDisplayState()` (lines 467-474) clears it; `applyResult()` (line ~439) assigns it. No change to `setTarget()`'s synchronous (dir/FIFO/stat-failed) branches — see conflict item 3 above. |
| `apps/files/preview_image_item.h` | Modified | New `Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)`. |
| `apps/files/preview_image_item.cpp` | Modified | `paint()` clips to an antialiased rounded rect matching the drawn image's destination rect (§5.3). |
| `tests/exif_reader_test.cpp` | Modified | New assertions for `lensModel`/`aperture` on the existing `ExtractsAllFieldsFromAJpegWithExif` fixture, plus a new partial-EXIF test. |
| `tests/preview_service_test.cpp` | Modified | New assertions for `exifLensModel`/`exifAperture`/`mimeTypeDescription` end to end. |
| `tests/preview_fixtures.h` | Modified | `buildSampleExifBlob()` gains two `detail::setAsciiEntry`/`detail::setRationalEntry` calls (SPEC.md's own §"Extensions required"); a new `buildSampleExifBlobWithoutLens()`; a long-lens-model variant. |
| `tests/preview_integration_test.cpp` | Modified | The three `textContent()` assertions in `SelectedFileEditsPermissionsReplacementRenameAndDeletionRefreshInPlace` (lines 128, 134, 136) become `size()` assertions (REQ-F-028). |
| `tests/smoke.cpp` | Modified (surgical) | Delete the `formatSize` `invokeMethod` block (lines 649-651, see conflict item 4 and §7/§8); add the REQ-F-030 cross-surface size-string equality assertion; add a `previewPane` structural assertion for the new metadata/EXIF `GridLayout`s. |

Explicitly **not** modified: `apps/files/QuickLookOverlay.qml` (REQ-C-006), `apps/files/text_preview_service.{h,cpp}` (REQ-F-026), `apps/files/IconFallbacks.js` (icon-fallback plumbing inside `imageArea` is untouched — only its `Layout.preferredHeight` binding changes, §4.2), `apps/files/thumbnail_service.{h,cpp}`, `apps/files/directory_controller.{h,cpp}` (no new role/argument needed — `iconName` already threads through today, `mimeTypeDescription`/`exifLensModel`/`exifAperture` are pure `PreviewService`-internal additions with no controller-level plumbing).

---

## 3. Data flow

```
Cursor moves (j/k, click, search jump, …)
  → DirectoryController::setCursorRow()/openEntry() → syncPreviewTarget()
      reads DirectoryModel roles (incl. IconNameRole, unchanged) for the new cursor row
  → PreviewService::setTarget(path, isDir, size, modified, mode, statFailed, statError, iconName, revision)
      (apps/files/preview_service.cpp:271) — synchronous, zero I/O:
        resetDisplayState()   // clears display_image_, source_pixel_size_, exif_, text_, error_,
                               // and (new) mime_type_description_
        mime_type_.clear()
        <branch: statFailed | non-regular | isDir | regular file>
        emit changed()         // metadata (name/size/modified/permissions) visible instantly
        dispatch()              // regular files only
  → [worker thread] runPreviewJob() → decodeImage()
        result.source_pixel_size = QImageReader(&file).size()      // SOURCE metadata, pre-scale
        result.mime_type = mime.name()
        result.mime_type_description = mime.comment()              // NEW, same QMimeType object
        … thumbnail-tier lookup/decode, publish(result) [final=false]
        result.exif = ExifReader::read(file, mimeType, cancel)      // now incl. lensModel/aperture
        publish(result) [final=true]
  → [UI thread, QueuedConnection] PreviewService::applyResult(result)
        mime_type_ = result.mime_type
        mime_type_description_ = result.mime_type_description        // NEW
        source_pixel_size_ = result.source_pixel_size
        exif_ = result.exif                                          // incl. lensModel/aperture
        display_image_ = result.image (if larger/first)
        emit changed()
  → QML: root.preview.* bindings re-evaluate
        imageArea.aspectRatio/frameHeight recompute from sourcePixelSize (§5.1)
        metadataTable/exifTable row `visible` bindings recompute from the *Text root properties (§4.2)
        GridLayout drops hidden rows from layout entirely (no gap, §4.2)
```

The only feedback edge that matters for REQ-NF-001 is `imageArea.width` (a resize, not a cursor
move) → `reportImageAreaSize()` → `setRequestedSize()` → worker redecode → `applyResult()` →
`source_pixel_size_` — and that edge is a no-op for `source_pixel_size_` specifically, because
`source_pixel_size` is read once per file from `QImageReader::size()` *before* any scaling
(`preview_service.cpp:125-127`) and is intrinsic to the file, not to the requested output size. A
redecode at a different requested size reproduces the *same* `source_pixel_size_` value; it cannot
introduce a new one that differs from what is already displayed. This is what makes the aspect
ratio safe to feed back into the frame's own sizing at all — see §5.1.

---

## 4. Interfaces / APIs

### 4.1 `apps/files/SizeFormat.js` (new)

```js
.pragma library

// Moved verbatim from DirectoryListing.qml's formatSize() (SPEC.md REQ-F-030) — the single shared
// size formatter for DirectoryListing.qml and PreviewPane.qml. Divides by 1024 (binary) but labels
// with SI-looking KB/MB/GB/TB suffixes, matching the mockup's "8.3 MB" form. Negative input (no
// size available) returns "", which both callers use directly as a hide-this-row signal.
function formatSize(bytes) {
    if (bytes < 0)
        return "";
    if (bytes < 1024)
        return qsTr("%1 B").arg(bytes);
    const units = ["KB", "MB", "GB", "TB"];
    let value = bytes / 1024;
    let unitIndex = 0;
    while (value >= 1024 && unitIndex < units.length - 1) {
        value /= 1024;
        unitIndex += 1;
    }
    return qsTr("%1 %2").arg(value.toFixed(1)).arg(units[unitIndex]);
}
```

`PreviewPane.qml`'s Size row additionally special-cases directories to the literal `"Dir"`
(REQ-F-005/REQ-C-004) *outside* this shared function — `formatSize()` itself never sees a
directory, exactly as it doesn't today in `DirectoryListing.qml` (which independently guards with
`delegate.isDir ? "" : SizeFormat.formatSize(delegate.size)`, unchanged).

### 4.2 `PreviewPane.qml` structure (new body, `root`-level properties)

The implemented content column lives in the vertical viewport described in §1,
with `contentHeight: content.implicitHeight` and `width: scrollArea.width` on the
column. `QtQuick.Controls` supplies the shared styled `ScrollBar`, positioned in
the outer margin so it does not cover table values. The entry key combines the
controller's current directory and preview name; changing that key resets the
scroll position without resetting it for asynchronous EXIF updates. Value cells
use `Text.Wrap` (word wrapping with a fallback for an overlong single word).

```qml
import "SizeFormat.js" as SizeFormat
// ... existing imports unchanged (IconFallbacks.js import stays; text-preview-only imports, none)

readonly property bool isDirectoryEntry: root.preview.mimeType === "inode/directory"

readonly property string sizeText: root.isDirectoryEntry ? qsTr("Dir")
    : (root.preview.size >= 0 ? SizeFormat.formatSize(root.preview.size) : "")

readonly property string dimensionsText: (root.preview.sourcePixelSize.width > 0
        && root.preview.sourcePixelSize.height > 0)
    ? qsTr("%1 × %2").arg(root.preview.sourcePixelSize.width).arg(root.preview.sourcePixelSize.height)
    : ""  // U+00D7 MULTIPLICATION SIGN, not "x" (REQ-F-006's acceptance criterion checks the code point)

readonly property string modifiedText: {
    const formatted = root.formatModified(root.preview.modified);  // unchanged helper, lines 43-47
    return formatted === qsTr("Unavailable") ? "" : formatted;      // conflict item 1: never rendered
}

readonly property string cameraText: [root.preview.exifMake, root.preview.exifModel]
    .filter(part => part.length > 0).join(" ")   // fixes today's unconditional "%1 %2" double-space bug
```

Offstage label-width metric (mirrors `DirectoryListing.qml`'s `modifiedColumnMetric`,
`apps/files/DirectoryListing.qml:31-36`), computed once from every possible label across *both*
tables so the label column never reflows between selections and both tables are pixel-identical
(REQ-F-004, REQ-F-011):

```qml
Column {
    id: labelMetrics
    visible: false
    HnLabel { role: HnTypographyRole.Caption; rawText: qsTr("Size") }
    HnLabel { role: HnTypographyRole.Caption; rawText: qsTr("Dimensions") }
    HnLabel { role: HnTypographyRole.Caption; rawText: qsTr("Modified") }
    HnLabel { role: HnTypographyRole.Caption; rawText: qsTr("Camera") }
    HnLabel { role: HnTypographyRole.Caption; rawText: qsTr("Lens") }
    HnLabel { role: HnTypographyRole.Caption; rawText: qsTr("Aperture") }
    HnLabel { role: HnTypographyRole.Caption; rawText: qsTr("Exposure") }
    HnLabel { role: HnTypographyRole.Caption; rawText: qsTr("ISO") }
    HnLabel { role: HnTypographyRole.Caption; rawText: qsTr("Focal length") }
}
readonly property real labelColumnWidth: {
    let widest = 0;
    for (const child of labelMetrics.children)
        widest = Math.max(widest, child.implicitWidth);
    return widest;
}
```

Metadata table — one row shown in full, the remaining two follow the identical shape (label cell,
value cell, both `visible` bound to the same `*Text` property so they always hide as a pair):

```qml
GridLayout {
    id: metadataTable
    Layout.fillWidth: true
    columns: 2
    columnSpacing: HnMetrics.internalSpacing(HnControlSize.Compact)
    rowSpacing: 2

    HnLabel {
        role: HnTypographyRole.Caption
        color: HoloniightPalette.textMuted
        rawText: qsTr("Size")
        horizontalAlignment: Text.AlignLeft
        Layout.preferredWidth: root.labelColumnWidth
        Layout.alignment: Qt.AlignTop | Qt.AlignLeft
        visible: root.sizeText.length > 0
    }
    HnLabel {
        role: HnTypographyRole.Caption
        rawText: root.sizeText
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        visible: root.sizeText.length > 0
    }
    // Dimensions: identical shape, rawText: root.dimensionsText, visible: root.dimensionsText.length > 0
    // Modified:   identical shape, rawText: root.modifiedText,   visible: root.modifiedText.length > 0
}

HnSeparator {
    Layout.fillWidth: true
    visible: root.preview.exifPresent   // conflict item 2: bundled with the header+table, not unconditional
}
HnLabel {
    role: HnTypographyRole.Body
    font.bold: true
    rawText: qsTr("EXIF")
    visible: root.preview.exifPresent
}
GridLayout {
    id: exifTable
    Layout.fillWidth: true
    columns: 2
    columnSpacing: metadataTable.columnSpacing   // identical geometry, REQ-F-011
    rowSpacing: metadataTable.rowSpacing
    visible: root.preview.exifPresent

    // Camera:      rawText: root.cameraText,            visible: root.cameraText.length > 0
    // Lens:        rawText: root.preview.exifLensModel,  visible: root.preview.exifLensModel.length > 0
    // Aperture:    rawText: root.preview.exifAperture,   visible: root.preview.exifAperture.length > 0
    // Exposure:    rawText: root.preview.exifExposureTime, visible: root.preview.exifExposureTime.length > 0
    // ISO:         rawText: root.preview.exifIso,        visible: root.preview.exifIso.length > 0
    // Focal length: rawText: root.preview.exifFocalLength, visible: root.preview.exifFocalLength.length > 0
    // — each a label/value pair identical in shape to the Size row above.
}
```

Type-description row (replaces the old raw-`mimeType` caption at `PreviewPane.qml:139-145`):

```qml
HnLabel {
    role: HnTypographyRole.Caption
    color: HoloniightPalette.textMuted
    rawText: root.preview.mimeTypeDescription
    visible: text.length > 0
    Layout.fillWidth: true
}
```

The old combined `"%1  ·  %2  ·  %3"` caption (size/modified/permissions, `PreviewPane.qml:131-137`)
is deleted outright — its three fields now live in the metadata table (size, modified) or are
dropped from the UI entirely (permissions, REQ-F-027).

### 4.3 `ExifReader::ExifSummary` and raw-rational aperture extraction

```cpp
// exif_reader.h
struct ExifSummary {
  bool present = false;
  QString make;
  QString model;
  QString exposureTime;
  QString iso;
  QString focalLength;
  QString lensModel;   // NEW — EXIF_TAG_LENS_MODEL, EXIF_IFD_EXIF (REQ-F-023/REQ-F-014)
  QString aperture;     // NEW — EXIF_TAG_FNUMBER, EXIF_IFD_EXIF, formatted "f/X.X" (REQ-F-015)
  bool operator==(const ExifSummary&) const = default;
};
```

```cpp
// exif_reader.cpp, anonymous namespace — new, alongside entryValue()/normalizeExposureTime()
#include <libexif/exif-utils.h>   // ExifRational, exif_get_rational
#include <optional>

// FNumber is stored as a single RATIONAL; libexif's own exif_entry_get_value() renders it through
// an internal aperture table that can diverge from a literal numerator/denominator division for
// non-standard values, so the raw components are read directly instead (mirrors how
// tests/preview_fixtures.h's detail::setRationalEntry() writes the tag, for symmetry).
std::optional<ExifRational> rationalEntryValue(ExifContent* content, ExifTag tag) {
  if (content == nullptr) {
    return std::nullopt;
  }
  auto* entry = exif_content_get_entry(content, tag);
  if (entry == nullptr || entry->format != EXIF_FORMAT_RATIONAL || entry->components != 1) {
    return std::nullopt;
  }
  const auto byteOrder = content->parent != nullptr ? exif_data_get_byte_order(content->parent) : EXIF_BYTE_ORDER_INTEL;
  return exif_get_rational(entry->data, byteOrder);
}

// "f/X.X", one decimal place (REQ-F-015). A zero (or otherwise unusable) denominator is a graceful
// omission, not an error (REQ-C-001) — matches every other per-tag failure in this file.
QString formatAperture(const std::optional<ExifRational>& value) {
  if (!value || value->denominator == 0) {
    return {};
  }
  const double fNumber = static_cast<double>(value->numerator) / static_cast<double>(value->denominator);
  return QStringLiteral("f/%1").arg(fNumber, 0, 'f', 1);
}
```

`parseExifBlob()` (exif_reader.cpp:96-123) gains:

```cpp
const auto lensModel = entryValue(data->ifd[EXIF_IFD_EXIF], EXIF_TAG_LENS_MODEL);
const auto aperture = formatAperture(rationalEntryValue(data->ifd[EXIF_IFD_EXIF], EXIF_TAG_FNUMBER));
// ... existing emptiness gate extended:
if (make.isEmpty() && model.isEmpty() && exposureTime.isEmpty() && focalLength.isEmpty() && iso.isEmpty()
    && lensModel.isEmpty() && aperture.isEmpty()) {
  return summary;
}
// ... existing assignments, plus:
summary.lensModel = lensModel;
summary.aperture = aperture;
```

Extending the emptiness gate is required, not cosmetic: without it, a JPEG carrying *only*
lens/aperture EXIF data (no make/model/exposure/iso/focal) would report `present == false` even
though it has two displayable rows, incorrectly hiding the entire EXIF section per REQ-F-012 (which
is gated on `exifPresent`).

Worked examples for §"Fixtures" of SPEC.md: `ExifRational{28, 10}` → `28.0/10.0 = 2.8` → `"f/2.8"`;
`ExifRational{63, 10}` → `"f/6.3"`; `ExifRational{8, 1}` → `"f/8.0"`; `ExifRational{n, 0}` for any
`n` → `{}` (row hides).

### 4.4 `PreviewService` additions

```cpp
// preview_service.h — new Q_PROPERTYs, alongside the existing exif*/mimeType properties (line ~52-62)
Q_PROPERTY(QString mimeTypeDescription READ mimeTypeDescription NOTIFY changed)
Q_PROPERTY(QString exifLensModel READ exifLensModel NOTIFY changed)
Q_PROPERTY(QString exifAperture READ exifAperture NOTIFY changed)

QString mimeTypeDescription() const { return mime_type_description_; }
QString exifLensModel() const { return exif_.lensModel; }
QString exifAperture() const { return exif_.aperture; }

private:
  // ... existing members
  QString mime_type_description_;   // NEW, alongside mime_type_
```

```cpp
// preview_service.cpp
struct PreviewResult {
  // ... existing fields
  QString mime_type_description;   // NEW
};

// runPreviewJob(), right after the existing `result.mime_type = mime.name();` (line 212):
result.mime_type_description = mime.comment();   // same QMimeType object already resolved on this thread

// applyResult(), alongside `mime_type_ = result.mime_type;` (line ~439):
mime_type_description_ = result.mime_type_description;

// resetDisplayState() (lines 467-474), alongside the existing exif_/source_pixel_size_ reset:
mime_type_description_.clear();
```

`exif_.lensModel`/`exif_.aperture` need no separate reset: `resetDisplayState()` already does
`exif_ = ExifReader::ExifSummary();` (line 470), and the struct's default member initializers make
both new fields empty strings automatically.

**Why `mime_type_description_` is populated only by the worker path, never by `setTarget()`'s
synchronous branches** (directories/FIFOs/stat-failed): `setTarget()`'s own doc comment states it
"formats generic metadata synchronously on the UI thread with zero I/O" (`preview_service.h:100-101`).
A `QMimeType::comment()` lookup by name (`QMimeDatabase().mimeTypeForName(...)`) is not necessarily
free on first touch — the shared-mime-info comment tables are parsed from disk on first use per
process (cached thereafter). Rather than add a UI-thread I/O path to a function whose contract
explicitly forbids one, this design accepts that directories/FIFOs/broken-symlink entries never show
a Type row (§"Spec conflicts", item 3) — a legitimate reading of REQ-F-003, whose own text and
examples are entirely file-oriented.

**Thread safety of `QMimeType::comment()` on the worker thread:** safe. `mime` is the same
`QMimeType` value already produced by `mimeDatabase.mimeTypeForFileNameAndData(path, sniff)` at
`preview_service.cpp:211`, which this exact worker function has called on this exact thread since
Stage 2 shipped. `QMimeDatabase`'s underlying provider is documented thread-safe/reentrant (a
process-wide, mutex-guarded cache); `.comment()` is a read through the same provider that
`.name()` already reads through — no new thread-safety surface is introduced.

### 4.5 `PreviewImageItem` rounded corners

```cpp
// preview_image_item.h
Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)
Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)   // NEW
...
qreal radius() const { return radius_; }
void setRadius(qreal radius);
signals:
  void imageChanged();
  void radiusChanged();   // NEW
private:
  QImage image_;
  qreal radius_ = 0;   // NEW
```

```cpp
// preview_image_item.cpp
#include <QPainterPath>   // NEW

void PreviewImageItem::setRadius(qreal radius) {
  if (qFuzzyCompare(radius_, radius)) {
    return;
  }
  radius_ = radius;
  update();
  emit radiusChanged();
}

void PreviewImageItem::paint(QPainter* painter) {
  if (image_.isNull()) {
    return;
  }
  const auto destination = fitRect(image_.size(), boundingRect().size());
  painter->setRenderHint(QPainter::Antialiasing, true);          // NEW
  painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
  if (radius_ > 0) {                                              // NEW
    QPainterPath clip;
    clip.addRoundedRect(destination, radius_, radius_);
    painter->setClipPath(clip);
  }
  painter->drawImage(destination, image_);
}
```

QML call site (`imageArea`'s `PreviewImageItem`, `PreviewPane.qml:78-82`):

```qml
PreviewImageItem {
    anchors.fill: parent
    image: root.preview.image
    visible: root.preview.hasImage
    radius: HnAppearance.roundedRadius(HnSurfaceRole.Card, width, height)
}
```

`HnAppearance.roundedRadius(role, width, height)` is the existing design-system entry point already
used for rounded surfaces elsewhere (`HnCardDelegate.qml:23`, `HnIconButton.qml:36`); `HnSurfaceRole.Card`
is the closest existing role to "a framed piece of media content." No new design-system API and no
hardcoded pixel constant are introduced.

Note the clip path is built from `destination` — the actual fitted image rect — not
`boundingRect()` (the full item rect). Since `imageArea`'s height is now derived from the same
`sourcePixelSize` aspect ratio the image itself has (§5.1), `destination` and `boundingRect()`
coincide almost exactly in the steady state, but clipping to `destination` is correct even for the
brief window where they don't (e.g. mid-resize, or the one-time square→aspect reflow described in
§5.1) — the rounded corners always hug the drawn pixels, never a stale frame edge.

---

## 5. Key decisions with rationale

### 5.1 Binding-loop break (REQ-NF-001): aspect from `sourcePixelSize`, height-change reporting removed entirely

**Decision:**

```qml
Item {
    id: imageArea
    readonly property real aspectRatio: (root.preview.sourcePixelSize.width > 0
            && root.preview.sourcePixelSize.height > 0)
        ? root.preview.sourcePixelSize.height / root.preview.sourcePixelSize.width
        : 1   // square fallback — see below
    readonly property real frameHeight: Math.min(240, Math.round(width * aspectRatio))

    Layout.fillWidth: true
    Layout.preferredHeight: frameHeight
    visible: root.preview.hasEntry
    onWidthChanged: root.reportImageAreaSize()   // onHeightChanged: REMOVED
    ...
}

function reportImageAreaSize(): void {
    const ratio = Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1;
    root.preview.setRequestedSize(PreviewService.Pane,
        Qt.size(imageArea.width * ratio, imageArea.frameHeight * ratio));
}
```

**Dependency graph** (arrows are "reads"):

```
sourcePixelSize (worker result; fixed per file, independent of requested size)
        │
        ▼
aspectRatio ──► frameHeight ──┬──► Layout.preferredHeight (visual)
                               └──► reportImageAreaSize()'s height argument
                                        │
                                        ▼
                              setRequestedSize() ──► worker redecode ──► applyResult()
                                                                              │
                                                                              ▼
                                                                    sourcePixelSize (same value — see §3)
```

This is acyclic in the sense that matters: the bottom arrow feeds back into the *same* value it
started from, not a new one. `sourcePixelSize` is read once via `QImageReader::size()` **before**
any scaling (`preview_service.cpp:125-127`) — it describes the file, not the decode's target size.
Re-dispatching a decode at a different requested size cannot change what `sourcePixelSize` reports
for the same file, so the "new decode → new image → new frame height" step in the hazard's own
description never actually produces a *new* aspect ratio to feed back with. The loop is broken at
the data level, which is stronger than just removing the QML signal handler.

Two further points the graph doesn't show:

- **`reportImageAreaSize()` never reads `imageArea.height`.** It reads `imageArea.frameHeight`, an
  independent property computed directly from `width` and `aspectRatio` — not the engine-managed
  `Item.height` that `Layout.preferredHeight` eventually produces. This avoids a one-frame lag: if
  the function instead read back `imageArea.height` inside the same handler that set
  `Layout.preferredHeight`, it could observe a stale value if the layout pass hasn't run yet,
  which is exactly the shape of bug `QML Binding loop detected` warnings catch.
- **The trigger is width/DPR-only; the requested *value* still has two components.** REQ-NF-001's
  "driven by panel WIDTH only" describes which *events* may call `reportImageAreaSize()`
  (`onWidthChanged`, `Screen.onDevicePixelRatioChanged` — never `onHeightChanged`), not that the
  `QSize` passed to `setRequestedSize()` omits height. The decode obviously still needs a target
  height; it is simply computed from width, never observed back from the frame.

**Before `sourcePixelSize` is known:** `aspectRatio` defaults to `1` (square), which is also
*exactly* today's shipped behavior (`Math.min(width, 240)` is a square cap). Non-image entries
(text files, directories, broken symlinks, and all three icon-fallback tiers) never populate
`sourcePixelSize` at all (`resetDisplayState()` leaves it `QSize()` and no image-mime worker path
ever runs for them), so their frame is *always* square — no jump, ever, for those entries. For an
image entry, the frame starts square on the very first paint after navigation and reflows once to
the correct aspect ratio the moment the first (thumbnail-tier, `final=false`) worker result arrives
— `source_pixel_size` ships in that same intermediate `PreviewResult` (`preview_service.cpp:126-127,
132-156`), so this is a single, early reflow (typically before or during the "Loading…" notice), not
a jump that recurs on every resize or every subsequent decode of the same file. This is a real,
visible, one-time layout change per file navigation — REQ-NF-001 only forbids binding-loop
*warnings*, not this reflow, and no attempt is made to hide it (e.g. by pre-fetching
`sourcePixelSize` before paint) since doing so would reintroduce exactly the ordering hazard this
section exists to avoid.

### 5.2 `mimeTypeDescription` sourced from the worker's already-resolved `QMimeType`, not a fresh lookup

See §4.4's rationale block. Rejected alternative: compute it in `setTarget()`'s synchronous
branches too (giving directories a "Folder" row). Rejected because it would add the *first*
disk-touching call to a function whose doc comment promises zero I/O, for a case SPEC.md's own
acceptance criteria never test. If a future cycle wants directory type descriptions, the natural
extension point is `setTarget()`'s `isDir` branch — the field is already `NOTIFY changed`, ready to
receive a value from anywhere.

### 5.3 Rounded corners: `QPainterPath`/`setClipPath` inside `paint()`, not `layer.enabled` or a `Rectangle` parent clip

**Decision:** §4.5 — an antialiased clip path built and applied inside `PreviewImageItem::paint()`.

**Rejected: `layer.enabled: true` + `layer.effect` (OpaqueMask/`MultiEffect` mask).** This allocates
a second offscreen texture (the layer's FBO) in addition to the `QImage` backing store
`QQuickPaintedItem` already renders into, and that texture's size/DPR handling is a *second*,
independent place device-pixel-ratio bugs can hide — exactly the failure class this project has
already been bitten by once (project memory: fractional-scale rendering gotcha; verified only on
the real 1.5x Hyprland display, never via offscreen capture). `paint()`'s clip path draws into the
buffer Qt Quick already allocates and already scales correctly for the window's device pixel ratio
today (this exact item, undecorated, already renders crisply on the user's 1.5x display) — adding
the clip does not touch that allocation at all, so it inherits the already-proven-correct DPR
behavior for free.

**Rejected: a rounded `Rectangle` parent with `clip: true`.** Qt Quick's scene-graph clip node for a
`Rectangle` with `radius > 0` is a stencil/scissor-style test — cheap, but not antialiased ("corners
alias" per the brief). This is worse, not better, at 1.5x fractional scale: the hard-edged clip
boundary and the fractional-scale pixel grid disagree in a way that is more visible than at integer
scale, precisely the class of artifact REQ-C-003 asks this project to check for on the real display.
`QPainter::setClipPath()` with `Antialiasing` enabled, by contrast, rasterizes the clip boundary with
the same antialiasing machinery already used for `SmoothPixmapTransform`'d image drawing — smooth
edges "for free," in the same render pass, no extra composition step.

**Cost:** effectively free. `paint()` already runs once per `update()` (on `setImage()`/on a
geometry change per `geometryChange()`, `preview_image_item.cpp:16-21`); adding two render-hint
sets, one `QPainterPath` construction, and one `setClipPath()` call is negligible next to the
`drawImage()` call it already makes.

### 5.4 Two-column table: static rows in a `GridLayout`, not a `Repeater`, not a separate `MetadataRow.qml`

**Decision:** each row is a hand-written pair of `HnLabel`s, both direct children of a `GridLayout
{ columns: 2 }`, both `visible`-bound to the same computed property (§4.2).

**Why not `Repeater` over a model:** the brief's own warning is exactly right — a `Repeater`
generating `GridLayout` children is fine in the common case, but ties visibility-driven row-hiding
to Repeater/model lifecycle subtleties (delegate creation order interacting with `GridLayout`'s
implicit-flow column assignment) for a set of rows that is small (9 total across both tables) and
*fixed* — every row's label, source property, and formatting is different, so there is no
reduction in genuinely-duplicated logic to be had by modeling them as data; only the boilerplate
(two `HnLabel`s per row) would be saved, and that boilerplate is exactly what makes each row
independently readable and greppable (consistent with this file's *existing* per-field EXIF blocks
at `PreviewPane.qml:152-175`, which already use this same explicit, unshared, `visible`-per-field
style — this design is a continuation of that pattern, not a new one).

**Why not a separate `MetadataRow.qml`:** would trigger REQ-C-002's registration burden (new `.qml`
file → `Taskfile.yml` + `scripts/check-qml-format.sh`) for a component that saves perhaps four lines
per row over the inline version, with no reuse outside this one file. Not worth it.

**Why `GridLayout { columns: 2 }` and not two independently-flowing `ColumnLayout`s of
`RowLayout`s:** `GridLayout`'s documented behavior — invisible children are excluded from layout
entirely, not just painted transparent — is precisely REQ-F-008/012's "no space reserved" behavior,
and it is a real, load-bearing Qt Quick Layouts guarantee (not something this design has to
reimplement). The two tables are two *separate* `GridLayout` instances (one per table), because
`exifTable`'s visibility as a whole unit (REQ-F-012) is simplest to express as one `visible:
root.preview.exifPresent` binding on its own `GridLayout`, rather than mixing metadata and EXIF rows
into a single grid with a discontinuous visible-run in the middle.

**Achieving identical column geometry across the two separate `GridLayout`s (REQ-F-011):** each
table's *own* `GridLayout` would, left to its own defaults, auto-size its label column from only
that table's own cells — which would not match between the two tables (e.g. "Focal length" is wider
than "Modified"). Every label cell in *both* grids instead sets an explicit
`Layout.preferredWidth: root.labelColumnWidth`, computed once (§4.2) from an offstage measurement of
every possible label across both tables — not just the currently-visible ones. This also prevents
the label column's width from visibly jumping between selections (e.g. narrowing when moving from a
JPEG with full EXIF to a `.txt` file with none) purely because fewer rows happen to be visible for
that particular file; the column width is a property of the *design*, not of the current selection.
Both tables' value columns then use plain `Layout.fillWidth: true` with no explicit width, so they
stretch to fill `GridLayout.width - labelColumnWidth - columnSpacing` — identical in both tables
because both are direct children of the same outer `content` `ColumnLayout` with the same margins,
and both use the same `columnSpacing` token.

### 5.5 REQ-F-013's Camera row is computed with `.filter().join(" ")`, not the existing `"%1 %2"` format string

The current EXIF block (`PreviewPane.qml:154`) does
`qsTr("Camera: %1 %2").arg(root.preview.exifMake).arg(root.preview.exifModel)` unconditionally —
make-only or model-only produces a stray leading/trailing space. REQ-F-013 explicitly requires "If
only make or only model is available, display that one field" with no such artifact. §4.2's
`cameraText` computed property (`[make, model].filter(p => p.length > 0).join(" ")`) fixes this as
part of the redesign rather than carrying the bug forward.

---

## 6. Alternatives considered and rejected

| Alternative | Rejected because |
|---|---|
| Give `PreviewService` a new `isDir`/`isDirectory` `Q_PROPERTY` for the Size row's "Dir" branch | `mimeType() == "inode/directory"` is already exposed today and is a more direct, already-present signal for exactly this purpose; adding a second property duplicates information the class already publishes (compare the sibling `main-view-icons` cycle's §5.6, which similarly avoided adding a redundant `isDir` property by reusing an existing exposed string). |
| Reuse the `iconName === "folder"` prefix trick (precedented in `imageArea.isFolderIconName`, `PreviewPane.qml:76`) for the Size row's directory detection | Works, but couples an unrelated feature (Size formatting) to icon-chain string conventions for no benefit over the more direct `mimeType` comparison; kept `isFolderIconName` scoped to its original icon-fallback purpose only. |
| Compute `mimeTypeDescription` via a fresh `QMimeDatabase().mimeTypeForName(mime_type_)` call inside `setTarget()`'s synchronous branches, so directories get a "Folder" row | Breaks `setTarget()`'s documented zero-I/O contract for a case SPEC.md's own acceptance criteria never exercise; see §5.2. |
| Extend `PreviewResult`/`ExifSummary` with a raw `ExifRational aperture` field and format it in QML instead of C++ | Pushes floating-point formatting logic (rounding, zero-denominator guarding) into QML/JS where it is harder to unit-test in isolation from the rest of the pane; the existing `normalizeExposureTime`/`normalizeFocalLength` precedent in `exif_reader.cpp` already establishes "format in C++, expose a display-ready string" as this file's convention. |
| `layer.enabled` + `MultiEffect` mask for rounded corners | See §5.3. |
| A `Rectangle { radius; clip: true }` wrapper around `PreviewImageItem` | See §5.3 — aliased corners, worse at 1.5x. |
| `Repeater` over a JS array model for the metadata/EXIF rows | See §5.4. |
| A separate `apps/files/MetadataRow.qml` reusable component | See §5.4 — triggers REQ-C-002 registration for negligible savings. |
| Register the new `SizeFormat.js` in `scripts/check-qml-format.sh` by extending the script to also run `qmlformat` (or a JS formatter) over `.js` files | See §6.2 — out of proportion to this cycle's scope, and inconsistent with the already-unformatted `IconFallbacks.js` precedent SPEC.md itself names. |

---

## 7. Known risks and mitigations

| Risk | Mitigation / verification |
|---|---|
| **`tests/smoke.cpp:649-651` calls the exact `formatSize` function this design deletes.** `QMetaObject::invokeMethod(pane, "formatSize", ...)` inside `TEST(Files, InspectionImageSplitterAndPixelSizing)` will fail (no such method) the moment `PreviewPane.qml`'s local `formatSize()` is removed. This is a required implementation-time edit, not a pre-existing pass/fail toggle. | Delete lines 649-651 outright (the three lines: the `invokeMethod` call and its two `EXPECT_TRUE(formattedSize.contains(...))` assertions). The behavior they tested (size formatting) is now covered by the new REQ-F-030 cross-surface equality test (§8) at the *shared-formatter* level, which is a strictly better test than invoking a soon-to-be-nonexistent per-file function. |
| **Same test's post-drag `imageSize.height() >= qRound(230 * dpr)` assertion implicitly assumed the old square-cap formula.** Verified numerically, not just assumed: the pane's `SplitView.preferredWidth` is `320` (`apps/files/Main.qml:138`) and the test drags it to `originalWidth + 100 ≈ 420` (line 683); `imageArea`'s width is that minus `content`'s margins (`2 × HnMetrics.internalSpacing(HnControlSize.Normal)`), comfortably above `320` — the width at which a 4:3 fixture image (this test's 800×600 `image.bmp`, `aspectRatio = 0.75`) already hits the `240`-px cap under the *new* aspect-derived formula (`240 / 0.75 = 320`). Both the old (`min(width, 240)`) and new (`min(240, width * 0.75)`) formulas therefore converge on `height = 240` at this test's actual widths, and the existing `>= 230 * dpr` margin still holds. | No code change required for this specific assertion, but re-run `Files.InspectionImageSplitterAndPixelSizing` explicitly after implementation (not just assumed passing from this analysis) — a differently-shaped or differently-sized fixture in a future change to this test would not enjoy the same numeric coincidence. |
| **The one-time square→aspect-ratio reflow (§5.1) is a real, user-visible layout change**, not fully eliminated, only made non-cyclic. | Accepted trade-off, stated plainly rather than hidden; REQ-NF-001 only prohibits binding-loop *warnings*. No mitigation attempted, since attempting to pre-know `sourcePixelSize` before the first paint would require either synchronous I/O on the UI thread or reintroducing a dependency this design specifically removes. |
| **Extending `ExifSummary`'s "all empty" gate must be paired with the new fields, or a lens/aperture-only JPEG silently reports `exifPresent == false`.** | §4.3 makes the extended condition explicit; `tests/exif_reader_test.cpp`'s new partial-EXIF case (§8) asserts `present == true` with only `lensModel`/`aperture` populated. |
| **`HnAppearance.roundedRadius()`'s behavior under the app's configurable corner style (`HnAppearance.cornerStyle`) is inherited, not chosen by this feature** — if a future/user-configured "sharp" corner style resolves to `radius == 0`, the thumbnail simply has square corners, silently diverging from REQ-F-001's "with rounded corners" wording in that configuration. | Not mitigated — this project already lets the same singleton govern every other rounded surface (cards, buttons) the same way; special-casing the thumbnail to ignore the app-wide corner style would be the actual inconsistency. Flagged here for visibility, not treated as a defect. |
| **Fractional-scale verification (REQ-C-003) remains manual.** No screenshot-based or offscreen-rendered test is relied on for antialiasing/DPR correctness, consistent with this project's established practice (project memory: fractional-scale rendering gotcha). | Manual check on the real Hyprland display at 1.5x during implementation: rounded thumbnail corners, the metadata/EXIF tables' column alignment, and the one-time square→aspect reflow all need visual confirmation there, not just in an automated `grabWindow()` capture. |

---

## 8. Test plan

### 8.1 `tests/exif_reader_test.cpp` (modified)

- `ExifReader.ExtractsAllFieldsFromAJpegWithExif` (existing, line 185): extend with
  `EXPECT_EQ(summary.lensModel, "HN 24-70mm F2.8")` and `EXPECT_EQ(summary.aperture, "f/8.0")`, once
  `tests/preview_fixtures.h`'s `buildSampleExifBlob()` gains the corresponding
  `detail::setAsciiEntry(..., EXIF_TAG_LENS_MODEL, "HN 24-70mm F2.8")` and
  `detail::setRationalEntry(..., EXIF_TAG_FNUMBER, ExifRational{8, 1}, order)` calls (SPEC.md's own
  "Extensions required" list).
- **New** `ExifReader.PartialExifWithOnlyLensAndApertureIsStillPresent`: a fixture built without
  make/model/exposure/iso/focal (a new small variant, or reuse `buildSampleExifBlobWithoutLens()`
  inverted) — asserts `present == true` with only `lensModel`/`aperture` populated, directly
  exercising the extended emptiness gate in §4.3.
- **New** `ExifReader.MissingLensAndApertureLeaveThoseFieldsEmptyWithoutFailingTheRead`: uses
  `buildSampleExifBlobWithoutLens()` (new fixture builder per SPEC.md), asserts
  `summary.lensModel.isEmpty() && summary.aperture.isEmpty() && summary.present` (the other five
  fields are still present) — REQ-C-001/REQ-NF-003.
- **New** `ExifReader.ApertureFormatsNonIntegerFStopsToOneDecimalPlace`: a fixture with
  `ExifRational{28, 10}` and one with `{63, 10}`, asserting `"f/2.8"` and `"f/6.3"` respectively.
- **New** `ExifReader.ZeroDenominatorApertureIsTreatedAsAbsent`: `ExifRational{8, 0}` →
  `summary.aperture.isEmpty()`, no crash.

### 8.2 `tests/preview_service_test.cpp` (modified)

- Extend `PreviewService.DirectoryTargetSetsInodeDirectoryMimeTypeWithNoWorkerDispatch`
  (line 116) or add a sibling assertion: `EXPECT_TRUE(service.mimeTypeDescription().isEmpty())` for
  a directory target — documents the §5.2 scope decision as an explicit, checked behavior rather
  than an implicit gap.
- **New** `PreviewService.MimeTypeDescriptionIsPopulatedAfterAnImageDecodeCompletes`: select a JPEG
  fixture, wait for settle, assert `!service.mimeTypeDescription().isEmpty()` (non-empty check per
  REQ-NF-004 — no literal-string assertion here; see §8.4 for the locale-pinned literal check).
- **New** `PreviewService.ExifLensModelAndApertureSurfaceFromTheExtendedSummary`: select
  `writeJpegWithExif()` (once its blob carries the new tags), assert
  `service.exifLensModel() == "HN 24-70mm F2.8"` and `service.exifAperture() == "f/8.0"`.
- **New** `PreviewService.ExifLensModelAndApertureClearOnRetarget`: select the EXIF fixture, then a
  plain text file; assert both new properties are empty again (mirrors the existing
  `ClearResetsToThePlaceholderState`/`RapidRetargetingOnlyEverShowsTheLastTarget` pattern already in
  this file).

### 8.3 `tests/preview_integration_test.cpp` (modified, REQ-F-028)

`SelectedFileEditsPermissionsReplacementRenameAndDeletionRefreshInPlace` (line 114): the three
`textContent()` assertions become:

```cpp
replaceText(path, "edited selected content");                              // 23 bytes
ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->size() == 23; }));
...
replaceText(replacement, "atomic replacement");                            // 18 bytes
...
ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->size() == 18; }));
...
replaceText(path, "reattached watch works");                               // 22 bytes
ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->size() == 22; }));
```

The three payload strings are unchanged (already exactly 23/18/22 bytes, verified by counting —
`"edited selected content"` = 23, `"atomic replacement"` = 18, `"reattached watch works"` = 22 —
so `size()` distinguishes all three unambiguously, satisfying the acceptance criterion without
changing the fixture payloads themselves). The subsequent rename assertion on `name()` and the
permissions assertion are untouched. Deliberately breaking the watcher (per the acceptance
criterion's own suggested check — commenting out the reattach-after-rename call in
`preview_service.cpp`) is a manual verification step during implementation, not a new automated
test.

### 8.4 `tests/smoke.cpp` (modified)

- **Delete** lines 649-651 (the `formatSize` `invokeMethod` block) per §7.
- **New**, addressing REQ-F-030's cross-surface equality directly: after opening a directory
  containing a known-size fixture file and letting both the listing and the pane settle, find
  `directoryEntryDelegate`'s `sizeColumnField` (existing `objectName`, `DirectoryListing.qml:307`)
  and the pane's new size-value label (give it `objectName: "previewSizeValue"` in §4.2's Size row)
  for the *same* row, and assert `sizeField->property("text") == previewSizeValue->property("text")`
  character-for-character. This is the test SPEC.md's own acceptance criterion for REQ-F-030
  describes ("a test selects a fixture file and asserts the sidebar's Size text equals the listing's
  metadata text for that same row").
- **New**: locate the metadata/EXIF `GridLayout`s (give them `objectName: "previewMetadataTable"` /
  `"previewExifTable"`) and assert a hidden row (e.g. Dimensions on a `.txt` fixture) does not
  consume vertical space — compare `exifTable`'s (or the metadata table's) `implicitHeight` between
  a fixture with all rows visible and one with some hidden, confirming no gap is reserved
  (REQ-F-008/012 at the rendered level, not just via `visible` bindings in isolation).
- Existing `InspectionImageSplitterAndPixelSizing`'s aspect/height assertions: re-run as-is per §7;
  no assertion text changes expected, but must be explicitly re-verified.

### 8.5 Locale pinning (REQ-NF-004)

Any test asserting a literal `mimeTypeDescription()`/`Modified` string wraps the assertion:

```cpp
const auto previous = QLocale();
QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
auto guard = qScopeGuard([&] { QLocale::setDefault(previous); });
// ... assert exact "JPEG image" / "9/16/26 ..." string here
```

mirroring the `qScopeGuard` idiom already used elsewhere in this test suite (per the `main-view-icons`
design cycle's precedent, `tests/smoke.cpp`/`tests/directory_performance_test.cpp`). A second test
run without pinning the locale asserts non-emptiness only, never a specific translated string.

### 8.6 Manual / non-automatable verification

- REQ-C-003: rounded thumbnail corners, table column alignment, and the one-time square→aspect
  reflow, all on the real Hyprland display at 1.5x scaling.
- REQ-NF-003: a folder of diverse real-world camera JPEGs (not synthesized fixtures), confirming
  graceful partial-EXIF display across manufacturers.

---

## 9. Build/tooling registration

- **`apps/files/CMakeLists.txt`**: no `SOURCES`/`QML_FILES` change is needed for `SizeFormat.js` —
  confirm how `IconFallbacks.js` is currently registered (as a `QML_FILES` entry in the
  `qt_add_qml_module(files-ui ...)` call, since it's `import`ed by path from `.qml` files in the same
  module) and add `SizeFormat.js` the same way, immediately alongside it.
- **`Taskfile.yml` / `scripts/check-qml-format.sh`**: **no changes.** Both are hardcoded to a fixed
  list of seven `.qml` paths (`scripts/check-qml-format.sh`'s `for qml in ...` loop; the mirrored
  `qmlformat -i` list at `Taskfile.yml:57`); `PreviewPane.qml` and `DirectoryListing.qml` are already
  in both lists, and no new `.qml` file is introduced. `SizeFormat.js`, like the pre-existing
  `IconFallbacks.js`, is **not** added to either — `scripts/check-qml-format.sh` only invokes
  `qmlformat` (a QML-syntax formatter) over `.qml` files; `qmlformat` is not a JS formatter and
  cannot format a `.pragma library` file. REQ-C-002's registration requirement is scoped to new
  `.qml` files specifically ("If this stage introduces a new `.qml` file... that file MUST be
  manually registered"); SPEC.md's own REQ-F-030 note anticipates exactly this gap ("if the shared
  helper is extracted into a `.js` file, `scripts/check-qml-format.sh` will NOT cover it... see
  REQ-C-002") and accepts it as a known, pre-existing class of gap rather than one this cycle must
  close. Extending the script to cover `.js` files is out of scope for this cycle (§6, alternatives
  table) — it would need a different tool entirely (there is no in-repo precedent for JS formatting)
  and is a larger, separate piece of tooling work.
- **`tests/CMakeLists.txt`**: no change — all modified/new test bodies land in already-registered
  files (`exif_reader_test.cpp`, `preview_service_test.cpp`, `preview_integration_test.cpp`,
  `smoke.cpp`), already listed in `files-smoke`'s `qt_add_executable` sources (`tests/CMakeLists.txt:2-4`).
- **`.clang-tidy`**: no change expected; new C++ (the two `exif_reader.cpp` helper functions, the
  `preview_image_item.cpp` clip-path addition) is small and single-purpose, well under the default
  cognitive-complexity threshold. Note for reviewers, carried over from prior cycles' documented
  gap: `HeaderFilterRegex` does not match `apps/files/*.h`, so header-only naming checks won't fire
  on `exif_reader.h`/`preview_service.h`/`preview_image_item.h` — pre-existing, not introduced here.

---

## 10. Requirement coverage map

| Requirement | Design element |
|---|---|
| REQ-F-001 | `imageArea.frameHeight` (§5.1); icon-fallback three-tier chain (`PreviewPane.qml:83-120`) untouched. |
| REQ-F-002 | Filename `HnLabel`, unchanged (`PreviewPane.qml:123-129`). |
| REQ-F-003 | `mimeTypeDescription` row (§4.2/§4.4); directory scope decision (§"Spec conflicts" item 3, §5.2). |
| REQ-F-004 | Metadata `GridLayout`, shared `labelColumnWidth` (§4.2/§5.4). |
| REQ-F-005 | `sizeText` (§4.2), `SizeFormat.js` (§4.1). |
| REQ-F-006 | `dimensionsText` with literal U+00D7 (§4.2). |
| REQ-F-007 | `formatModified()` unchanged; `modifiedText` sentinel-hides "Unavailable" (§"Spec conflicts" item 1). |
| REQ-F-008 | `GridLayout` excludes `visible: false` children (§5.4). |
| REQ-F-009 | `HnSeparator`, gated with the EXIF section (§"Spec conflicts" item 2). |
| REQ-F-010 | Bold "EXIF" `HnLabel`, `visible: exifPresent` (§4.2). |
| REQ-F-011 | Shared `labelColumnWidth`/`columnSpacing` across both `GridLayout`s (§5.4). |
| REQ-F-012 | `exifTable.visible: exifPresent`; extended emptiness gate (§4.3). |
| REQ-F-013 | `cameraText` filter/join (§4.2/§5.5). |
| REQ-F-014 | `exifLensModel` Q_PROPERTY (§4.4), `EXIF_TAG_LENS_MODEL` extraction (§4.3). |
| REQ-F-015 | `exifAperture` Q_PROPERTY (§4.4), `formatAperture()` (§4.3). |
| REQ-F-016 | `exifExposureTime`, unchanged passthrough. |
| REQ-F-017 | `exifIso`, unchanged passthrough. |
| REQ-F-018 | `exifFocalLength`, unchanged passthrough. |
| REQ-F-019 | `wrapMode: Text.WordWrap` on every value cell (§4.2); filename keeps `ElideMiddle`. |
| REQ-F-020 | `HnEmptyState`, unchanged (`PreviewPane.qml:230-236`). |
| REQ-F-021 | "Loading…" `HnLabel`, relocated below the tables, unchanged binding. |
| REQ-F-022 | Error `HnLabel`, relocated below the tables, unchanged binding. |
| REQ-F-023 | `ExifSummary::lensModel`/`aperture` (§4.3). |
| REQ-F-024 | `mimeTypeDescription` Q_PROPERTY (§4.4). |
| REQ-F-025 | `exifLensModel`/`exifAperture` Q_PROPERTYs (§4.4). |
| REQ-F-026 | No change to `text_preview_service.{h,cpp}` or the text Q_PROPERTYs (§2). |
| REQ-F-027 | `permissions` property untouched in C++; simply not rendered (§4.2, old caption line deleted). |
| REQ-F-028 | §8.3 — `size()`-based assertions. |
| REQ-F-029 | Text `Flickable`/`TextEdit`/truncation notice deleted from `PreviewPane.qml` only (§2). |
| REQ-F-030 | `SizeFormat.js` (§4.1), `tests/smoke.cpp` cross-surface equality test (§8.4). |
| REQ-NF-001 | §5.1 in full. |
| REQ-NF-002 | `Layout.fillWidth`/`wrapMode: WordWrap` throughout (§4.2); no elision in tables. |
| REQ-NF-003 | Extended emptiness gate (§4.3); manual diverse-JPEG check (§8.6). |
| REQ-NF-004 | §8.5. |
| REQ-NF-005 | `SplitView.minimumWidth: 220` (`Main.qml:139`, unchanged) + REQ-NF-002's wrapping. |
| REQ-C-001 | `formatAperture()`/`rationalEntryValue()` graceful-omission design (§4.3). |
| REQ-C-002 | No new `.qml` file; `SizeFormat.js`'s deliberate non-registration justified (§9). |
| REQ-C-003 | §5.3's DPR argument; manual verification (§8.6). |
| REQ-C-004 | `sizeText`'s directory branch never touches `sourcePixelSize`/EXIF/child enumeration (§4.2). |
| REQ-C-005 | No MakerNote code added; only `EXIF_TAG_LENS_MODEL`/`EXIF_TAG_FNUMBER` from `EXIF_IFD_EXIF` (§4.3). |
| REQ-C-006 | `QuickLookOverlay.qml` not in the modified-files list (§2). |
| REQ-C-007 | Three new Q_PROPERTYs, all `NOTIFY changed`, no new thread/service (§4.4). |

### Approved table-alignment correction (2026-09-16)

Both tables left-align labels and values to match `docs/mockups/moc1.png`.
Retain the shared widest-translated-label measurement, compact column spacing,
top-aligned wrapped rows and vertical viewport. Typography, colors, thumbnail
sizing, responsive widths and public APIs are unchanged. Geometry checks cover
full and partial EXIF at the default and 220 px widths; rendered captures are
compared with the mockup, alongside the existing scrolling regression.
