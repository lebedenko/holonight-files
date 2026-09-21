# Design

Use the shared separator contract; no application-local rendering helper. Boundary owners select
alignment. Layouts use implicit logical occupancy. Preserve unrelated work and raw dividers outside
this migration. See the umbrella [initiative](../../../../docs/initiatives/crisp-separator-joins/README.md).

Files: Main's header bottom, content top and sidebar top share a layout boundary; content bottom
and footer top share another. The column-header bottom rule owns each T junction. Vertical rules
end at that rule's top, whose default height is the logical physical thickness. Column labels
supply x positions only. No arbitrary endpoint correction or alpha compensation is needed.


Headers paint above adjacent content backgrounds (`z: 1`). At DPR 1.5625 a background otherwise occluded
part of a correctly snapped shared edge. This is boundary ownership in stacking order; there are no
thickness, opacity or endpoint-offset compensations. The column header does not tightly clip its snapped
bottom rule; labels already elide within their cells.

The integration test loads the actual Main item tree, changes window widths through 1000/850/740/420/1000,
and compares complete junction neighborhoods to independently snapped public item boundaries. A background
capture and half-opacity white strokes expose gaps and duplicate blending. The GPU harness redirects that
item tree to an explicit OpenGL FBO because fractional offscreen QWindow capture buffers are unreliable.
The backend and DPR are asserted and software OpenGL fallback is rejected. Engine warnings fail the test
on binding loops. See [verification and launch evidence](VERIFICATION.md).
