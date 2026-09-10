# SDD Tasks — vim-modal-editing

- [x] T-001: VimModeController skeleton with mode enum and Q_PROPERTY plumbing
  - REQs: REQ-F-001, REQ-F-002
  - Check: VimModeController instantiates with currentMode property returning NORMAL on startup, and the property is queryable from C++ and QML.

- [x] T-002: DirectoryController::handleKey() two-way dispatcher with F/Q/: gating
  - REQs: REQ-C-001, REQ-F-005, REQ-C-006
  - Check: In NORMAL mode, F and Q Shortcut handlers fire; in VISUAL/SEARCH/INSERT modes, F and Q Shortcut handlers are suppressed, and pressing `:` in NORMAL mode is a no-op.

- [x] T-003: NameValidator helper with ordered validation checks
  - REQs: REQ-F-030, REQ-F-031, REQ-F-032, REQ-F-033, REQ-F-034, REQ-F-035, REQ-F-036, REQ-F-037, REQ-F-038
  - Check: validateName() runs all ordered checks in specified order, returns valid=true for correct names and valid=false with appropriate error messages for each rule violation.

- [x] T-004: FuzzyMatcher helper with fzf-style scoring and position highlighting
  - REQs: REQ-F-025
  - Check: fuzzyMatch() scores query against candidate with consecutive-run, word-boundary, and gap-penalty bonuses, and returns matched character positions for highlighting.

- [x] T-005: VISUAL mode entry and exit (v/V/Escape)
  - REQs: REQ-F-018, REQ-F-019, REQ-F-020
  - Check: Pressing v or V enters VISUAL with current entry marked; pressing Escape clears selection and returns to NORMAL with cursor at last selected entry.

- [x] T-006: VISUAL mode motion-based selection extension and counter
  - REQs: REQ-F-021, REQ-F-022
  - Check: Motion keys (j/k/gg/G) in VISUAL extend selection range to cursor position; status bar displays "N selected" counter that updates with each motion.

- [x] T-007: VISUAL mode operation keys produce no observable effect
  - REQs: REQ-F-023, REQ-C-004
  - Check: In VISUAL mode with multiple entries selected, pressing operation keys (dd, yy, x, d, c, m) has no effect on filesystem or selection, and no task is enqueued.

- [x] T-008: INSERT mode entry via i/I (prepend to cursor position)
  - REQs: REQ-F-006, REQ-F-007
  - Check: Pressing i or I on a selected entry enters INSERT mode with inline editable text field and cursor positioned before first character.

- [x] T-009: INSERT mode entry via a/A (append to end)
  - REQs: REQ-F-008, REQ-F-009
  - Check: Pressing a or A on a selected entry enters INSERT mode with cursor positioned after last character of the name.

- [x] T-010: INSERT mode entry via o/O (create below/above with placeholder row)
  - REQs: REQ-F-010, REQ-F-011
  - Check: Pressing o inserts editable placeholder row immediately below cursor; O inserts above; existing rows shift accordingly; new row displays empty editable text field.

- [x] T-011: INSERT mode commit/cancel for rename (unchanged/changed/escape)
  - REQs: REQ-F-012, REQ-F-013, REQ-F-014
  - Check: Unchanged name calls touch (mtime updates, no rename); changed name renames file; pressing Escape discards all edits and returns to NORMAL.

- [x] T-012: INSERT mode commit/cancel for create (directory/file/escape)
  - REQs: REQ-F-015, REQ-F-016, REQ-F-017
  - Check: Name ending in "/" creates directory (slash stripped); name without "/" creates empty file; pressing Escape removes placeholder and returns to NORMAL without filesystem operation.

- [x] T-013: Status bar display with NameValidator live feedback and error recoloring
  - REQs: REQ-F-042, REQ-NF-001, REQ-F-030, REQ-F-031, REQ-F-032, REQ-F-033, REQ-F-034, REQ-F-035, REQ-F-036, REQ-F-037, REQ-F-038
  - Check: Status bar shows validation error messages within 200ms of typing; inline editor text input recolors to error state when invalid and reverts to normal when valid.

- [x] T-014: Permission failure handling at INSERT mode commit time
  - REQs: REQ-F-039, REQ-F-040
  - Check: INSERT entry is allowed in read-only directory; pressing Enter shows "Permission denied" error in status bar and remains in INSERT; user can Escape or retry.

- [x] T-015: SEARCH mode entry with query input field
  - REQs: REQ-F-024
  - Check: Pressing "/" in NORMAL mode enters SEARCH mode and displays editable search query input field in status bar ready for text input.

- [x] T-016: SEARCH mode live fuzzy matching and highlighting
  - REQs: REQ-F-025
  - Check: Typing characters in SEARCH query live-jumps cursor to best fuzzy match with matched substrings highlighted; cursor and highlighting update character by character.

- [x] T-017: SEARCH mode cycle forward/backward with wraparound (n/N)
  - REQs: REQ-F-026, REQ-F-027
  - Check: Pressing n advances to next fuzzy-matching entry with wraparound to first at end; N reverses to previous entry with wraparound to last at beginning.

- [x] T-018: SEARCH mode commit and cancel (Enter/Escape)
  - REQs: REQ-F-028, REQ-F-029
  - Check: Pressing Enter confirms search, returns to NORMAL at matched entry without opening it; Escape cancels search and restores cursor to pre-search position.

- [x] T-019: NORMAL mode Escape handling (Quick Look closure / fullscreen exit)
  - REQs: REQ-F-003, REQ-F-004
  - Check: Escape with Quick Look open closes overlay without altering cursor/sort; Escape without overlay retains fullscreen exit and leaves cursor/sort/mode unchanged.

- [x] T-020: Synchronous filesystem operations verification (no async machinery)
  - REQs: REQ-F-041, REQ-C-005
  - Check: Code review confirms all INSERT-mode file operations (mkdir, touch, rename) use direct synchronous Qt/Linux filesystem calls with no TaskManager or async queue usage.

- [ ] T-021: Backward compatibility smoke test for Stage 1-2 keybindings
  - REQs: REQ-C-002, REQ-C-003
  - Check: Manual test confirms j/k/gg/G motions work unchanged, sort toggle works, hidden-files toggle works, Space opens Quick Look, all existing keybindings dispatch correctly through DirectoryController.

- [x] T-022: Project verify sequence (build, format, lint, tidy, test)
  - REQs: All (implicit coverage of all requirements)
  - Check: Project build succeeds, code format checks pass, QML lint passes, clang-tidy passes without exemptions, test suite passes with no failures.

## Approved review remediation

- [x] R-001 Filesystem safety and failure tests (REQ-R-001/002).
- [x] R-002 Model suspension, navigation cleanup and lifecycle tests (REQ-R-003/004).
- [x] R-003 Placeholder comparator and proxy regressions; spark assignment, main-agent quota fallback (REQ-R-005).
- [x] R-004 Search revision, identity restoration and literal highlighting (REQ-R-006).
- [x] R-005 QML focus/keyboard, 20-edit timing and themed scale captures (REQ-NF-001, REQ-R-006).
- [x] R-006 Required project checks and accurate evidence/docs (all requirements).

Completed tasks are backed by the regression and capture evidence in VERIFICATION.md.
T-021 retains its explicit manual acceptance gate; native IME/accessibility and the full
native theme/scale matrix remain pending separately from passing automated checks.
