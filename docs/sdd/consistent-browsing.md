# Consistent browsing contract

Vim editing and search remain application-owned. Selection membership, ranges and marks delegate to the browser session. Copy, cut, trash and drag use the same set, with cursor fallback when empty. Only the grid adds empty-space rectangles and edge scrolling; item dragging remains file transfer. PreviewPane binds the shared browser information body.

See the umbrella docs/initiatives/consistent-file-browsing.md for interaction and dependency contracts. Historical designs remain historical.

## Verification

Selection highlights observe the shared change notifier, including membership replacements with an unchanged count. The footer reserves a trailing, right-aligned count slot so selecting or clearing entries does not shift the layout. Focused controller, operation, list/grid, gutter, icon, preview and footer regressions passed. The user confirmed all native manual checks passed.

The host-native publication gate passed both staged and relocated prefixes, including provider inspection, unknown-size rejection, required consumer lanes, passive observation and rendered previews.

Container publication acceptance passed all 24 CTest checks, formatting, static analysis, QML import/type/lint checks and REUSE licensing checks.
