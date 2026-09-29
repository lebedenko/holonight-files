# Design

PreviewPane owns a retained QImage value in local presentation state. PreviewImageItem
uses it while transitioning, otherwise uses the current service image. The service's
selectionChanged signal precedes property notifications with the new coherent state
already readable. Its handler resets the existing icon timer, drops pixels on clear,
failure or synchronous fallback, and starts a fade only if retained pixels remain and
no fade is running. Subsequent navigation cannot restart an active fade.

The existing changed signal synchronizes fresh pixels at full opacity, including partial
thumbnail delivery while metadata is still busy. Same-selection updates and resizing never
start an animation. A 120 ms NumberAnimation with InQuad easing targets only the image's
opacity. NumberAnimation keeps the interruptible opacity value on the QML object, making
rapid-navigation continuity observable in deterministic presentation regressions. Completion,
interruption and effective visibility loss stop the animation and release the retained image.
The painted item's existing square layout, fitting and rounded clipping are unchanged.

Fallback timer expiry also synchronizes presentation so all fallback tiers discard retained
pixels immediately. Filename and metadata remain live service bindings. No worker, shared
primitive, API, setting or Quick Look changes are needed.
