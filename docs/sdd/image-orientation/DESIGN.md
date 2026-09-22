# Design

Use the installed HolonightImages provider unchanged. Original inspection uses `Apply` and `Inspection::orientedSize`; original bounded decoding uses `Apply`. The private decoding helper requires an explicit policy; cached PNG inspection and decoding use `Ignore` because their pixels are already oriented.

The existing `sourcePixelSize` contract is clarified as full-resolution dimensions after intrinsic orientation. Existing QML dimension labels and aspect-ratio bindings consume it without presentation changes. Memory caching, request identity, scheduling and metadata publication remain unchanged.

Add the versioned orientation marker to the existing PNG metadata validation and atomic QSaveFile path. Reject legacy or external unmarked entries lazily; do not scan or delete caches. Existing URI, mtime, size, revision and resolution checks remain in force.

Tests use generated asymmetric four-color JPEG quadrants, with explicit independent corner permutations for EXIF 1–8. Check rectangular bounds, small originals, all tiers, migration and disk reuse. Exercise oriented dimensions through asynchronous pane/Quick Look upgrades and metadata completion, retaining existing safety regressions.
