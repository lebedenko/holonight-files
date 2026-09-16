# SDD Spec — quick-look-redesign

## Overview

Redesign `apps/files/QuickLookOverlay.qml` (modal `QtQuick.Controls` Popup bound to
`controller.preview`) to match the "Quick look" card in `docs/mockups/moc1.png`: a rounded,
padded card holding the preview, a centered filename, a muted metadata line
(`6000 × 4000 · 8.3 MB`) and a dim hint (`Press Space or Esc to close`).

Terms used below:

- **Bounds** — the logical rectangle `0.92 × window.width` by `0.92 × window.height`, centered in
  the window. The card never exceeds it.
- **Preview area** — the part of the card above the caption (filename, metadata, hint), inside the
  card padding. **Preview bounds** = bounds minus card padding minus the caption reserve.
- **Entry kind** — derived from `PreviewService`:
  - *image*: `mimeType` starts with `image/` and no preview error.
  - *text*: `hasText`.
  - *compact*: directory (`mimeType === "inode/directory"`), non-regular file, any
    `previewErrorKind !== None`, or a settled (`!busy`) entry that is neither image nor text.
  - *pending*: `hasEntry`, `busy`, and none of the above determinable yet (`mimeType` empty).
- **Settled geometry** — the card size last computed for a non-pending entry.

`PreviewService` behaviour this spec relies on (verified in `preview_service.cpp`):
`setTarget()` synchronously resets `image`, `sourcePixelSize`, `hasText`, error and `mimeType`
(directories, special files and stat failures get their `mimeType`/error synchronously);
for regular files `mimeType`, `sourcePixelSize` and the first image arrive together in
asynchronous (possibly non-final) results, with `busy` true until the final one.

---

## Functional Requirements

### Card layout

#### REQ-F-001 — Card within bounds
**Ubiquitous:** The Quick Look popup shall render as a single card, centered in the window, whose
width and height never exceed the bounds.

**Acceptance:** For a 1280×800 window, with each of an image, a text file and a directory
previewed, the `quickLookOverlay` popup satisfies `width ≤ 1177.6 + 1`, `height ≤ 736 + 1`, and
its center lies within 1 px of the window center.

#### REQ-F-002 — Card styling
**Ubiquitous:** The card shall use `HoloniightPalette.surface` fill, an `HnMetrics.borderWidth`
border in `HoloniightPalette.borderPassive`, a corner radius from
`HnAppearance.roundedRadius(HnSurfaceRole.Card, …)`, and padding from `HnMetrics` spacing tokens.

**Acceptance:** `grep -nE 'radius: *[0-9]|color: *"#|margins: *[0-9]|padding: *[0-9]|spacing: *[0-9]' apps/files/QuickLookOverlay.qml`
returns no matches; the popup background's `radius` property is > 0 at runtime.

#### REQ-F-003 — Dimming backdrop
**Ubiquitous:** While the popup is open, the system shall draw a backdrop over the whole window
behind the card using `HoloniightPalette.scrim`.

**Acceptance:** With Quick Look open, the popup's modal overlay item exists, covers the full
window size, and its color equals `HoloniightPalette.scrim`; a native screenshot shows the
listing visible but darkened.

#### REQ-F-004 — Centered filename
**Ubiquitous:** The card shall show `preview.name` below the preview area, horizontally centered,
Subheading role, bold, `textPrimary`, middle-elided when wider than the card content width.

**Acceptance:** An entry named with 200 characters yields a name label whose `truncated` is
true and whose width ≤ card content width; a 5-character name is not truncated; the label's
horizontal center is within 1 px of the card center.

#### REQ-F-005 — Hint line
**Ubiquitous:** The card shall show the translatable text "Press Space or Esc to close" as the
last caption line, centered, Caption role, in a color dimmer than the metadata line
(`HoloniightPalette.textDisabled`).

**Acceptance:** For image, text and compact entries the hint label is visible, its text equals
`"Press Space or Esc to close"`, and its color equals `textDisabled` while the metadata
label's color equals `textMuted`.

### Image entries

#### REQ-F-006 — Aspect-fit frame
**State-driven:** While the entry kind is *image* and `sourcePixelSize` is valid, the preview
frame shall have the largest size with the aspect ratio of `sourcePixelSize` that fits inside
the preview bounds.

**Acceptance:** At a 1280×800 window: a 6000×4000 source gives a frame with
`|frame.width × 4000 − frame.height × 6000| ≤ 6000` (≤ 1 px aspect error) and exactly one
dimension equal (±1 px) to the corresponding preview-bounds dimension; a 1000×5000 source's
frame height equals the preview-bounds height (±1 px) and width < preview-bounds width; a
5000×1000 source's frame width equals the preview-bounds width (±1 px) and height <
preview-bounds height.

#### REQ-F-007 — Image never upscaled beyond the frame, rounded clip
**Ubiquitous:** The image shall be drawn aspect-fit inside the frame and clipped to rounded corners
whose radius comes from `HnAppearance.roundedRadius`.

**Acceptance:** The `quickLookImageArea` item's width/height equal the frame size (±1 px); the
clip/mask element's radius is > 0 and not a numeric literal in QML (checked by the REQ-F-002 grep).

#### REQ-F-008 — Image metadata line
**State-driven:** While the entry kind is *image*, the metadata line shall read
`"<W> × <H> · <formatSize(size)>"` (U+00D7, U+00B7), and shall read only
`"<formatSize(size)>"` while `sourcePixelSize` is invalid.

**Acceptance:** A generated 600×400 JPEG of byte size S gives metadata text
`"600 × 400 · " + SizeFormat.formatSize(S)`; before its first result arrives (hold the job with
the existing `before_dispatch_for_test_` hook) and with `mimeType` forced to image, the text
equals `SizeFormat.formatSize(S)` with no `×` character.

### Text entries

#### REQ-F-009 — Full-bounds text frame
**State-driven:** While the entry kind is *text*, the preview frame shall equal the preview bounds
and contain a read-only, scrollable, wrapping, monospace text view (`quickLookText`) inside a
rounded, padded inner surface.

**Acceptance:** For `writeLargeText()` the frame size equals preview bounds (±1 px);
`quickLookText.readOnly` is true, `wrapMode` is a wrapping mode, font family equals
`HolonightTheme.monospaceFont`; the enclosing Flickable's `contentHeight > height`
and `contentWidth === width`.

#### REQ-F-010 — Text metadata line
**State-driven:** While the entry kind is *text*, the metadata line shall read
`formatSize(size)`, followed by `" · truncated"` when `preview.textTruncated` is true.

**Acceptance:** `writeSmallText()` gives exactly `formatSize(size)`; `writeLargeText()` (which
exceeds the text preview cap) gives `formatSize(size) + " · truncated"`.

### Compact entries

#### REQ-F-011 — Compact card
**State-driven:** While the entry kind is *compact*, the card shall size to its content (icon from
`preview.iconName`, filename, metadata, hint) with a minimum width derived from `HnMetrics`
tokens, and shall be strictly smaller than the bounds in both dimensions.

**Acceptance:** Previewing a directory at 1280×800 gives card `width < 0.5 × bounds.width` and
`height < 0.5 × bounds.height`; the icon item is visible with a non-empty source derived from
`iconName`; widening the window to 1920×1080 does not change the card size.

#### REQ-F-012 — Compact metadata line
**State-driven:** While the entry kind is *compact*, the metadata line shall read `"Dir"` for
directories, `preview.previewErrorMessage` in `HoloniightPalette.error` color when
`previewErrorKind !== None`, and otherwise `preview.mimeTypeDescription` (falling back to
`mimeType` when the description is empty).

**Acceptance:** A directory shows `"Dir"` in `textMuted`; `writeBrokenSymlink()` shows the
entry's non-empty `previewErrorMessage` in `error` color; `writeCorruptJpeg()` (after `busy`
becomes false) shows its non-empty error message in `error` color; `writeRandomBinary()` shows a
non-empty text equal to `mimeTypeDescription` or `mimeType`.

### Navigation and loading

#### REQ-F-013 — Settled geometry retained while pending
**Event-driven:** When the target changes while Quick Look is open and the new entry kind is
*pending*, the card shall keep its settled geometry while the window bounds are unchanged; if the window
is resized while pending, the retained entry kind and source dimensions shall be fitted to the
new bounds so the frame and caption remain inside the card. When the kind becomes non-pending, the card
shall change to the new geometry in one step without animation.

**Acceptance:** With `before_dispatch_for_test_` blocking, press `j` from a settled 600×400 image
to a 400×600 image: card width/height stay equal to the previous values while blocked;
after release, a `widthChanged`/`heightChanged` spy on the popup records the transition with
at most one distinct intermediate size (the final one) and no `Behavior`/`NumberAnimation` exists
on the card geometry.

#### REQ-F-014 — Immediate caption update
**Event-driven:** When the target changes while Quick Look is open, the filename shall update
immediately, and the metadata line shall show at least `formatSize(size)` (or `"Dir"`) immediately,
independent of geometry settling.

**Acceptance:** In the REQ-F-013 blocked state, the name label text already equals the new
entry's name and the metadata text equals `formatSize(newSize)`.

#### REQ-F-015 — Busy indicator, never a stale image
**State-driven:** While `preview.busy` is true and `preview.hasImage` and `preview.hasText` are both
false, the preview frame shall show a running busy indicator and no image.

**Acceptance:** In the REQ-F-013 blocked state, the busy indicator is visible and running, and
the image item is invisible or has a null image; after the final result, the indicator is
invisible and the image is visible.

#### REQ-F-016 — Unwanted: stale settled geometry after close
**Unwanted behaviour:** If Quick Look is closed and reopened on a different entry, then the card
shall use the new entry's geometry (or the pending rule of REQ-F-013 relative to the last
settled geometry), never a size from an unrelated entry of a different kind once the new kind
is known.

**Acceptance:** Open on a directory (compact), close, move to a 600×400 image, wait for
`!busy`, reopen: card geometry equals the image aspect-fit geometry of REQ-F-006.

### Decode request sizing

#### REQ-F-017 — Request size from bounds only
**Ubiquitous:** The overlay shall call
`preview.setRequestedSize(PreviewService.QuickLook, s)` with `s` = preview bounds ×
`Screen.devicePixelRatio`, recomputed only when the window size or device pixel ratio changes.

**Acceptance:** Using `preview_service_test_access.h` (or a spy on the requested size), navigating
with Quick Look open across 600×400, 400×600 and 1000×200 images leaves the QuickLook
requested size unchanged; resizing the window changes it.

#### REQ-F-018 — Unwanted: requested size feedback
**Unwanted behaviour:** If the fitted frame size changes, then the system shall not call
`setRequestedSize` as a consequence.

**Acceptance:** A call counter on `setRequestedSize(QuickLook, …)` (via test access or a QML
spy wrapper) does not increase across the navigation in REQ-F-017.

---

## Non-Functional Requirements

#### REQ-NF-001 — No binding loops
**Ubiquitous:** Card and frame geometry shall be computed from bounds, `sourcePixelSize`, entry
kind and content implicit sizes only, never from the card's or frame's own resulting size.

**Acceptance:** A test installs a message handler, opens Quick Look, navigates with `j`/`k`
across image, text, directory and error entries and resizes the window; zero messages contain
`"Binding loop detected"`.

#### REQ-NF-002 — Fractional-DPR crispness (manual)
**Ubiquitous:** The card, image clip and caption shall render without blur at device pixel ratio 1.5.

**Acceptance:** On the user's Hyprland display at scale 1.5, the user confirms crisp card and
image corners, sharp caption text, centered caption, and a dimmed listing behind the card.

#### REQ-NF-003 — Open latency unchanged
**Ubiquitous:** Opening Quick Look shall remain within the existing smoke budget.

**Acceptance:** The existing `smoke.cpp` assertion `EXPECT_LT(quickLookMs, 200)` still passes.

---

## Constraints

#### REQ-C-001 — Key handling unchanged
**Ubiquitous:** Key routing (`InspectionKeys.js`: Space/Esc close, j/k navigate, Ctrl+O/Ctrl+I
history, Space auto-repeat suppression) shall remain unchanged.

**Acceptance:** `git diff` shows no change to `apps/files/InspectionKeys.js` or
`DirectoryController::handleKey`; existing tests in `smoke.cpp`,
`history_navigation_window_test.cpp`, `preview_integration_test.cpp` and
`directory_controller_test.cpp` pass unmodified in their key-routing assertions.

#### REQ-C-002 — Test object names preserved
**Ubiquitous:** The objectNames `quickLookOverlay`, `quickLookContent`, `quickLookImageArea` and
`quickLookText` shall remain on elements with the same roles (popup, key-handling content item,
image frame, text view).

**Acceptance:** `grep` finds each objectName exactly once in `QuickLookOverlay.qml`; the smoke
tests that `findChild` them pass.

#### REQ-C-003 — No holonight-qt or decode-pipeline changes
**Ubiquitous:** The change shall not modify the `holonight-qt` repository nor the
`PreviewService` / `ThumbnailService` / `TextPreviewService` decode logic.

**Acceptance:** `git -C ../holonight-qt status --porcelain` is empty; `git diff` over
`apps/files/*service*.{h,cpp}` is empty, except test-access additions if needed.

#### REQ-C-004 — Closing policy
**Ubiquitous:** The popup `closePolicy` shall remain `Popup.NoAutoClose`.

**Acceptance:** Clicking the backdrop with Quick Look open leaves `controller.quickLookOpen` true.

#### REQ-C-005 — New QML files registered for format checks
**Where** a new `.qml` file is added, it shall be added to `Taskfile.yml` and
`check-qml-format.sh` format lists.

**Acceptance:** `task format-check` and `task qml-lint` pass and list every new `.qml` file.

---

## Non-Goals

- Zoom, pan, rotation, slideshow.
- EXIF or other extended metadata in Quick Look (the info sidebar owns it).
- New key bindings or click-outside-to-close.
- Resize/open animations.
- Changes to `PreviewPane.qml`, `holonight-qt`, or the decode pipeline.

## Test fixtures

Existing helpers in `tests/preview_fixtures.h` (`renderJpegBytes(QSize)`, `writeSmallText`,
`writeLargeText`, `writeCorruptJpeg`, `writeBrokenSymlink`, `writeRandomBinary`) and
`tests/preview_service_test_access.h`. No checked-in binaries.

## Review remediation acceptance

Resize a 1280×800 window to 640×420 while the next image job is blocked: the frame,
filename, metadata and hint remain inside the card and window. Restore the original
window size while still blocked: the retained geometry returns to its original size.
Existing unchanged-window pending navigation assertions must continue to pass.
