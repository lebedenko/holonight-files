# Breadcrumb geometry

The shared HnBreadcrumbBar owns the only visible frame and all internal vertical
geometry. It exposes a normalized sizeRole, defaulting to Normal (32 px field,
28 px buttons), and caps buttons at availableHeight. Xs uses 24 px buttons.
Wrappers, labels and separators stay vertically centered in both path layouts.
Files uses a plain positioning Item and Normal inside its unchanged 42 px header,
with 5 px top and bottom padding around the 32 px field;
horizontal offsets and original activation indices remain unchanged.

Build holonight-qt first into an isolated prefix, then build Files against it.
Accept default/Xs/explicit-height containment and centering within 0.5 px,
expanded/collapsed and editable/read-only states, existing navigation checks,
and dark/light fractional-scale renders. Native visual checks are manual.

See the [cross-project contract](../../../docs/breadcrumb-geometry.md).
