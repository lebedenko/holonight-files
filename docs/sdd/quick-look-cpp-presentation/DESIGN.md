# Quick Look C++ presentation design

Approved implementation plan: 2026-09-16. REQ-001–008.

`QuickLookPresentationModel` is a QML-creatable QObject implementing QQmlParserStatus,
owned by QuickLookOverlay. It takes a guarded PreviewService pointer, windowSize,
devicePixelRatio, cardPadding, captionReserve, frameCaptionGap, minCompactWidth,
iconExtent and hintImplicitWidth. Outputs are Kind (None/Pending/Image/Text/Compact),
cardSize and frameSize. Retained kind/source dimensions are private. The shared
PreviewService and DirectoryController acquire no card-layout responsibilities.

Input setters recompute geometry synchronously. Preview changed signals recompute
classification and geometry only. All output fields are assigned before notifications.
QML construction suppresses decode requests until componentComplete; native instances
are active immediately. Request calculation uses window bounds and caption measurements,
never fitted output dimensions. Cache the last submitted pixel size per attached service;
submit only positive changed sizes. The test-only request counter is private with a
friend access helper. No timers or animations are introduced.

QPointer plus explicit changed/destroyed connection management handle replacement and
destruction. Both reset retained geometry inputs and request deduplication state. A
service's clear() instead preserves layout inputs, matching the previous absent rule.

Port the formulas verbatim, including compact frame icon extent even when the card is
clamped, aspect fitting from source pixels, floor of logical dimensions and rounding
of physical request pixels. Nonpositive/nonfinite measurements are normalized to zero;
invalid DPR becomes 1. Geometry inputs remain independent of output bindings.

QML owns the model and binds inputs from theme tokens, label measurements, overlay
size and Screen DPR. Popup/frame sizes and compact icon kind use model outputs.
Remove geometry handlers/functions and QuickLookGeometry.js resource registration.
Retain caption formatting, keyboard handling, visual object names and focus behavior.

Direct gtests cover arithmetic, classification, retention, notifications, request sizing
and lifetime. Existing rendered smoke tests cover integration. Replace their QML request
counter reads with model test access; no production test-only QML property is exposed.
