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

## Directional panel focus

In NORMAL mode, when no prompt, finder or Quick Look is open:

| Focus | Ctrl+H | Ctrl+J | Ctrl+K | Ctrl+L |
|---|---|---|---|---|
| File listing | Places | Stay | Breadcrumbs | Stay |
| Places | Stay | Stay | Breadcrumbs | File listing |
| Breadcrumbs | Places | File listing | Stay | Stay |

Breadcrumb entry always focuses the last segment (current directory). These keys
only move focus; preview and footer are excluded. The listing retains startup
focus, Tab traversal remains available, and SEARCH retains its footer input.

Plain `l` activates the sidebar cursor, like Enter. In breadcrumbs, plain `h`/`l`
moves keyboard focus left/right through visible segments and the ellipsis button,
without navigation or wrapping. Enter activates the focused breadcrumb.
See [panel keyboard navigation](sdd/panel-key-navigation.md) for scope and deferred
Tab/Backtab design work.
