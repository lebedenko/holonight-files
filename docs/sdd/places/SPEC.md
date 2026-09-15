# Places sidebar requirements

Status: Approved by the supplied implementation plan (2026-09-15).

- P-001: At startup, Files shall list nonempty QStandardPaths Home, Documents,
  Downloads, Pictures, Music and Videos locations in that order with translated
  labels, retaining configured locations that do not exist.
- P-002: When exactly ~/Projects is a directory at startup, Files shall append
  Projects; otherwise it shall omit it. No live discovery shall occur.
- P-003: Each place shall expose an iconName candidate chain and render a theme
  icon with the bundled folder fallback on failure.
- P-004: The sidebar shall show a muted Places heading, compact shared delegates,
  centered icons, token spacing and inset selection, preserving its width and
  separator and supporting scrolling in short windows.
- P-005: A row shall be highlighted exactly when its path equals currentPath.
- P-006: When activated in NORMAL mode without a task prompt or Quick Look,
  a row shall call DirectoryController.open, preserving errors, history, preview
  updates and return of listing focus. Otherwise activation shall be disabled.
- P-007: Places shall have accessible names, Tab focus, Up/Down row navigation,
  and Enter/Space activation.

Non-goals: bookmarks/persistence, folder creation, Trash, Devices, Network,
project discovery, sibling changes. Shared controls/tokens determine dimensions.
