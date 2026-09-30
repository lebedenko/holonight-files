# Fast fuzzy finding — verification

Date: 2026-09-30

The approved implementation plan introduced Jump to Directory (`Ctrl+G`) and Find File
(`Ctrl+P`) with Home and Current Folder roots, background scanning, session cache,
ranked results and navigation. The existing `/` listing search remains available.

## Automated evidence

- The focused `PathMatcher.*`, `PathFinderModel.*` and `FuzzyMatcher.*` run passed
  all 18 tests. The `VimModeController.*` run passed all 18 tests, including listing
  search behavior.
- Debug and Release builds passed. The Release staged installation and offscreen
  launch check passed.
- Formatting, QML lint/import policy and metadata checks, REUSE lint, and the
  repository-wide C++ static analysis passed.
- A 2,500-file temporary fixture measured a 20–42 ms cold scan and a 10–11 ms
  cached query across local runs. This fixture is too small and uniform to
  establish performance on a real home tree.
- The aggregate `task check` was not run because its test suite includes automated
  pointer/focus-dependent UI interaction, which this repository prohibits.

## Native acceptance and open findings

The user confirmed that the popup UI, root buttons, keyboard handling and file
selection work as expected on the native desktop.

The user also reported two major shortcomings: a weak matching algorithm and poor
performance. `lambda audit` returns no intended paths because the matcher treats
the entire query as one ordered subsequence. The 2,500-file timing does not
represent a two-million-path workload. These are **known failures**, so this
cycle is closed as a UI prototype, not an accepted search implementation. Its
popup, keys, root buttons, and navigation are retained for shared-engine testing.
The separate ranking and performance follow-ups are superseded by the
[shared search adoption](../shared-search-adoption/README.md) cycle.
