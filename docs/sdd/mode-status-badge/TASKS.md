# SDD Tasks — mode-status-badge

Acceptance remains open. See [verification evidence and pending checks](VERIFICATION.md).

- [x] T-001: Badge Control, modeMeta array, and hidden metric in ModeStatusBar.qml
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-007, REQ-NF-001, REQ-C-001, REQ-C-002, REQ-C-003, REQ-C-004
  - Check: ModeStatusBar.qml contains Control with objectName "modeBadge" as first RowLayout child, a modeMeta array mapping modes to label/color pairs, and a hidden HnLabel metric for constant width.

- [x] T-002: Remove VISUAL/INSERT prefixes from footer status labels
  - REQs: REQ-F-008
  - Check: visualStatusLabel rawText removes "VISUAL  ·  " prefix and insertStatusLabel rawText removes "INSERT  ·  " prefix from their source bindings.

- [x] T-003: Per-mode smoke tests verifying text and fill color
  - REQs: REQ-F-002, REQ-F-003, REQ-NF-003
  - Check: test_ModeBadge_NormalMode, VisualMode, InsertMode, SearchMode all pass, each asserting correct 6-char label text and matching palette fill color.

- [x] T-004: Width/x constancy and same-turn mode update test
  - REQs: REQ-F-005, REQ-NF-001
  - Check: test_ModeBadge_WidthConstant passes, cycling through all four modes and asserting badge width and x-position are identical and update occurs in same event-loop turn.

- [x] T-005: Prompt visibility test
  - REQs: REQ-F-006
  - Check: test_ModeBadge_VisibleDuringPrompt passes, verifying badge remains visible and displays NORMAL mode while trash-confirm prompt is open.

- [x] T-006: Prefix removal and label content test
  - REQs: REQ-F-008
  - Check: test_ModeBadge_RemovesPrefixes passes, asserting visualStatusLabel contains selection count without prefix and insertStatusLabel contains hint text without prefix.

- [x] T-007: Accessibility, no-mouse/focus, and centering/radius tests
  - REQs: REQ-F-004, REQ-F-007, REQ-F-009, REQ-F-010
  - Check: Files.ModeBadgeNormalMode, the shared badgeShows assertions, Files.ModeBadgeWidthConstant and Files.ModeBadgeNoMouseOrFocus pass; accessible name updates with mode, badge ignores mouse clicks, accepts no focus, is vertically centered, and uses HnSurfaceRole.Pill radius.

- [x] T-008: Build and code quality verification
  - REQs: REQ-NF-002
  - Check: task deps, task build, task test and the full task check gate pass; record commands, results and limitations in VERIFICATION.md.

- [ ] T-009: Manual native Hyprland @1.5 visual verification (manual)
  - REQs: REQ-F-004, REQ-F-007
  - Check: On Hyprland @1.5 DPR, pill renders crisp and vertically centered, text contrast against all four mode fills meets WCAG AA ≥4.5:1, and no jitter occurs across four-mode cycle.

- [ ] T-010: Native accessibility-inspector verification (manual)
  - REQs: REQ-F-009
  - Check: Confirm StaticText role and the correct accessible name in all four modes using an accessibility inspector; record observations in VERIFICATION.md.
