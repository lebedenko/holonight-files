# SDD Design — quick-look-redesign

Implementation note (2026-09-16): the JavaScript geometry helpers and QML settle/request
functions described below are superseded by the approved
[C++ presentation design](../quick-look-cpp-presentation/DESIGN.md). This document
retains the original design history; visual behavior and acceptance requirements remain.

Source of truth: `docs/sdd/quick-look-redesign/SPEC.md`. This document is grounded in the current
contents of `apps/files/QuickLookOverlay.qml`, `apps/files/PreviewPane.qml`,
`apps/files/preview_service.h/.cpp`, `apps/files/preview_image_item.h/.cpp`, `tests/smoke.cpp`,
and the `holonight-qt` design-system headers, all read as part of this design pass. Every
identifier below (property, function, file, objectName) either already exists at the cited
location or is explicitly called out as **new**.

## 1. Components

| Component | File | Status |
|---|---|---|
| `QuickLookOverlay` (`C.Popup`) | `apps/files/QuickLookOverlay.qml` | modified |
| `QuickLookGeometry.js` (`.pragma library`) | `apps/files/QuickLookGeometry.js` | **new** |
| `PreviewService` | `apps/files/preview_service.h/.cpp` | unchanged (read-only consumer) |
| `PreviewServiceTestAccess` | `tests/preview_service_test_access.h` | modified (one accessor added) |
| `PreviewImageItem` | `apps/files/preview_image_item.h/.cpp` | unchanged; only its existing `radius` QML property gets bound |
| Test cases | `tests/smoke.cpp` | modified (new `TEST(Files, …)` cases appended) |
| `apps/files/CMakeLists.txt` | build | modified (`QuickLookGeometry.js` added to `QML_FILES`) |

No `holonight-qt` file changes (REQ-C-003). No changes to `InspectionKeys.js`,
`directory_controller.{h,cpp}`, `PreviewPane.qml`, or the decode pipeline.

### 1.1 Why a new `.pragma library` file instead of inline QML functions

`QuickLookOverlay.qml` already imports two sibling `.pragma library` modules this way
(`InspectionKeys.js`, `IconFallbacks.js`), and `apps/files/CMakeLists.txt`'s `QML_FILES` list
already lists three such files alongside the `.qml` files — a fourth pure-JS module is an
established pattern here, not a new one.

`QuickLookGeometry.js` holds exactly the functions the SPEC states as "formulas" (entry-kind
classification, aspect-fit rect, compact-card width) — pure functions of numbers/strings with no
QML item references. Everything that must read a live `HnLabel.implicitHeight` or a palette color
stays as inline `readonly property` bindings in `QuickLookOverlay.qml`, matching
`PreviewPane.qml`'s own convention (`sizeText`, `dimensionsText`, etc. are plain QML properties,
not JS-module calls).

**Testability finding (SPEC design point 2):** grepping the repo (`find … -iname "*.qml" -path
"*test*"`, `grep -rn qmltestrunner`) turns up no `qmltestrunner`/`QmlTest` infrastructure and no
`TestCase {}` files anywhere in `holonight-files`; `tests/CMakeLists.txt` only builds gtest
binaries. So `QuickLookGeometry.js`'s functions are **not** unit-tested in isolation — none of that
infra exists to extend, and adding it is out of this cycle's scope. Instead it is exercised
indirectly: its outputs become the actual `width`/`height` of real `QQuickItem`s in a rendered
`files-smoke` window, and the new gtest cases assert on those. The module is still extracted
(rather than left inline) so a future cycle can add `qmltestrunner` coverage without touching
`QuickLookOverlay.qml`.

## 2. Data flow

```
DirectoryController::syncPreviewTarget()
        │  (unchanged — REQ-C-001)
        ▼
PreviewService (controller.preview)            ─┐
  hasEntry / busy / mimeType / hasText /         │ Q_PROPERTY, NOTIFY changed()
  sourcePixelSize / image / previewErrorKind /    │ (existing)
  previewErrorMessage / mimeTypeDescription /     │
  size / name / textTruncated / iconName          │
        │ changed()                              ─┘
        ▼
QuickLookOverlay.qml
  readonly property string kind: QuickLookGeometry.classify(...)   // pure function of preview.*
  Connections { target: preview; function onChanged() { root.settle() } }
        │
        ▼
  root.settle()  — imperative, called on preview.changed AND on bounds change
     reads: root.parent.width/height, preview.sourcePixelSize, kind,
            captionColumn implicit heights, hintLabel.implicitWidth (never nameLabel.implicitWidth)
     writes: root.settledWidth, root.settledHeight, root.settledKind,
             root.settledFrameWidth, root.settledFrameHeight       (all **new**, see §4)
        │
        ▼
  root.width/height  ◄── bound to settledWidth/settledHeight (replaces today's `parent.width*0.92`)
  previewFrame.width/height ◄── bound to settledFrameWidth/settledFrameHeight
  captionColumn (name/metadata/hint) ◄── bound directly to preview.* every time (REQ-F-014,
                                          independent of settle())

  root.reportRequestedSize()  — unchanged trigger philosophy, but now driven ONLY by
     root.parent.width/height + Screen.devicePixelRatio (never by settledFrameWidth/Height or
     preview.changed) — see §5.7 for why this decoupling is the key change from today's code.
        │
        ▼
  preview.setRequestedSize(PreviewService.QuickLook, size)   // existing Q_INVOKABLE, unchanged signature
```

The docked `PreviewPane` is untouched and keeps driving `PreviewConsumer::Pane` exactly as today;
both consumers still share the one `PreviewService` instance and its one decode pipeline
(`preview_service.cpp`'s `updateRequestedSize()`/`quick_look_active_` switch, unchanged).

## 3. Interfaces / APIs

### 3.1 `QuickLookGeometry.js` (new, `.pragma library`)

Pure functions only — no QML item references, no `qsTr` (translatable strings stay in
`QuickLookOverlay.qml`/`SizeFormat.js` per existing convention), no imports of C++ types (mirrors
`InspectionKeys.js`/`IconFallbacks.js`, which take everything as plain parameters because a
`.pragma library` file cannot `import` QML/C++ types).

```js
.pragma library

// Entry-kind classification (SPEC "Entry kind" terms). `mimeTypeIsImageError` must already fold
// in "previewErrorKind !== PreviewService.None" from the caller, since this file cannot reference
// the PreviewService enum type directly.
function classify(hasEntry, busy, mimeType, hasText, hasError) {
    if (!hasEntry)
        return "none";
    if (hasError)
        return "compact";                          // errors are always compact, settled or not
    if (mimeType.indexOf("image/") === 0)
        return "image";
    if (hasText)
        return "text";
    // Unknown mimeType, or a known non-image/non-text type whose decode is still running: its
    // final kind is not determinable yet (SPEC "compact" requires a settled, !busy entry).
    return busy ? "pending" : "compact";
}

// Largest size with `sourceWidth x sourceHeight`'s aspect ratio that fits inside
// `boundsWidth x boundsHeight`. Mirrors preview_image_item.cpp's fitRect() math (kept in sync
// deliberately — see §5.2 — but duplicated because that C++ function returns a QRectF used only
// for painting, not a QML layout size).
function fitRect(sourceWidth, sourceHeight, boundsWidth, boundsHeight) {
    if (sourceWidth <= 0 || sourceHeight <= 0 || boundsWidth <= 0 || boundsHeight <= 0)
        return {width: 0, height: 0};
    const scale = Math.min(boundsWidth / sourceWidth, boundsHeight / sourceHeight);
    return {width: sourceWidth * scale, height: sourceHeight * scale};
}

// Compact-card width: driven only by inputs that cannot vary per-entry in a way that would blow
// the card past `boundsWidth` (icon extent, the fixed hint string's implicit width, and a
// token-derived floor) — never by the filename or metadata text, which are elided/wrapped to fit
// whatever width this returns instead of growing it (see §5.1).
function compactCardWidth(minWidth, hintImplicitWidth, iconExtent, horizontalPadding, boundsWidth) {
    const contentWidth = Math.max(minWidth, hintImplicitWidth + 2 * horizontalPadding, iconExtent + 2 * horizontalPadding);
    return Math.min(contentWidth, boundsWidth);
}
```

### 3.2 `QuickLookOverlay.qml` — new/changed properties and functions on `root` (the `C.Popup`)

All **new** unless marked otherwise.

```qml
function currentKind(): string { … }     // §4.2 — QuickLookGeometry.classify(preview.*)

property string settledKind: "none"
property size settledSourcePixelSize: Qt.size(0, 0)
property real settledWidth: 0
property real settledHeight: 0
property real settledFrameWidth: 0
property real settledFrameHeight: 0

readonly property real cardPadding: HnMetrics.internalSpacing(HnControlSize.Normal)   // §5.1
readonly property real captionSpacing: HnMetrics.internalSpacing(HnControlSize.Compact)
readonly property real frameCaptionGap: HnMetrics.internalSpacing(HnControlSize.Normal)
readonly property real minCompactWidth: HnMetrics.controlHeight(HnControlSize.Hero) * 4  // §5.1, tunable
readonly property real iconExtent: HnMetrics.iconSize(HnControlSize.Hero) * 2

// name/metadata/hint implicit heights never depend on width (single-line, non-wrapping — §5.1);
// this is why captionReserve is stable across every entry, which REQ-F-017 depends on.
readonly property real captionReserve: nameLabel.implicitHeight + captionSpacing
    + metadataLabel.implicitHeight + captionSpacing + hintLabel.implicitHeight

property int requestedSizeCallCount: 0   // test-observable spy for REQ-F-018, see §5.8

function currentKind(): string {
    return QuickLookGeometry.classify(root.preview.hasEntry, root.preview.busy, root.preview.mimeType,
        root.preview.hasText, root.preview.previewErrorKind !== PreviewService.None);
}

function settle(): void { … }            // §4.2 — imperative, called on preview.changed and bounds change
function reportRequestedSize(): void { … }  // §5.7 — changed trigger wiring, same call shape
```

### 3.3 `tests/preview_service_test_access.h` (modified — allowed by REQ-C-003)

One additional **read-only** static accessor, no behavior change to `PreviewService`:

```cpp
struct PreviewServiceTestAccess {
  static void beforeFullDecode(PreviewService& service, std::function<void()> callback) { … }  // unchanged
  static void beforeDispatch(PreviewService& service, std::function<void()> callback) { … }     // unchanged
  // New: exposes the private quick_look_size_ set by setRequestedSize(QuickLook, …), so
  // REQ-F-017's "requested size unchanged across navigation" can be asserted directly instead of
  // inferred. PreviewService already declares `friend struct PreviewServiceTestAccess;`
  // (preview_service.h:122), so this needs zero new friend declarations.
  static QSize quickLookRequestedSize(const PreviewService& service) { return service.quick_look_size_; }
};
```

`preview_service.h`/`.cpp` themselves are **not modified** — `quick_look_size_` already exists
(`preview_service.h:171`) and is already written by the existing `setRequestedSize()`
(`preview_service.cpp:341-346`).

## 4. Key decisions with rationale

### 4.1 Card geometry formulas (design point 1)

Terms match the SPEC glossary exactly.

- **Bounds**: `boundsWidth = root.parent.width * 0.92`, `boundsHeight = root.parent.height * 0.92`
  (unchanged expression, just no longer bound straight to `root.width/height`).
- **cardPadding**: `HnMetrics.internalSpacing(HnControlSize.Normal)`, applied as the `C.Popup`'s
  own `padding` property (one token, all four insets — Popup's `padding`/`topPadding`/etc. default
  to it uniformly unless overridden, and we do not override per-side).
- **captionReserve**: `nameLabel.implicitHeight + captionSpacing + metadataLabel.implicitHeight +
  captionSpacing + hintLabel.implicitHeight`. Stable across every entry because `nameLabel`/
  `metadataLabel`/`hintLabel` never wrap (`elide`, no `wrapMode`), so `implicitHeight` is exactly
  one line's font-metric height regardless of string length or assigned `width` — the
  "`implicitHeight` does not depend on width" property the task brief calls out. This is a
  **deliberate divergence** from `PreviewPane.qml`'s `RowValue` (which wraps, e.g. for long EXIF
  lens names): Quick Look's metadata/error line stays single-line-elided so `captionReserve` — and
  therefore `previewBounds` and the REQ-F-017 decode-request size — cannot change between entries.
  A pathologically long compact-entry error message would be truncated instead of wrapped;
  flagged as a known risk in §6.
- **previewFrame width/height** ("preview bounds" in SPEC terms):
  `previewBoundsWidth = boundsWidth - 2 * cardPadding`
  `previewBoundsHeight = boundsHeight - 2 * cardPadding - frameCaptionGap - captionReserve`
  (`frameCaptionGap` is the `ColumnLayout`'s own `spacing` between the frame item and the caption
  `Column`, folded into the "caption reserve" subtraction so the two-term SPEC definition — bounds
  minus padding minus caption reserve — holds exactly if `captionReserve` is read to already
  include that gap; §4.2's code keeps them as two named constants for clarity but they are always
  subtracted together for image/text kinds).
- **Fitted image frame** (REQ-F-006): `QuickLookGeometry.fitRect(sourcePixelSize.width,
  sourcePixelSize.height, previewBoundsWidth, previewBoundsHeight)`.
- **Image/text card**: `cardWidth = min(boundsWidth, max(frameWidth + 2*cardPadding,
  minCompactWidth))`, `cardHeight = min(boundsHeight, frameHeight + 2*cardPadding +
  frameCaptionGap + captionReserve)`. The card wraps the fitted frame; for text the frame is the
  full preview bounds, so the card equals bounds.
- **Compact card** (REQ-F-011):
  `compactWidth = QuickLookGeometry.compactCardWidth(minCompactWidth, hintLabel.implicitWidth,
  iconExtent, cardPadding, boundsWidth)`
  `compactHeight = min(2*cardPadding + iconExtent + frameCaptionGap + captionReserve, boundsHeight)`
  Never uses `nameLabel.implicitWidth` or `metadataLabel.implicitWidth` as an input (see §4.1's
  bullet above and §6) — a 200-character filename (REQ-F-004's own fixture) must not grow the
  compact card past `compactWidth`; instead `nameLabel`'s `Layout.fillWidth` + `elide:
  Text.ElideMiddle` truncate it to whatever `compactWidth` already is.

### 4.2 `settle()` — explicit, imperative settled-geometry state (design point 3)

```qml
function settle(): void {
    let kind = QuickLookGeometry.classify(root.preview.hasEntry, root.preview.busy, root.preview.mimeType, root.preview.hasText, root.preview.previewErrorKind !== PreviewService.None);
    let sourceSize = root.preview.sourcePixelSize;
    if (kind === "pending" || kind === "none") {
        // Retain the layout inputs, not stale pixel extents: a resize must also refit the frame.
        kind = root.settledKind === "none" ? "compact" : root.settledKind;
        sourceSize = root.settledSourcePixelSize;
    }
    const boundsWidth = root.parent ? root.parent.width * 0.92 : 0;
    const boundsHeight = root.parent ? root.parent.height * 0.92 : 0;
    if (kind === "compact") {
        root.settledFrameWidth = root.iconExtent;
        root.settledFrameHeight = root.iconExtent;
        root.settledWidth = Math.floor(QuickLookGeometry.compactCardWidth(root.minCompactWidth, hintLabel.implicitWidth, root.iconExtent, root.cardPadding, boundsWidth));
        root.settledHeight = Math.floor(Math.min(2 * root.cardPadding + root.iconExtent + root.frameCaptionGap + root.captionReserve, boundsHeight));
    } else {
        const previewBoundsWidth = Math.max(0, boundsWidth - 2 * root.cardPadding);
        const previewBoundsHeight = Math.max(0, boundsHeight - 2 * root.cardPadding - root.frameCaptionGap - root.captionReserve);
        let frameWidth = previewBoundsWidth;
        let frameHeight = previewBoundsHeight;
        if (kind === "image" && sourceSize.width > 0 && sourceSize.height > 0) {
            const fitted = QuickLookGeometry.fitRect(sourceSize.width, sourceSize.height, previewBoundsWidth, previewBoundsHeight);
            frameWidth = fitted.width;
            frameHeight = fitted.height;
        }
        // Whole logical pixels keep the card edges and image clip crisp at fractional scales.
        root.settledFrameWidth = Math.floor(frameWidth);
        root.settledFrameHeight = Math.floor(frameHeight);
        // A narrow portrait still gets a readable caption; its frame centers in the wider card.
        root.settledWidth = Math.floor(Math.min(boundsWidth, Math.max(root.settledFrameWidth + 2 * root.cardPadding, root.minCompactWidth)));
        root.settledHeight = Math.floor(Math.min(boundsHeight, root.settledFrameHeight + 2 * root.cardPadding + root.frameCaptionGap + root.captionReserve));
    }
    root.settledKind = kind;
    root.settledSourcePixelSize = sourceSize;
}
```

Triggers: `Component.onCompleted: root.settle()` (so the first open never sees the initial
0×0 settled size), `Connections { target: root.preview; function onChanged(): void { root.settle(); } }`,
plus `root.parent.onWidthChanged: root.settle()` / `onHeightChanged: root.settle()` (window
resize). `root.width`/`root.height` bind to `settledWidth`/`settledHeight`; the frame item's
`width`/`height` bind to `settledFrameWidth`/`settledFrameHeight`.
This is a single assignment per `settle()` call — no `Behavior`, satisfying REQ-NF-001's "never
animated" and REQ-F-013's "at most one distinct intermediate size" (the width/height change fires
once, synchronously, inside one JS function call, not through a chain of dependent bindings).

- **Close/reopen (REQ-F-016):** `settledKind`/`settledWidth`/`settledHeight` live on `root`, a
  single long-lived `Popup` instance `Main.qml` creates once (not recreated per-open). Closing
  only flips `controller.quickLookOpen` (hence `root.visible`) — it never touches `preview` or
  calls `settle()`. `DirectoryController::syncPreviewTarget()` keeps running regardless of
  `quickLookOpen` (per this file's own header comment), so by the time the popup reopens,
  `preview.changed` — and therefore `settle()` — has already run for the entry under the cursor.
  There is no "reset to a default on reopen" path to get wrong, because nothing is ever reset.
- **Resize while pending:** `settle()` reuses `settledKind` and
  `settledSourcePixelSize` and computes the frame/card dimensions from the current
  bounds. At unchanged bounds this preserves the previous dimensions; on resize
  it keeps the caption inside the card and restores the original fit when the
  window returns to its former size. The next determined entry replaces the
  retained inputs. No old image is displayed while pending.

### 4.3 Busy indicator vs. image visibility (design point 4, REQ-F-015)

The user chose a **spinner** in Stage 0. `Holonight.Controls`' `HnLoadingState.qml` renders a
linear indeterminate `H.ProgressBar`, not a spinner, and `holonight-qt` has no spinner control, so
the overlay uses `QtQuick.Controls.Basic`'s `BusyIndicator` (already importable as `C`), sized from
`HnMetrics.iconSize(HnControlSize.Hero)`. No `holonight-qt` change needed (REQ-C-003). If its
default Basic-style color reads poorly on `surface`, tint via its `palette.dark` from
`HoloniightPalette.textMuted` — never a hex literal.

```qml
C.BusyIndicator {
    objectName: "quickLookBusy"                      // new objectName, §4.7
    anchors.centerIn: parent                          // centered in previewFrame
    width: HnMetrics.iconSize(HnControlSize.Hero)
    height: width
    visible: root.preview.busy && !root.preview.hasImage && !root.preview.hasText
    running: visible
}
```

This directly implements REQ-F-015's condition (`busy && !hasImage && !hasText`), independent of
`kind`/`settledKind` — it can be visible while `kind === "pending"` (mimeType still empty) *or*
while `kind === "image"` but the first async result hasn't arrived yet with any image bytes. The
existing `PreviewImageItem` inside `quickLookImageArea` only ever holds `preview.image`
(`preview_service.cpp`'s `applyResult()` only replaces `display_image_` with a same-or-larger
image for the *same* generation/target — `setTarget()` clears it synchronously first), so binding
`image: root.preview.image` (unchanged) can never show a stale image from a different target;
`quickLookImageArea`'s existing `visible: root.preview.hasImage` binding is kept as-is.

### 4.4 Metadata text per kind (design point 5, REQ-F-008/010/012/014)

Kept as an inline `readonly property string` on `root`, mirroring `PreviewPane.qml`'s
`sizeText`/`dimensionsText` convention rather than moving into `QuickLookGeometry.js` (this logic
needs `qsTr()` and `SizeFormat.formatSize()`, both already imported in this file; `.pragma
library` files in this codebase do use `qsTr` — see `SizeFormat.js` — but keeping it here avoids
threading five loose parameters through a JS call for no reuse benefit, since nothing outside this
file needs it):

```qml
readonly property string metadataText: {
    if (!preview.hasEntry)
        return "";
    if (preview.mimeType === "inode/directory")
        return qsTr("Dir");                                              // REQ-F-012
    if (preview.previewErrorKind !== PreviewService.None)
        return preview.previewErrorMessage;                              // REQ-F-012
    if (preview.mimeType.length === 0)
        return SizeFormat.formatSize(preview.size);                      // REQ-F-014 (pending)
    if (preview.mimeType.indexOf("image/") === 0) {
        return preview.sourcePixelSize.width > 0 && preview.sourcePixelSize.height > 0
            ? qsTr("%1 × %2 · %3").arg(preview.sourcePixelSize.width)
                  .arg(preview.sourcePixelSize.height).arg(SizeFormat.formatSize(preview.size))
            : SizeFormat.formatSize(preview.size);                       // REQ-F-008
    }
    if (preview.hasText) {
        return SizeFormat.formatSize(preview.size)
            + (preview.textTruncated ? qsTr(" · truncated") : "");  // REQ-F-010
    }
    return preview.mimeTypeDescription.length > 0
        ? preview.mimeTypeDescription : preview.mimeType;                // REQ-F-012 (settled compact, non-error)
}

readonly property color metadataColor: preview.previewErrorKind !== PreviewService.None
    ? HoloniightPalette.error : HoloniightPalette.textMuted
```

The `mimeType.length === 0` branch is checked *before* the image/text/compact branches so
REQ-F-014 (pending → `formatSize(size)` immediately) and REQ-F-012 (settled compact →
`mimeTypeDescription`) don't collide: a pending entry (mimeType not yet known) always hits the
size-only branch; a settled compact entry (mimeType known, not image/text/dir/error) hits the
final branch.

Hint line (REQ-F-005): `hintLabel.text: qsTr("Press Space or Esc to close")`,
`color: HoloniightPalette.textDisabled` — unconditionally dimmer than `metadataColor`
(`textMuted` or `error`).

### 4.5 Backdrop (design point 6, REQ-F-003)

```qml
C.Popup {
    id: root
    …
    C.Overlay.modal: Rectangle {
        color: HoloniightPalette.scrim
    }
```

`C.Overlay.modal` (qualified, because the file imports Controls `as C`) is the standard `QtQuick.Controls` attached-property component QQC2 uses to draw
the dimming layer behind a modal popup; it is automatically sized to the full `Overlay.overlay`
item (the whole window), which is exactly REQ-F-003's "covers the full window size" acceptance.
`modal: true` and `closePolicy: C.Popup.NoAutoClose` are both already set today and remain
unchanged (REQ-C-004): the backdrop dims but does not close on click.

### 4.6 `setRequestedSize` wiring (design points 7, REQ-F-017/018)

```qml
function reportRequestedSize(): void {
    const ratio = Screen.devicePixelRatio > 0 ? Screen.devicePixelRatio : 1;
    const boundsWidth = root.parent ? root.parent.width * 0.92 : 0;
    const boundsHeight = root.parent ? root.parent.height * 0.92 : 0;
    const width = Math.max(0, boundsWidth - 2 * root.cardPadding);
    const height = Math.max(0, boundsHeight - 2 * root.cardPadding - root.frameCaptionGap - root.captionReserve);
    root.preview.setRequestedSize(PreviewService.QuickLook, Qt.size(width * ratio, height * ratio));
    root.requestedSizeCallCount += 1;
}

Component.onCompleted: root.reportRequestedSize()
Screen.onDevicePixelRatioChanged: root.reportRequestedSize()
Connections {
    target: root.parent
    function onWidthChanged(): void { root.reportRequestedSize(); }
    function onHeightChanged(): void { root.reportRequestedSize(); }
}
```

This is the one substantive behavioral break from today's file. Today,
`reportRequestedSize()` is wired to `root`'s and `imageArea`'s own `widthChanged`/`heightChanged`
— harmless only because `imageArea` today just `Layout.fillWidth`/`fillHeight`s the popup's fixed
`parent.width*0.92` size and never resizes on its own. Once `quickLookImageArea` is aspect-fitted
per entry (REQ-F-006) and the popup itself resizes for compact entries (REQ-F-011), binding the
request size to `root.width`/`imageArea.width` would fire `setRequestedSize` on every navigation
that changes the fitted frame or the card — exactly what REQ-F-018 forbids. The fix: compute the
requested size from `root.parent.width/height` (the *window*, unaffected by navigation) and the
constant `captionReserve` (§4.1), triggered only by `root.parent`'s size changes,
`Screen.devicePixelRatioChanged`, and `Component.onCompleted` — never by `preview.changed` or
`settle()`.

`requestedSizeCallCount` is a plain integer property incremented at the one call site; it is not
gated by "did the value change" (an actual value-based no-op guard already exists downstream in
`PreviewService::updateRequestedSize()`, `preview_service.cpp:353-361`), so it is a direct,
literal count of *invocations from QML*, matching what REQ-F-018's acceptance criterion asks a
"call counter" to observe.

### 4.7 Test access and objectNames (design point 8)

New objectNames (none of the four REQ-C-002 names change or move):

| objectName | Element | Purpose |
|---|---|---|
| `quickLookCard` | the `C.Popup`'s `background: Rectangle` | read `radius`/`width`/`height` for REQ-F-001/002 |
| `quickLookName` | the name `HnLabel` | read `text`/`truncated` for REQ-F-004 |
| `quickLookMetadata` | the metadata `HnLabel` | read `text`/`color` for REQ-F-008/010/012 |
| `quickLookHint` | the hint `HnLabel` | read `text`/`color` for REQ-F-005 |
| `quickLookBusy` | the `C.BusyIndicator` | read `visible`/`running` for REQ-F-015 |
| `quickLookIcon` | the compact-card `HnIcon` | read `visible`/`source` for REQ-F-011 |

(`quickLookOverlay`, `quickLookContent`, `quickLookImageArea`, `quickLookText` are unchanged per
REQ-C-002.)

`PreviewServiceTestAccess::quickLookRequestedSize()` (§3.3) lets tests assert REQ-F-017 directly:
`PreviewServiceTestAccess::quickLookRequestedSize(*controller.preview())` should be bit-identical
across a 600×400 → 400×600 → 1000×200 navigation sequence with Quick Look open and the window size
unchanged.

## 5. Where tests live (design point 9)

**All new tests are appended to `tests/smoke.cpp`**, as additional `TEST(Files, …)` cases. No new
test binary, no `tests/CMakeLists.txt` change.

Rationale:
- The precedent tests (`QuickLookConsumesSpace…` at smoke.cpp:469,
  `InspectionImageSplitterAndPixelSizing` at smoke.cpp:629,
  `PreviewSidebarRowsHideWrapAndStayFreeOfBindingLoops` at smoke.cpp:1057 with its
  `qInstallMessageHandler`-based binding-loop counter at smoke.cpp:997-1015) already do exactly
  what the new tests need: load `Main.qml` via `QQmlApplicationEngine`, `findChild` by objectName,
  drive with `QTest::keyClick`, read back QML properties.
- `files-smoke` already links `files-ui`/`files-uiplugin`/`Qt6::Test`/`GTest::gtest` and runs
  offscreen with software rendering — everything these assertions need.
- The other two gtest binaries (`files-fsops-smoke`, `files-fsops-window-smoke`) exist only to
  isolate `unshare()`-based mount-namespace tests from `files-smoke`'s chmod-000 fixtures — an
  orthogonal concern; no reason to add a fourth binary for Quick Look.
- `bindingLoopCounter()`/`countBindingLoops()` are reused as-is, not duplicated.

New includes needed at the top of `smoke.cpp`: `"preview_service_test_access.h"` (for §3.3's
accessor). `preview_fixtures.h` is already included.

### 5.1 New test cases (mapped to REQ IDs)

| Test (proposed `TEST(Files, …)` name) | REQ IDs | What it does |
|---|---|---|
| `QuickLookCardStaysWithinBoundsForEveryKind` | F-001, F-002 | 1280×800 window; image/text/directory entries; assert `quickLookCard` `width ≤ 1177.6+1`, `height ≤ 736+1`, centered within 1px, `background.radius > 0`. |
| `QuickLookBackdropCoversWindowWithScrim` | F-003 | Open Quick Look; find the `Overlay.overlay` modal child; assert its size equals the window's and its `color` equals `HoloniightPalette.scrim`. |
| `QuickLookNameElidesLongFilenamesAndStaysCentered` | F-004 | 200-char and 5-char filenames; assert `quickLookName.truncated` true/false, width ≤ card content width, centered. |
| `QuickLookHintDimmerThanMetadata` | F-005 | Image/text/compact entries; assert hint text/color (`textDisabled`) vs. metadata color (`textMuted`). |
| `QuickLookImageFrameAspectFitsPreviewBounds` | F-006, F-007 | `renderJpegBytes({6000,4000}/{1000,5000}/{5000,1000})`; assert `quickLookImageArea` size per the three REQ-F-006 cases and a non-literal clip radius. |
| `QuickLookImageMetadataLineShowsDimensionsThenSizeOnly` | F-008 | `beforeDispatch` blocks the worker; assert size-only text pre-result, `"600 × 400 · " + size` post-result. |
| `QuickLookTextFrameFillsPreviewBoundsWithMonospaceView` | F-009 | `writeLargeText()`; assert `quickLookText.readOnly`, wrapping, monospace font, enclosing `Flickable` scrolls. |
| `QuickLookTextMetadataShowsTruncatedSuffix` | F-010 | `writeSmallText()` → exact size; `writeLargeText()` → size + `" · truncated"`. |
| `QuickLookCompactCardShowsDirIconAndStaysSmall` | F-011, F-012 | Directory at 1280×800 → card < half bounds, `quickLookIcon` visible; `"Dir"` metadata; unaffected by widening to 1920×1080. |
| `QuickLookCompactCardShowsErrorAndMimeDescription` | F-012 | `writeBrokenSymlink()`/`writeCorruptJpeg()` → error text in `error` color; `writeRandomBinary()` → `mimeTypeDescription`/`mimeType`. |
| `QuickLookRetainsSettledGeometryWhilePending` | F-013 | `beforeDispatch` blocks; navigate 600×400→400×600 image; card size frozen while blocked; one post-release `widthChanged`/`heightChanged`; no `Behavior`/`NumberAnimation` in the file. |
| `QuickLookCaptionUpdatesImmediatelyWhilePending` | F-014 | Same blocked state; name/metadata already reflect the new entry. |
| `QuickLookBusyIndicatorHidesStaleImage` | F-015 | Same blocked state; `quickLookBusy` visible+running, image hidden/null; inverse after release. |
| `QuickLookReopenOnDifferentKindUsesNewGeometry` | F-016 | Open on directory, close, settle on a 600×400 image, reopen; geometry equals the image case, not the directory's. |
| `QuickLookRequestedSizeStableAcrossNavigationButNotResize` | F-017, F-018 | Navigate 600×400/400×600/1000×200 images; `quickLookRequestedSize()` and `requestedSizeCallCount` unchanged; then resize the window and confirm both change. |
| `QuickLookNavigationAndResizeProduceNoBindingLoops` | NF-001 | Reuse `bindingLoopCounter()` (smoke.cpp:997); navigate/resize across all kinds; zero warnings. |
| (existing) `NativeInspectionAcceptance` | NF-003 | Unchanged; `EXPECT_LT(quickLookMs, 200)` (smoke.cpp:621) still expected to pass — decode path untouched. Opt-in, not run by default. |
| (existing) `QuickLookConsumesSpace…`, `InspectionImageSplitterAndPixelSizing` | C-001, C-002, C-004 | Must keep passing verbatim against the restructured internals — this *is* the acceptance test for C-001/002. |

### 5.2 Binding-loop test mechanics (design point 10)

Identical pattern to the existing `PreviewSidebarRowsHideWrapAndStayFreeOfBindingLoops` test:
install `countBindingLoops` via `qInstallMessageHandler` before constructing the
`QQmlApplicationEngine`, restore the previous handler via `qScopeGuard` on scope exit, and assert
`bindingLoopCounter().warnings.load() == 0` after driving the scenario. No new helper needed.

## 6. Known risks / infeasibilities found in the SPEC

1. **Compact-entry error messages may be truncated instead of wrapped.** §4.1 forces the metadata
   label to be single-line/elided for every kind so `captionReserve` (and hence the REQ-F-017
   decode-request size) stays constant across navigation, unlike `PreviewPane.qml`'s equivalent
   label (`RowValue`, `Text.Wrap`). A very long `previewErrorMessage` on a compact entry would be
   visibly truncated — a real behavior trade-off the SPEC doesn't call out, forced by REQ-F-017's
   stability requirement conflicting with wrapping. No listed acceptance criterion currently
   exercises a message long enough to expose this, so nothing fails. **Decision (orchestrator
   review):** accept elision — preview errors are short `strerror`-style strings or
   "Cannot decode image"/"No preview available"; the full text remains in the info sidebar. The
   name/metadata labels are single-line elided for every kind.
2. **REQ-F-002's grep cannot verify "derived from HnMetrics tokens" for `minCompactWidth`.** A
   literal `readonly property real minCompactWidth: 400` would pass the grep (which only matches
   `radius`/`color`/`margins`/`padding`/`spacing`) while violating REQ-F-011's intent. This has to
   be caught by review against §4.1's formula, not by `task format-check`.
3. **`Overlay.modal` availability under the `Basic` style is unconfirmed in this repo.** No
   existing `apps/files/` or vendored `holonight-qt` QML uses `Overlay.modal`, though it is
   standard `QtQuick.Controls` API. Since this app pins `QtQuick.Controls.Basic` specifically,
   verify it renders correctly before relying on it for REQ-F-003; fall back to a `Rectangle`
   manually reparented into `C.Overlay.overlay` if it does not.
4. **No infeasibility found that blocks the SPEC overall.** Every requirement (REQ-F-001–018,
   REQ-NF-001–003, REQ-C-001–005) is implementable with existing `PreviewService`/
   `PreviewImageItem`/`HoloniightPalette`/`HnMetrics`/`HnAppearance`/`HnLabel`/`HnIcon`/
   `C.BusyIndicator` APIs verified in this pass, with zero changes to `holonight-qt`, the decode
   pipeline, or `InspectionKeys.js`.

## 7. Non-goals reaffirmed

No zoom/pan/rotation/slideshow, no EXIF in Quick Look, no new key bindings, no click-outside-to-close, no open/resize animation, no `PreviewPane.qml`/`holonight-qt`/decode-pipeline changes — all unchanged from the SPEC and untouched by this design.

## Review remediation — pending resize and test cleanup

`settle()` retains `settledKind` and `settledSourcePixelSize` in addition to the
computed dimensions. For pending or absent entries it recomputes geometry from
those retained inputs and current window bounds, rather than returning early.
This preserves geometry during navigation at a fixed window size and keeps the
frame and caption within the resized card without introducing size feedback.
The pending source dimensions are never used to display an old image.

Blocked-worker tests declare their release scope guard after `QuickLookHarness`,
so even a fatal assertion releases the worker before the harness destructor joins it.
The §4.2 implementation reflects this behavior.
