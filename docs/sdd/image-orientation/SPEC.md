# Image orientation specification

Approved by the user-provided implementation plan. Baseline: `152cb98078e973fce74e020fd0e3b63bb4b5f046`.

- R1: When Files previews an original image, it shall apply intrinsic EXIF orientation, including all mirrored values 1–8, matching Viewer.
- R2: When image dimensions are published, `sourcePixelSize` shall contain full-resolution oriented width × height for the pane and Quick Look, including after metadata completion.
- R3: When decoding an image, Files shall preserve aspect fit, no upscaling, resource limits, verified descriptors, asynchronous scheduling and stale-result rejection.
- R4: When reading a disk thumbnail, Files shall require `Files::OrientationPolicy=applied-v1` in addition to existing metadata and resolution validation. Missing or different markers shall cause a miss, including external thumbnails.
- R5: When regenerating thumbnails, Files shall atomically replace only the requested tier with oriented pixels and the marker. Cached PNG decoding shall ignore intrinsic orientation. Cache-write failure shall still return decoded pixels.

Scope is Files only. Provider APIs/revisions, Viewer, QML rotation, color management, codecs, animation, shared caches, CI infrastructure and umbrella pins are excluded. Publication needs separate authorization.
