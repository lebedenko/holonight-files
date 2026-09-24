# Shared SVG support — Design

Add SVG dispatch in preview_service.cpp before the generic image branch. Keep inspection and rendering inside
runPreviewJob on the verified QFile. Extend PreviewResult/PreviewService state with kind and QSizeF document size.
Use shared svgPixelSize consistently for viewport requirements, dispatched sizes and resize adequacy; avoid fitting
an already rounded bound twice. Keep generation/revision suppression, DPR request inputs and debounce unchanged.

Extend thumbnail_service to use a versioned SVG policy marker and existing tiers/atomic PNG writes. Validate original
SVG resources before both worker memory lookup and disk lookup. Keep source geometry out of rendered-pixel adequacy.
Use explicit Disabled animation, output-byte policy and translated unsupported-resource errors. Skip the EXIF stage.

Implementation uses published/pinned provider 3da5f4e51fe2eed9a1bd9f72c0ab523aa57a5ceb.

## Implementation files

`preview/image_policy.h`, `preview/preview_service.{h,cpp}`, `preview/thumbnail_service.{h,cpp}`,
`presentation/quick_look_presentation_model.{h,cpp}` and preview/Quick Look QML; regressions in
`preview_service_test.cpp`, `thumbnail_service_test.cpp` and `quick_look_presentation_model_test.cpp`.
