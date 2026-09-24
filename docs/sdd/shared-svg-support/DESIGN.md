# Shared SVG support — Design

Add SVG dispatch in preview_service.cpp before the generic image branch. Keep inspection and rendering inside
runPreviewJob on the verified QFile. Extend PreviewResult/PreviewService state with kind and QSizeF document size.
Use shared svgPixelSize consistently for viewport requirements, dispatched sizes and resize adequacy; avoid fitting
an already rounded bound twice. Keep generation/revision suppression, DPR request inputs and debounce unchanged.

Extend thumbnail_service to use a versioned SVG policy marker and existing tiers/atomic PNG writes. Validate original
SVG resources before both worker memory lookup and disk lookup. Keep source geometry out of rendered-pixel adequacy.
Use explicit Disabled animation, output-byte policy and translated unsupported-resource errors. Skip the EXIF stage.

Implementation may begin only after SVG-001 is published and pinned. No consumer implementation has started.
