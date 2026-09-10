# Vim Modal Editing — Stage 3 SPEC

## Context

HoloNight Files is a keyboard-driven, Vim-style-modal file manager for navigating, inspecting, and managing files from the keyboard. Stages 1–2 established folder browsing and safe inspection preview capabilities. This Stage 3 specification formalizes a Vim-style modal editing state machine (NORMAL/VISUAL/SEARCH/INSERT modes) that complements the existing NORMAL-mode motion and Quick Look overlay. The implementation uses synchronous filesystem operations; async machinery is deferred to Stage 4.

## Explicit Non-Goals

The following capabilities are explicitly OUT OF SCOPE for this stage:

1. **COMMAND mode** — No `:` command palette, no command-line invocation model. This cycle implements only NORMAL, VISUAL, SEARCH, and INSERT modes.
2. **VISUAL mode operations** — No delete, copy, move, yank, or any filesystem action consumed by a VISUAL selection. Selection display and range navigation only.
3. **Quick Look overlay refactoring** — The existing Quick Look overlay (toggled via Space) remains an orthogonal overlay flag. Do not implement a generalized "overlay stack" abstraction; Quick Look's existing Escape handling is preserved as-is.
4. **Permission pre-checks** — Users are never blocked from entering INSERT mode; permission failures are detected and surfaced only at COMMIT time (Enter).
5. **Async file operations** — All filesystem operations (mkdir, touch, rename) are synchronous direct calls. Async TaskManager machinery is Stage 4 scope (copy, move, trash, delete).
6. **fzf-style live filtering** — SEARCH mode is a jump-to-match, not a live-filtered directory listing. The full directory listing remains visible throughout.
7. **Auto-open on Search Enter** — Confirming a search (Enter) leaves the cursor at the matched entry and returns to NORMAL mode; it does NOT open or act on that entry.

## Requirements

### REQ-F-001: Mode State Machine Initialization

**EARS:** Ubiquitous

The system shall initialize with the modal state set to NORMAL upon application startup and upon returning to the main directory listing (after Quick Look closes, after any INSERT/SEARCH/VISUAL operation completes).

**Acceptance Criterion:** Verify that `DirectoryController` or integrated `VimModeController` reports mode NORMAL when the application starts and when the directory listing gains focus after Quick Look closes, via instrumentation (logging, unit test, or manual verification with mode displayed in status bar).

---

### REQ-F-002: Mode State Representation

**EARS:** Ubiquitous

The system shall maintain a discrete, explicit mode state (NORMAL, VISUAL, SEARCH, INSERT) accessible to UI layers and keyboard handlers.

**Acceptance Criterion:** The mode state is queryable via a public method or property (e.g., `VimModeController::currentMode()` or `getCurrentMode()`) that returns an enum or equivalent unambiguous value. At least one UI component (e.g., status bar or logging) reads and displays this state.

---

### REQ-F-003: NORMAL Mode Escape — Quick Look Closure

**EARS:** Event-driven

When the user presses Escape while in NORMAL mode with Quick Look overlay open, the system shall close the Quick Look overlay without altering the cursor position or sort order, and remain in NORMAL mode.

**Acceptance Criterion:** Open Quick Look (Space), press Escape, verify Quick Look is closed, cursor is at the same entry it was before Space was pressed, and no sort order change occurred. Mode remains NORMAL.

---

### REQ-F-004: NORMAL Mode Escape — Fullscreen Compatibility When Overlay Absent

**EARS:** Event-driven

When the user presses Escape while in NORMAL mode with no overlay open, the system shall retain the existing fullscreen exit behavior; cursor, sort order, and mode remain unchanged.

**Acceptance Criterion:** In NORMAL mode with Quick Look closed, press Escape; verify no cursor movement, no sort change, no mode change, and fullscreen exits when active. Repeat outside fullscreen to confirm no other effect.

---

### REQ-F-005: Window Shortcuts Gated to NORMAL Mode

**EARS:** State-driven

Given the system is in NORMAL mode, the system shall allow window-level Shortcut handlers (F for fullscreen, Q for quit) to fire. Given the system is in VISUAL, SEARCH, or INSERT mode, the system shall suppress these Shortcuts (prevent them from firing), allowing typed keys to be interpreted as mode-specific input without leaking into app-level actions.

**Acceptance Criterion:** (1) In NORMAL mode, press F; verify fullscreen toggle fires. Press Q; verify quit dialog or action fires. (2) In VISUAL mode, press F; verify fullscreen does NOT fire and F is consumed as a VISUAL/NORMAL key or no-op. Do the same for Q. Repeat for SEARCH and INSERT modes. Verify no window-level actions fire in non-NORMAL modes.

---

### REQ-F-006: INSERT Mode Entry via `i` (Prepend)

**EARS:** Event-driven

When the user presses `i` while in NORMAL mode with a single entry selected, the system shall enter INSERT mode, make the current entry's name editable in place, and position the text cursor at the beginning of the name (before the first character).

**Acceptance Criterion:** Select an entry (e.g., "example.txt"), press `i`, verify INSERT mode is active, the name is editable in a text field, and the cursor is positioned before the first character (before "e" in "example.txt"). Typing "new" should result in "newexample.txt" before Enter.

---

### REQ-F-007: INSERT Mode Entry via `I` (Prepend, Synonym)

**EARS:** Event-driven

When the user presses `I` while in NORMAL mode with a single entry selected, the system shall behave identically to pressing `i`: enter INSERT mode, make the name editable in place, and position the cursor at the beginning of the name.

**Acceptance Criterion:** Select an entry, press `I` (capital i), verify INSERT mode activates and cursor is at the beginning of the name, identical to the `i` behavior. Verify that pressing `i` and `I` on the same entry produce indistinguishable starting states.

---

### REQ-F-008: INSERT Mode Entry via `a` (Append)

**EARS:** Event-driven

When the user presses `a` while in NORMAL mode with a single entry selected, the system shall enter INSERT mode, make the current entry's name editable in place, and position the text cursor at the end of the name (after the last character).

**Acceptance Criterion:** Select an entry (e.g., "example.txt"), press `a`, verify INSERT mode is active, the name is editable, and the cursor is positioned after the last character (after "t" in "example.txt"). Typing "1" should result in "example.txt1" before Enter.

---

### REQ-F-009: INSERT Mode Entry via `A` (Append, Synonym)

**EARS:** Event-driven

When the user presses `A` while in NORMAL mode with a single entry selected, the system shall behave identically to pressing `a`: enter INSERT mode, make the name editable in place, and position the cursor at the end of the name.

**Acceptance Criterion:** Select an entry, press `A` (capital a), verify INSERT mode activates and cursor is at the end of the name, identical to the `a` behavior.

---

### REQ-F-010: INSERT Mode Entry via `o` (Create Below)

**EARS:** Event-driven

When the user presses `o` while in NORMAL mode, the system shall enter INSERT mode with a new blank/placeholder editable row inserted immediately below the current cursor position.

**Acceptance Criterion:** With cursor at line 5, press `o`, verify INSERT mode is active and a new empty editable row appears at line 6 (pushing previous line 6 to line 7). The new row's text field is ready for input. Pressing Escape discards the row; pressing Enter creates a file or directory based on the name.

---

### REQ-F-011: INSERT Mode Entry via `O` (Create Above)

**EARS:** Event-driven

When the user presses `O` while in NORMAL mode, the system shall enter INSERT mode with a new blank/placeholder editable row inserted immediately above the current cursor position.

**Acceptance Criterion:** With cursor at line 5, press `O`, verify INSERT mode is active and a new empty editable row appears at line 5 (shifting the original line 5 to line 6). The new row's text field is ready for input.

---

### REQ-F-012: INSERT Mode — Rename Commit (Unchanged Name)

**EARS:** Event-driven

When the user presses Enter in INSERT mode after editing an existing entry's name without changing it (e.g., selecting "readme.md", pressing `i`, making no edits, pressing Enter), the system shall perform a touch-equivalent filesystem operation (update modification time only) and return to NORMAL mode without renaming the entry.

**Acceptance Criterion:** Select "readme.md" with mtime T1, press `i`, press Enter immediately (no edits), verify mtime is updated to T2 (T2 >= T1), the entry name remains "readme.md", and mode returns to NORMAL. Use a file listing or stat tool to confirm mtime changed.

---

### REQ-F-013: INSERT Mode — Rename Commit (Changed Name)

**EARS:** Event-driven

When the user presses Enter in INSERT mode after editing an existing entry's name and changing it (e.g., "old.txt" → "new.txt"), the system shall rename the entry to the new name and return to NORMAL mode.

**Acceptance Criterion:** Select "old.txt", press `i`, delete all characters, type "new.txt", press Enter, verify the entry is now named "new.txt" in the directory listing and mode returns to NORMAL.

---

### REQ-F-014: INSERT Mode — Rename Cancel (Escape)

**EARS:** Event-driven

When the user presses Escape in INSERT mode while renaming an existing entry (via `i`, `I`, `a`, or `A`), the system shall discard all edits, leave the entry name unchanged, and return to NORMAL mode without any filesystem operation.

**Acceptance Criterion:** Select "original.txt", press `i`, type "modified.txt", press Escape, verify the entry is still named "original.txt" in the listing, no rename occurred, and mode returns to NORMAL.

---

### REQ-F-015: INSERT Mode — Create Commit (Directory with Trailing Slash)

**EARS:** Event-driven

When the user presses Enter in INSERT mode after creating a new entry (via `o` or `O`) with a name ending in a single `/` character (e.g., "newfolder/"), the system shall create a new directory with the name minus the trailing `/` (e.g., "newfolder") and return to NORMAL mode.

**Acceptance Criterion:** Press `o`, type "newfolder/", press Enter, verify a new directory named "newfolder" (without the trailing slash) appears in the listing at the expected position, and mode returns to NORMAL.

---

### REQ-F-016: INSERT Mode — Create Commit (Regular File)

**EARS:** Event-driven

When the user presses Enter in INSERT mode after creating a new entry (via `o` or `O`) with a name NOT ending in `/` (e.g., "newfile.txt"), the system shall create a regular empty file and return to NORMAL mode.

**Acceptance Criterion:** Press `o`, type "newfile.txt", press Enter, verify a new regular file (not a directory) named "newfile.txt" appears in the listing and mode returns to NORMAL.

---

### REQ-F-017: INSERT Mode — Create Cancel (Escape)

**EARS:** Event-driven

When the user presses Escape in INSERT mode while creating a new entry (via `o` or `O`), the system shall discard the placeholder row, perform no filesystem operation, and return to NORMAL mode.

**Acceptance Criterion:** Press `o`, type "temp.txt", press Escape, verify the temporary row disappears from the listing, no file is created, and mode returns to NORMAL.

---

### REQ-F-018: VISUAL Mode Toggle via `v`

**EARS:** Event-driven

When the user presses `v` while in NORMAL mode, the system shall enter VISUAL mode with the current entry marked as the first selected item.

**Acceptance Criterion:** In NORMAL mode, press `v`, verify mode changes to VISUAL, the current entry is visually marked as selected (e.g., highlighted differently or marked with a selection indicator).

---

### REQ-F-019: VISUAL Mode Toggle via `V` (Synonym)

**EARS:** Event-driven

When the user presses `V` (capital v) while in NORMAL mode, the system shall behave identically to pressing `v`: enter VISUAL mode with the current entry marked as the first selected item.

**Acceptance Criterion:** In NORMAL mode, press `V` (capital v), verify mode changes to VISUAL and the current entry is marked as selected, identical to the `v` behavior.

---

### REQ-F-020: VISUAL Mode Exit via Escape

**EARS:** Event-driven

When the user presses Escape while in VISUAL mode, the system shall clear the selection, return to NORMAL mode, and leave the cursor at the last-selected entry.

**Acceptance Criterion:** Enter VISUAL mode, select multiple entries via motion keys (e.g., `3j` to extend selection), press Escape, verify the selection is cleared (no entries highlighted), mode returns to NORMAL, and cursor is at the last-selected entry.

---

### REQ-F-021: VISUAL Mode — Motion-Based Selection Extension

**EARS:** Event-driven

When the user presses motion keys (j, k, gg, G, or count-prefixed motions like `5j`) while in VISUAL mode, the system shall extend or shrink the selection range to include entries from the original first-selected entry to the current cursor position (following Vim selection semantics), without leaving VISUAL mode.

**Acceptance Criterion:** Enter VISUAL with cursor at entry 5, press `3j` (move to entry 8), verify entries 5–8 are all selected. Press `1k` (move to entry 7), verify entries 5–7 are selected (shrink the range). Remain in VISUAL throughout.

---

### REQ-F-022: VISUAL Mode Selection Counter

**EARS:** Ubiquitous

The system shall display a live counter indicating the number of currently-selected entries while in VISUAL mode (e.g., "3 selected" in the status bar or mode indicator).

**Acceptance Criterion:** Enter VISUAL mode, select 1 entry; status bar shows "1 selected". Extend to 5 entries; status bar updates to "5 selected". Exit VISUAL; counter disappears or becomes inactive.

---

### REQ-F-023: VISUAL Mode — No Operations This Stage

**EARS:** Ubiquitous

The system shall NOT implement delete, copy, move, yank, or any filesystem action triggered by or consuming a VISUAL selection in this stage.

**Acceptance Criterion:** In VISUAL mode with multiple entries selected, press common operation keys (dd, yy, x, d, c, m) — none shall have any observable effect on the selection or filesystem. These operations are logged as out-of-scope (Stage 4) if pressed, or are no-ops.

---

### REQ-F-024: SEARCH Mode Entry via `/`

**EARS:** Event-driven

When the user presses `/` while in NORMAL mode, the system shall enter SEARCH mode and present an editable search query input field (e.g., a text input or inline search box in the status bar).

**Acceptance Criterion:** In NORMAL mode, press `/`, verify SEARCH mode is active and a search input field appears and is ready for text input (cursor in the field, blinking, or otherwise indicating focus).

---

### REQ-F-025: SEARCH Mode — Live Fuzzy Matching with Highlighting

**EARS:** Event-driven

When the user types characters in the SEARCH query field, the system shall live-jump the cursor to the entry with the best fuzzy-match score against the query, highlight the matched substring(s) in that entry's name, and update both as the query changes character by character.

**Acceptance Criterion:** Enter SEARCH (`/`), type "ex" (for an entry "example.txt"), cursor jumps to "example.txt" and the "ex" prefix is highlighted. Append "a" to make the query "exa", cursor may jump to a different entry if a better match exists, highlight updates. Delete the "a" to return to "ex"; cursor and highlighting revert accordingly.

---

### REQ-F-026: SEARCH Mode — Cycle Forward via `n`

**EARS:** Event-driven

When the user presses `n` in NORMAL mode after committing a search with Enter, the system shall advance the cursor to the next entry that matches the active search query (with fuzzy scoring), wrapping to the first match if the cursor is currently at the last match.

**Acceptance Criterion:** Enter SEARCH, type "e", verify cursor is at the first match (e.g., "example.txt"). Press Enter, then `n`; cursor advances to the next entry matching "e" (e.g., "file.exe"). Continue pressing `n` until the last match is reached, then press `n` once more; verify the cursor wraps to the first match (matching vim's `/` + `n` wrap-around behavior).

---

### REQ-F-027: SEARCH Mode — Cycle Backward via `N`

**EARS:** Event-driven

When the user presses `N` (capital n) in NORMAL mode after committing a search with Enter, the system shall advance the cursor to the previous entry that matches the active search query (in reverse order), wrapping to the last match if the cursor is currently at the first match.

**Acceptance Criterion:** Enter SEARCH, type "e", press Enter, move forward via `n` to a middle match, press `N`, cursor moves to the previous match. Continue pressing `N` until the first match is reached, then press `N` once more; verify the cursor wraps to the last match.

---

### REQ-F-028: SEARCH Mode Commit (Enter)

**EARS:** Event-driven

When the user presses Enter while in SEARCH mode, the system shall confirm the search, leave the cursor at the currently-matched entry, return to NORMAL mode, and close the search input field (without opening or acting on the matched entry).

**Acceptance Criterion:** Enter SEARCH, type "ex", cursor is at "example.txt", press Enter, verify mode returns to NORMAL, cursor remains at "example.txt", search input closes, and no open/action occurs on "example.txt".

---

### REQ-F-029: SEARCH Mode Cancel (Escape)

**EARS:** Event-driven

When the user presses Escape while in SEARCH mode, the system shall cancel the search, restore the cursor to the filename selected before `/` was pressed (or a clamped original row if that entry disappeared), return to NORMAL mode, and close the search input field.

**Acceptance Criterion:** In NORMAL mode with cursor at entry 3, press `/`, type "e", cursor jumps to entry 7 (a match), press Escape, verify cursor is back at entry 3, mode is NORMAL, and search input is closed.

---

### REQ-F-030: INSERT Mode — Leading `./` Autocorrection

**EARS:** Event-driven

When the user enters a name starting with `./` in INSERT mode (rename or create), the system shall silently treat this as equivalent to the same name without `./` and perform no error or warning; the committed name shall not contain the leading `./`.

**Acceptance Criterion:** Enter INSERT via `i` or `o`, type "./example.txt", press Enter, verify the committed name is "example.txt" (without the `./`), no error is shown, and the file/directory is created or renamed correctly.

---

### REQ-F-031: INSERT Mode — Reject `../` in Name

**EARS:** Event-driven

When the user enters a name containing `../` anywhere (leading or embedded) in INSERT mode, the system shall display a live validation error in the status bar and recolor the text input to indicate an error state.

**Acceptance Criterion:** Enter INSERT via `i` or `o`, type "../danger", verify the status bar immediately displays a message indicating parent directory traversal is not allowed (e.g., "Parent directory access not permitted"), the text input turns red or displays an error indicator, and Enter is blocked (no operation occurs). Pressing Escape returns to NORMAL without side effects.

---

### REQ-F-032: INSERT Mode — Reject Embedded `/` (Not Trailing)

**EARS:** Event-driven

When the user enters a name containing an embedded `/` character anywhere except as a single trailing `/` (which marks a directory) in INSERT mode, the system shall display a live validation error and recolor the input to indicate an error state.

**Acceptance Criterion:** Enter INSERT via `i` or `o`, type "foo/bar", verify status bar shows "Nested entries cannot be created", input is recolored to error state, and Enter is blocked. Type "dir/" (single trailing slash); no error. Correct "foo/bar" to "foo-bar"; error clears and Enter is allowed.

---

### REQ-F-033: INSERT Mode — Reject Empty or Whitespace-Only Names

**EARS:** Event-driven

When the user attempts to commit a name consisting of zero characters or only whitespace (spaces, tabs, newlines) in INSERT mode, the system shall display a live validation error and recolor the input.

**Acceptance Criterion:** Enter INSERT via `i` or `o`, delete all characters (or type only spaces), press Enter, verify status bar displays an error (e.g., "Name cannot be empty"), input is recolored, and no operation occurs. Type a non-whitespace character; error clears.

---

### REQ-F-034: INSERT Mode — Reject Literal `.` and `..`

**EARS:** Event-driven

When the user attempts to commit the literal name `.` or `..` in INSERT mode, the system shall display a live validation error and recolor the input.

**Acceptance Criterion:** Enter INSERT via `i` or `o`, type ".", press Enter, verify status bar shows an error (e.g., "Reserved name"), input is recolored, and no operation occurs. Do the same for "..". Type "abc"; error clears.

---

### REQ-F-035: INSERT Mode — Reject Names Exceeding Filesystem Max Length

**EARS:** Event-driven

When the user enters a name that exceeds the filesystem's maximum filename length (typically 255 bytes on ext4/NTFS/most systems) in INSERT mode, the system shall display a live validation error and recolor the input.

**Acceptance Criterion:** Enter INSERT via `i` or `o`, type a name longer than 255 characters (or the system's limit), verify status bar displays an error (e.g., "Name too long"), input is recolored, and Enter is blocked. Delete characters to fall under the limit; error clears and Enter is allowed.

---

### REQ-F-036: INSERT Mode — Reject Invalid Characters

**EARS:** Event-driven

When the user enters a name containing invalid byte content for a Linux filesystem (a null byte, or other non-printable control characters), the system shall display a live validation error with a generic message and recolor the input.

**Acceptance Criterion:** Enter INSERT via `i` or `o`, type a name containing a null byte or control character, verify status bar displays "Invalid file name" (or similar generic message), input is recolored to error state, and Enter is blocked. Enter a valid character; error clears.

---

### REQ-F-037: INSERT Mode — Reject Collision with Existing Entry (for Create)

**EARS:** Event-driven

When the user presses Enter in INSERT mode to create a new entry (via `o` or `O`) with a name that collides with an existing entry in the current directory, the system shall display a live validation error and recolor the input, blocking the create operation.

**Acceptance Criterion:** Directory contains "existing.txt". Press `o`, type "existing.txt", press Enter, verify status bar displays an error (e.g., "Name already exists"), input is recolored, no file is created, and mode remains INSERT. Type "new.txt"; error clears and Enter creates the file.

---

### REQ-F-038: INSERT Mode — Reject Collision with Existing Entry (for Rename, Excluding Self)

**EARS:** Event-driven

When the user presses Enter in INSERT mode to rename an entry (via `i`, `I`, `a`, or `A`) with a new name that collides with a DIFFERENT existing entry in the current directory, the system shall display a live validation error and recolor the input, blocking the rename operation.

**Acceptance Criterion:** Directory contains "file1.txt" (selected) and "file2.txt". Enter INSERT via `i`, type "file2.txt", press Enter, verify status bar displays an error (e.g., "Name already exists"), input is recolored, no rename occurs, and mode remains INSERT. Edit the name to "file1.txt" (its own name, unchanged); error clears and Enter performs a touch (no rename, mtime update only). Edit to "file3.txt"; error clears and rename succeeds.

---

### REQ-F-039: INSERT Mode — No Permission Pre-Checks

**EARS:** Ubiquitous

The system shall allow users to enter INSERT mode (`i`, `I`, `a`, `A`, `o`, `O`) regardless of the current directory's read/write permissions. Permission failures are detected and surfaced only at COMMIT time (when Enter is pressed).

**Acceptance Criterion:** In a read-only directory, press `i`, `a`, or `o`; verify INSERT mode is entered without blocking and the name input appears. Type a valid name, press Enter; if the directory is read-only, the status bar displays a permission error (e.g., "Permission denied") and the operation does not complete. Exit to NORMAL mode without side effects.

---

### REQ-F-040: INSERT Mode — Permission Failures at Commit Time

**EARS:** Event-driven

When a filesystem operation (mkdir, touch, rename) fails at COMMIT time in INSERT mode due to permission errors (e.g., EACCES), the system shall display a permission-specific error message in the status bar and remain in INSERT mode, allowing the user to cancel (Escape) or retry (edit and Enter again).

**Acceptance Criterion:** Attempt to create a file in a read-only directory; press Enter, verify status bar displays "Permission denied" (or similar), mode remains INSERT, and the file is not created. User can press Escape to exit without side effects, or edit the name and retry.

---

### REQ-F-041: Synchronous Filesystem Operations

**EARS:** Constraint

All filesystem operations in this stage (mkdir, touch, rename) shall be implemented via direct, synchronous calls (e.g., `QDir::mkdir()`, `QFile::open()`, `QFile::rename()`) and shall NOT use async TaskManager, FileOperationService, or any queued/deferred operation machinery.

**Acceptance Criterion:** Code review confirms all file operations in INSERT mode commit paths are synchronous blocking calls, not async queue operations. No usage of TaskManager or operation services for mkdir/touch/rename in this stage.

---

### REQ-F-042: Live Validation Feedback Display

**EARS:** Ubiquitous

During INSERT mode, the system shall display live validation feedback in a status bar or contextual UI element (exact rendering location is a Design-stage concern, not Spec-stage), showing error messages as the user types and recoloring the text input to indicate valid vs. invalid states.

**Acceptance Criterion:** Open INSERT mode, type an invalid name (e.g., a name with `../`), verify within 200ms the status bar displays an error message, the input is recolored to an error color (e.g., red), and the color reverts to normal when the name becomes valid. No error delays of more than 200ms.

---

### REQ-NF-001: Validation Latency

**EARS:** Constraint

Live validation during INSERT mode (error detection and display) shall complete within 200 milliseconds of the user typing each character, to provide immediate visual feedback.

**Acceptance Criterion:** Measure latency between keystroke and status bar update (error message appearance or removal) and input recoloring. All samples shall be under 200ms. Repeat with 20+ distinct validation errors (embedded `/`, collision detection, etc.) to ensure consistency.

---

### REQ-C-001: Mode State Dispatch Point Integration

**EARS:** Constraint

The new VimModeController (or mode state machine) shall integrate with the existing `DirectoryController::handleKey()` dispatch point. Window-level Shortcuts (F, Q) shall gate their fire conditions to NORMAL mode via the mode controller.

**Acceptance Criterion:** Code review confirms VimModeController queries are used in the Shortcut handlers for F and Q, blocking them in non-NORMAL modes. `DirectoryController::handleKey()` is either extended to route non-NORMAL-mode keys through mode-specific handlers, or a new mode dispatcher layer calls mode-specific handlers based on current mode.

---

### REQ-C-002: Backward Compatibility with Stage 1–2 Keybindings

**EARS:** Constraint

Existing NORMAL-mode keybindings from Stages 1–2 (motion keys j/k/gg/G, sort toggle, hidden-files toggle, Quick Look Space) shall remain unchanged and functional in NORMAL mode.

**Acceptance Criterion:** Manual smoke test: j/k/gg/G motions work as before, sort order toggle (existing binding) works, hidden-files toggle works, Space opens Quick Look in NORMAL mode, all existing keybindings still dispatch through `DirectoryController::handleKey()` or equivalent.

---

### REQ-C-003: Quick Look Orthogonality (No Refactoring)

**EARS:** Constraint

The existing Quick Look overlay shall remain an orthogonal overlay flag (`quick_look_open_` or equivalent). Do NOT implement a generalized overlay stack, modal layering system, or refactor Quick Look's Escape handling into a mode-driven overlay abstraction.

**Acceptance Criterion:** Code review confirms Quick Look's open/close logic and Escape handling remain in their current location and mechanism, no new overlay stack classes or abstractions are introduced, and the mode controller does NOT manage Quick Look's state.

---

### REQ-C-004: Stage 4 Deferral — VISUAL Mode Operations

**EARS:** Constraint

No delete, copy, move, yank, or other filesystem operation shall be triggered by, consume, or depend on a VISUAL-mode selection in this stage. These operations are deferred to Stage 4 (TaskManager-driven machinery).

**Acceptance Criterion:** Code review and testing confirm that pressing any operation key (dd, yy, d, c, x, m, p, etc.) in VISUAL mode has no effect on the filesystem or clipboard/register state, and no task is enqueued.

---

### REQ-C-005: Stage 4 Deferral — Async File Operations

**EARS:** Constraint

No async operation service (TaskManager, FileOperationService, work queue, or similar) shall be used for INSERT-mode file operations (mkdir, touch, rename) in this stage. All operations shall be synchronous.

**Acceptance Criterion:** Code review confirms no TaskManager usage, no operation-queue calls, and no async patterns in the INSERT-mode commit paths (the code paths that execute on Enter for rename/create).

---

### REQ-C-006: No Command Mode

**EARS:** Constraint

No command mode (`:` command palette, command-line input, or command execution) shall be implemented in this stage.

**Acceptance Criterion:** Pressing `:` in NORMAL mode is a no-op (no command palette appears, no mode change). No code for parsing/executing colon-prefixed commands exists.

---

## Summary

This specification formalizes the Vim modal editing state machine for Stage 3 of HoloNight Files, covering four modes (NORMAL, VISUAL, SEARCH, INSERT) with their entry/exit conditions, mode-specific behaviors, and integration constraints. Live validation, synchronous filesystem operations, and backward compatibility with Stages 1–2 are ensured via explicit requirements and acceptance criteria. Key non-goals (COMMAND mode, VISUAL operations, Quick Look refactoring, permission pre-checks, async machinery) are listed to avoid scope creep. Implementation of these requirements unlocks the next stage (Stage 4: TaskManager-driven copy/move/trash/delete operations).

## Review remediation acceptance

- REQ-R-001: When creating an entry, the system shall reject existing files, directories,
  and symlinks including dangling links. Exclusive creation shall preserve contents if a
  collision occurs between validation and execution. Failures retain INSERT text and placeholder.
- REQ-R-002: When committing an unchanged name, the system shall update only mtime of
  the file, directory, or symlink itself, preserving atime and symlink targets. Missing
  sources shall fail without recreation; operation errors shall distinguish collisions,
  permissions, missing entries, and other failures using translated messages.
- REQ-R-003: While INSERT is active, the system shall suspend model deliveries and
  sort/filter changes. Initial and refresh scans cannot shift or remove the editor.
  Commit/cancel removes placeholders while suspended and starts one fresh diff refresh;
  cancellation and queued deliveries shall not prevent shutdown.
- REQ-R-004: Before folder navigation, the system shall cancel unfinished edits without
  committing, remove placeholders, clear VISUAL/search/chords, load the destination,
  and restore listing focus.
- REQ-R-005: The system shall place o/O placeholders adjacent to the exact anchor in
  both sort directions, across directory/file boundaries and collator ties, and in empty listings.
- REQ-R-006: SEARCH shall accept literal n/N. NORMAL repetition shall recompute committed
  matches after row/name/layout/filter changes. Navigation clears remembered searches.
  Escape restores pre-search filename identity, or a clamped original row if missing.
  SEARCH highlights current match character runs as literal filename text, retaining
  metadata, accessibility, clipping, and inline editing.

Verification includes at least 20 timed validation edits against 200 ms and modal
captures in dark/light themes at normal/fractional scales. Native checks are recorded
separately and remain pending when unavailable.
