# Mode Status Badge — Verification

2026-09-17. Review remediation aligns REQ-C-003 with the implemented Control,
translates the accessible-name template while preserving literal mode labels,
and reopens native acceptance checks without recorded evidence. The user
requested these review fixes. Spark delegation was attempted but its configured
model was unavailable; the main agent completed the change.

## Automated evidence

- `task deps`: passed; providers were built and installed under build/ without
  modifying sibling sources.
- `task build`: passed (debug).
- `task test`: all eight badge smoke tests passed. The sandbox run failed only
  FileOperationService.SocketAndDeviceCopiesAreRejectedWithoutReading because
  binding a local socket returned permission denied (484 passed, two opt-in
  tests skipped, one failed). The expanded-permission `task check` rerun passed
  all eight CTest targets, including the previously blocked test.
- `task check`: passed in full with expanded permissions: debug/release builds,
  all eight CTest targets, formatting, clang-tidy, QML lint, REUSE, staged
  installation and uninstall checks. Native inspection and rendered-directory
  performance remain opt-in skips.
- `git diff --check`: passed after the review edits.

Logs are under `build/mode-status-badge-{deps,build,test,check}.log`.

| Requirements | Coverage |
| --- | --- |
| F-001–003, C-002–004 | Files.ModeBadgeNormalMode checks first-child placement, visibility, font, palette fill and text; VisualMode, SearchMode and InsertMode check each mode. Source inspection confirms a Control with HnLabel content and Rectangle background, and untranslated label literals. |
| F-004 | NormalMode checks the text color token; DESIGN.md records default-dark contrast calculations. Native legibility remains pending. |
| F-005, F-007, NF-001 | Files.ModeBadgeWidthConstant checks mode updates, width/x stability, centering and Control radius. Native fractional-scale rendering remains pending. |
| F-006 | Files.ModeBadgeVisibleDuringPrompt checks trash-confirm visibility and dismissal. Busy-operation and conflict-prompt badge checks are not separately automated. |
| F-008 | Files.ModeBadgeRemovesPrefixes checks selection count, insert hint and validation error. |
| F-009 | Shared badgeShows assertions check accessible names in all four modes; NormalMode checks StaticText role. The former planned standalone AccessibleName test is covered by these assertions. Inspector acceptance remains pending. |
| F-010 | Files.ModeBadgeNoMouseOrFocus checks click/focus behavior and subsequent keyboard navigation. |
| NF-002–003 | Full quality gate and smoke-suite results are recorded above. |
| C-001 | Production changes remain in ModeStatusBar.qml; tests and supporting documentation are permitted. |

## Pending acceptance

- [ ] Native Hyprland at 1.5×: crisp pill, vertical centering, legibility in all
  modes, no mode-switch jitter, monospace labels and contextual prefix removal
  (SPEC manual checklist; T-009).
- [ ] Accessibility inspector: verify StaticText role and correct announced
  name in all four modes (REQ-F-009; T-010).

These checks have no recorded native observations and remain open. Offscreen
smoke tests do not close them. The SPEC manual checklist is intentionally
unchecked. Contrast evidence covers only the default dark scheme; the light
scheme limitation remains documented in DESIGN.md.
