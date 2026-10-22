# Directional panel focus

Window-scoped Ctrl+H/J/K/L shortcuts reuse historyNavigationEnabled: NORMAL mode,
no task prompt, finder or Quick Look. Determine the source through active focus item
ancestry, recognizing listing delegates, sidebar children and breadcrumb buttons.
Preview and footer are excluded. Explicit routes only change focus; they preserve
path, cursors, selection and pending commands. Startup listing focus, Tab traversal,
plain Vim keys and navigation focus restoration retain their existing behavior.
InspectionKeys consumes reserved Ctrl+H/J/K/L chords when routing is disabled,
preventing them from becoming plain Vim movement. The window prompt filter also
consumes them before interpreting prompt answers.

DirectoryListing.focusListing targets its actual ListView. AppHeaderBar delegates
to [HnBreadcrumbBar.focusCurrentSegment](../../../holonight-qt/docs/sdd/directional-panel-focus.md),
which focuses the final button with keyboard focus indication without activation.

Headless window tests cover the complete route table, repeated transitions, state
preservation, root/collapsed paths and modal guards. Provider smoke tests cover the
API and no-ops. Build against an isolated provider prefix and run sibling acceptance.
Native routing and focus outlines are checked manually; never automate native input.

Suggested commit explanation: feat(navigation): add directional panel focus shortcuts.
