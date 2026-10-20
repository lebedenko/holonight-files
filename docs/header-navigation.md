# Design: header navigation and listing alignment

Files owns history eligibility and window shutdown. Normal mode without a task
prompt, Quick Look or finder enables historyNavigationEnabled; buttons, shortcuts,
mouse handling and direct traversal share this decision. Buttons and mouse presses
navigate once through goBack/goForward and discard pending counts. The owning
window event filter consumes Back/Forward press/release without moving focus.

AppHeaderBar accepts the actual listingPanel item. Its ancestor x coordinates and
width drive the breadcrumb frame, including the line-number gutter, without a
second sidebar-width formula. Ancestor coordinates are explicit binding dependencies
because mapToItem alone does not observe their movement. The breadcrumb clips long
collapsed paths inside its frame. Header height and breadcrumb vertical metrics stay fixed.

The header requests go-previous/go-next theme icons and uses
Holonight.Controls.HnHeaderCloseButton at the right with a 12 px outer inset.
closeRequested connects to window.close(), preserving shutdown coordination and
mouse focus behavior. No application-owned navigation or close artwork remains.

Build Icons and Qt into an isolated prefix before consumers; see the umbrella
header-navigation contract. Regressions cover mouse regions, boundaries, counts,
modal/editing gates, listing focus, breadcrumb edges at multiple window/sidebar/
preview widths, and close activation. Physical mouse buttons, split dragging and
light/dark fractional-scale parity remain manual native acceptance.
