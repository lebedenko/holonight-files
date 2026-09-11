# Stage 4: File Operations — Requirements Specification (EARS Format)

## Overview

Stage 4 introduces core file-manipulation operations to HoloNight Files: copying, moving, trashing, creating directories and files, and renaming. These operations are invoked via Vim-modal keybindings (yy/dd/y/d/p/D) integrated into the existing VimModeController state machine and executed sequentially through a new TaskManager subsystem. Operations that involve I/O (copy, move, trash) are handled asynchronously in a worker thread; fast synchronous operations (mkdir, touch, rename) remain unchanged from Stage 3. Conflict resolution and trash confirmation are surfaced inline in the existing ModeStatusBar.qml. Cross-filesystem moves are transparently detected and fall back to copy-then-delete. Trash strictly follows the freedesktop.org Trash specification.

## Scope

### In Scope

**Keybindings & Operations:**
- `yy` (NORMAL mode): copy the item under the cursor to the clipboard register.
- `dd` (NORMAL mode): cut (move) the item under the cursor to the clipboard register with a cut flag.
- `y` (VISUAL mode, single keypress): copy the entire VISUAL selection to the register, then exit VISUAL mode.
- `d` (VISUAL mode, single keypress): cut the entire VISUAL selection to the register, then exit VISUAL mode.
- `p` (NORMAL mode): paste the register's contents (copy or cut) into the currently displayed directory.
- `D` (NORMAL mode or VISUAL mode): trash the item under the cursor (NORMAL) or the entire VISUAL selection (VISUAL) to freedesktop Trash.
- mkdir, touch, rename: remain synchronous (Stage 3 implementation unchanged); no new requirements here except noting they are out of scope for TaskManager integration.

**Clipboard Register:**
- Exactly one register (not named, not stacked, no history).
- Holds a list of file paths and a copy-vs-cut flag.
- A new yy/dd/y/d operation silently overwrites the previous register contents.

**Conflict Resolution (Paste-Time Name Collisions):**
- Inline, blocking prompt in ModeStatusBar.qml (no modal overlay).
- Options: skip, overwrite, auto-rename (append " (2)", " (3)", etc. before extension), cancel (abort remaining queue).
- No "apply to all" batch option; every conflict resolved individually.

**Trash Confirmation:**
- Unconditional one-key (y/n) prompt in ModeStatusBar for every trash operation (single item or full VISUAL selection).
- Hardcoded on; no v1 toggle or config.

**Task Execution & Queue:**
- Sequential (one task at a time, no parallelism).
- Copy, move, and trash operations pass through TaskManager with visible progress.
- Mkdir, touch, rename remain synchronous and outside TaskManager.
- Cancellation via Ctrl+C only (cancels current task, drops queue remainder).
- Escape declines trash confirmation and leaves conflicts unresolved; outside prompts it retains its existing mode behavior.

**Progress Reporting:**
- Inline in ModeStatusBar: operation type, current file, N/M items or progress indicator.
- No modal or overlay progress UI.

**Partial Failure Handling:**
- Multi-item operations skip failed items and continue; final summary reports success/failure counts and identifies failed items with reasons.

**Cross-Filesystem Move:**
- Detect EXDEV (rename() fails across filesystems); transparently fall back to copy-then-delete-source.
- Use identical progress/conflict/cancellation machinery — no user-visible difference except naturally longer duration.

**Symlink Handling:**
- Copy or move duplicates/relocates the symlink itself (preserves target string), never dereferences.
- Equivalent to `cp -P` / `mv` behavior.

**Trash Implementation:**
- Strictly follows the *full* freedesktop.org Trash specification, not just the home-trash subset.
- Home trash: files moved to `$XDG_DATA_HOME/Trash/files/` with metadata in `$XDG_DATA_HOME/Trash/info/`; metadata includes original absolute path and ISO 8601 deletion timestamp.
- Per-partition trash: for files on a filesystem other than `$XDG_DATA_HOME`'s, uses `$topdir/.Trash/$uid` (if `$topdir/.Trash` exists, isn't a symlink, and has the sticky bit set) or `$topdir/.Trash-$uid` (created on demand) instead, with a relative (not absolute) `Path` in `.trashinfo`.
Permanent deletion is unavailable. Trash failures leave sources untouched (REQ-F-044); REQ-F-049/050 are retired.
- Creates all Trash directories with correct permissions if missing.

### Out of Scope

- Permanent/unrecoverable delete (any form).
- Undo of any operation.
- Restoring items from trash via the app UI.
- Named/multiple clipboard registers.
- "Apply to all" / batch conflict resolution.
- Concurrent/parallel task execution.
- Configurable/toggleable trash confirmation.
- Drag-and-drop file operations.
- New progress/conflict overlay UI.

## Non-Goals

This stage explicitly does not include, and these decisions are firm (not deferred):

Permanent deletion is unavailable. Trash failures leave sources untouched (REQ-F-044); REQ-F-049/050 are retired.
- **Undo/Redo:** Undo of copy/move/trash/mkdir/rename is explicitly out of scope, not merely deferred to v2. This is a deliberate design choice: HoloNight Files is a navigation-focused tool, not a transaction-aware editor. Users manage undo at the filesystem level (backups, VCS, system snapshots).
- **Trash restoration:** Restoring items from trash is expected to happen outside HoloNight Files (e.g., via a standard freedesktop Trash manager). The app creates `.trashinfo` metadata for spec compliance, but does not offer a restore UI.
- **Named registers:** Vim's `"a`, `"b`, etc. register style is out of scope. Single, unnamed register only.
- **Batch conflict resolution:** No "apply to all" or "skip all" option during multi-file conflicts. Each conflict is individually resolved.
- **Parallel/concurrent operations:** Only one task in flight at a time. Sequential queue only.
- **Trash confirmation toggle:** Confirmation is unconditionally on for all trash operations. No config or command-line flag to disable.
- **Drag-and-drop:** This is a keyboard-driven tool. D&D is out of scope.
- **New UI overlays:** Conflict resolution and progress must be surfaced in the existing ModeStatusBar.qml, not in new modal dialogs or overlays.

## Functional Requirements

### Clipboard & Register Management

**REQ-F-001: Yank (Copy) in NORMAL Mode**

The system shall, when the user presses `yy` in NORMAL mode, copy the file path of the item under the cursor to the internal clipboard register with a copy flag.

Acceptance Criterion: A test that positions the cursor on a file, presses `yy`, verifies the register contains that file path with copy flag set, then navigates to a different directory and presses `p`; the file is copied (not moved) to the new location with the original remaining in place.

---

**REQ-F-002: Yank (Copy) in VISUAL Mode**

The system shall, when the user presses `y` (single keypress, not doubled) in VISUAL mode, copy the file paths of all items in the active VISUAL selection to the register with a copy flag, then exit VISUAL mode back to NORMAL.

Acceptance Criterion: A test that selects multiple files in VISUAL mode (using existing VISUAL keybindings), presses `y`, verifies the register contains all selected paths with copy flag, the mode is NORMAL, and a subsequent `p` copies all items.

---

**REQ-F-003: Cut (Move) in NORMAL Mode**

The system shall, when the user presses `dd` in NORMAL mode, store the file path of the item under the cursor in the clipboard register with a cut flag.

Acceptance Criterion: A test that positions the cursor on a file, presses `dd`, verifies the register contains that file path with cut flag set, navigates to a different directory and presses `p`; the file is moved (not copied) to the new location, and the original is no longer present.

---

**REQ-F-004: Cut (Move) in VISUAL Mode**

The system shall, when the user presses `d` (single keypress, not doubled) in VISUAL mode, store the file paths of all items in the active VISUAL selection in the register with a cut flag, then exit VISUAL mode back to NORMAL.

Acceptance Criterion: A test that selects multiple files in VISUAL mode, presses `d`, verifies the register contains all selected paths with cut flag, the mode is NORMAL, and a subsequent `p` moves all items; originals are no longer present.

---

**REQ-F-005: Register Overwrite on New Yank/Cut**

The system shall silently overwrite the clipboard register when the user performs a new `yy`, `dd`, `y`, or `d` operation, with no warning or confirmation.

Acceptance Criterion: A test that performs `yy` on file A, then `yy` on file B (without pasting); verifies the register now contains only file B, and `p` pastes only file B.

---

**REQ-F-006: Register State Persistence Across Navigation**

The system shall retain the clipboard register's contents and flags (copy vs. cut) as the user navigates between directories without pasting.

Acceptance Criterion: A test that performs `yy` on a file, navigates to a different directory, performs other operations (ls, sort, search), then presses `p`; the original file is still pasted into the new directory.

---

### Copy & Move Operations

**REQ-F-007: Paste into Current Directory (Copy)**

The system shall, when the user presses `p` in NORMAL mode and the register contains a copy flag, queue a copy operation to transfer the files in the register to the currently displayed directory.

Acceptance Criterion: A test in a source directory (e.g., /tmp/src) with a yy-copied file, navigate to a destination directory (/tmp/dst), press `p`, verify the copied file appears in /tmp/dst and the original remains in /tmp/src.

---

**REQ-F-008: Paste into Current Directory (Cut/Move)**

The system shall, when the user presses `p` in NORMAL mode and the register contains a cut flag, queue a move operation to transfer the files in the register to the currently displayed directory and clear the register.

Acceptance Criterion: A test that performs `dd` on a file, navigates to another directory, presses `p`, verifies the file is moved to the new directory, the original location no longer contains it, and the register is cleared (subsequent `p` has no effect).

---

**REQ-F-009: Destination is Current Display Directory, Not Cursor Item**

The system shall paste into the currently displayed directory, regardless of what item (if any) is under the cursor—even if the cursor is on a subdirectory.

Acceptance Criterion: A test that navigates into a nested directory structure (e.g., /a/b/c), yy-copies a file, navigates to /a/b, positions the cursor on a subdirectory (e.g., "x"), presses `p`; the file is pasted into /a/b, not into /a/b/x.

---

**REQ-F-010: Multi-File Copy/Move Queuing**

The system shall accept a paste operation (`p`) containing multiple files and queue them as a single task to be executed sequentially.

Acceptance Criterion: A test that selects 3 files in VISUAL mode, presses `y`, navigates to another directory, presses `p`, verifies all 3 files appear in the destination and the operation completes as one queued task (visible as one entry in task progress, or one operation summary).

---

**REQ-F-011: Recursive Directory Copy**

The system shall, when copying a directory, recursively copy all contents (subdirectories, files, metadata) to the destination.

Acceptance Criterion: A test creates a nested directory tree with multiple files at different levels, yy-copies the root directory, pastes it elsewhere, verifies the entire tree structure and all files appear at the destination with the same relative paths.

---

**REQ-F-012: Symlink Copy Behavior**

The system shall, when copying a symlink, duplicate the link itself (preserving its target string) rather than dereferencing and copying the target's content.

Acceptance Criterion: A test creates a symlink pointing to /path/to/target, yy-copies the symlink, pastes it into another directory, verifies the new symlink points to the same target string (even if that target no longer exists or is unreachable).

---

**REQ-F-013: Symlink Move Behavior**

The system shall, when moving a symlink, relocate the link itself (preserving its target string) rather than dereferencing the target.

Acceptance Criterion: A test creates a symlink pointing to a file, dd-cuts it, navigates to another directory, presses `p`, verifies the symlink appears in the new directory pointing to the same target; the original symlink is gone.

---

### Trash (Delete to Trash) Operations

**REQ-F-014: Trash Single Item in NORMAL Mode**

The system shall, when the user presses `D` in NORMAL mode on the item under the cursor, prompt the user for confirmation (one-key y/n), and upon yes, move the item to the freedesktop Trash.

Acceptance Criterion: A test positions the cursor on a file, presses `D`, a one-key prompt appears in ModeStatusBar (y to confirm, n to cancel), user presses `y`, file is removed from the current directory and appears in `$XDG_DATA_HOME/Trash/files/` with corresponding `.trashinfo` metadata.

---

**REQ-F-015: Trash VISUAL Selection in VISUAL Mode**

The system shall, when the user presses `D` on the VISUAL selection, prompt once for confirmation, and upon yes, move all items in the selection to Trash.

Acceptance Criterion: A test selects 3 files in VISUAL mode, presses `D`, a single one-key prompt appears, user presses `y`, all 3 files are moved to Trash and removed from the current directory.

---

**REQ-F-016: Trash Confirmation is Mandatory**

The system shall always prompt for trash confirmation (y/n) before moving an item to Trash, regardless of whether the selection contains one item or many.

Acceptance Criterion: A test performs `D` on a single file; a confirmation prompt appears. Then performs `D` on a VISUAL selection of 5 files; a confirmation prompt still appears (not skipped or auto-confirmed).

---

**REQ-F-017: Trash Cancellation (Decline Confirmation)**

The system shall, when the user presses `n` or any key other than `y` in a trash confirmation prompt, cancel the operation without modifying any files.

Acceptance Criterion: A test performs `D` on a file, prompt appears, user presses `n`, file remains in the current directory and does not appear in Trash.

---

**REQ-F-018: Freedesktop Trash Specification Compliance**

The system shall move trashed files to `$XDG_DATA_HOME/Trash/files/` and create corresponding metadata files in `$XDG_DATA_HOME/Trash/info/` following the freedesktop.org Trash specification, including original absolute path and ISO 8601 deletion timestamp.

Acceptance Criterion: A test trashes a file, verifies:
1. The file appears under `$XDG_DATA_HOME/Trash/files/`.
2. A `.trashinfo` metadata file exists in `$XDG_DATA_HOME/Trash/info/` with:
   - `[Trash Info]` header.
   - `Path=<original-absolute-path>` (URL-encoded per spec).
   - `DeletionDate=<ISO8601-timestamp>`.
3. The metadata format exactly matches the freedesktop.org specification.

---

**REQ-F-019: Trash Directory Creation**

The system shall create `$XDG_DATA_HOME/Trash/files/` and `$XDG_DATA_HOME/Trash/info/` directories with correct permissions (as per freedesktop spec) if they do not exist when a trash operation first runs.

Acceptance Criterion: A test in a clean environment (redirected `$XDG_DATA_HOME` via fixture), performs a trash operation, verifies both Trash subdirectories are created with permissions 0700 (owner-only read/write/execute).

---

**REQ-F-020: Preserve File Metadata on Trash**

The system shall preserve file metadata (modification time, permissions—where applicable on the target filesystem) when moving files into the Trash, as required by the freedesktop Trash spec.

Acceptance Criterion: A test creates a file with specific mtime/permissions, trashes it, verifies the trashed file's metadata matches the original (or is preserved according to the spec's requirements for the filesystem).

---

### Conflict Resolution

**REQ-F-021: Conflict Prompt on Destination Name Collision**

When a copy or move operation targets a destination directory containing a file/folder with the same name as an incoming item, the system shall pause the operation and prompt the user inline in ModeStatusBar with options: skip, overwrite, auto-rename, cancel.

Acceptance Criterion: A test creates a source file "document.txt", navigates to a destination directory that already contains "document.txt", performs `yy` then `p`, a conflict prompt appears offering all four options (s/o/r/c or similar single keys); the task is paused and does not proceed until the user resolves it.

---

**REQ-F-022: Skip on Conflict**

The system shall, when the user chooses skip during a conflict, leave the existing destination file untouched and proceed to the next item in the queue without copying/moving the conflicting source item.

Acceptance Criterion: A test creates a conflict scenario, user presses skip-key, verifies the destination file remains unchanged (original content, mtime), the operation continues with other queued items, and the skipped item is counted separately from failures in the final summary.

---

**REQ-F-023: Overwrite on Conflict**

The system shall, when the user chooses overwrite, replace the destination file with the source file.

Acceptance Criterion: A test creates source "file.txt" (content "NEW"), destination already has "file.txt" (content "OLD"), performs copy with overwrite choice, verifies destination "file.txt" now contains "NEW".

---

**REQ-F-024: Auto-Rename on Conflict**

The system shall, when the user chooses auto-rename, append a suffix (e.g., " (2)", " (3)", etc.) before the file extension, and retry the destination. If the new name also conflicts, append " (4)", etc., until a free name is found.

Acceptance Criterion: A test creates source "report.pdf", destination has "report.pdf" and "report (2).pdf" but not "report (3).pdf", user chooses auto-rename, verifies the copied file is named "report (3).pdf" in the destination.

---

**REQ-F-025: Cancel on Conflict**

The system shall, when the user chooses cancel, abort the current task and drop all remaining queued tasks and items, including pending trash confirmations. This "cancel" is a resolution option of an already-open conflict prompt only — it is reachable exclusively while that prompt is displayed, is not a standing hotkey at any other time, and does not itself replace or overlap with REQ-C-009's Ctrl+C mechanism (see REQ-C-009 for the distinction between interrupting a running task and declining to resume a paused one).

Acceptance Criterion: A test queues a 5-file paste, encounters a conflict on file 2, user chooses cancel, verifies files 1 is processed (or skipped if that was earlier), file 2 is not processed, and files 3–5 are dropped; task exits immediately. A separate test confirms the cancel key/option has no effect when pressed outside an open conflict prompt.

---

**REQ-F-026: Per-Item Conflict Resolution**

The system shall resolve each conflict individually, even in a multi-file operation. No "apply to all" option exists; the user resolves every conflict they encounter.

Acceptance Criterion: A test queues a 3-file paste where all 3 destinations conflict. User encounters 3 separate conflict prompts and makes different choices (skip file 1, overwrite file 2, auto-rename file 3). All 3 choices take effect independently.

---

### Task Execution & Queue Management

**REQ-F-027: Sequential Task Execution**

The system shall execute file operations (copy, move, trash) sequentially: only one operation (task) is in flight at any given time. A new paste/trash queues the task; it does not start until the previous operation completes.

Acceptance Criterion: A test queues two paste operations in rapid succession (yy, p, yy, p) or triggers a trash while a paste is in progress, verifies only one operation is running at a time (inspected via task progress indicator or logging).

---

**REQ-F-028: Task Progress Reporting**

The system shall, while a file operation is in progress, display in ModeStatusBar: operation type (Copy/Move/Trash), current file name (or path), and an indicator of progress (e.g., "3/10 items" or similar).

Acceptance Criterion: A test initiates a copy of 10 files, verifies ModeStatusBar displays "Copy 3/10: filename.ext" or similar, updates in real-time as files are processed.

---

**REQ-F-029: No Overlay Progress Dialog**

The system shall surface all progress and prompts (conflicts, confirmation, cancellation) exclusively in the existing ModeStatusBar.qml. No new modal dialog, overlay, or popup shall be introduced for progress or conflict resolution.

Acceptance Criterion: A test initiates a file operation, verifies no new window/overlay/modal appears; all feedback is inline in ModeStatusBar. Review the codebase to confirm no new modal/overlay UI components are added for Stage 4.

---

### Cancellation

**REQ-F-030: Ctrl+C Cancels Current Task**

The system shall, when the user presses Ctrl+C while a file operation is in progress, cancel the currently-running task and discard all remaining queued items.

Acceptance Criterion: A test initiates a copy of 20 large files, presses Ctrl+C after 5 files are copied, verifies the current file's partial copy is cleaned up (or aborted), remaining 15 files are not queued, and the operation stops immediately.

---

**REQ-F-031: Escape Does Not Cancel Tasks**

The system shall treat Escape as a VimModeController state-transition key only (exiting VISUAL/SEARCH/INSERT back to NORMAL) and not as a task cancellation key. File operations in progress are not affected by Escape.

Acceptance Criterion: A test initiates a file operation in NORMAL mode, presses Escape, verifies the operation continues uninterrupted (the keypress has no effect on task cancellation).

---

**REQ-F-032: Cancelled Transfer Preservation**

When a copy is cancelled or fails, the system shall remove temporary output and preserve any pre-existing destination. A cancelled incomplete directory move shall retain the entire source tree; completed destination copies remain.

Acceptance Criterion: Safety regression tests exercise this behavior and preserve failed sources and existing destinations.

---

### Partial Failure Handling

**REQ-F-033: Skip Failed Items, Continue Operation**

If a copy, move, or trash operation encounters an error on one item (e.g., permission denied, disk full), the system shall log the failure, skip that item, and continue processing the remainder of the queue.

Acceptance Criterion: A test queues a 5-file operation where file 3 is unreadable (permission denied). Files 1, 2, 4, 5 are copied/moved/trashed successfully; the operation completes and reports partial success.

---

**REQ-F-034: Final Summary on Partial Failure**

The system shall, upon task completion (whether fully successful or partially failed), display a summary in ModeStatusBar showing the count of succeeded items, failed items, and enough detail to identify which items failed and the reason for each failure.

Acceptance Criterion: A test runs a 10-item operation where 2 items fail (one permission denied, one disk full). ModeStatusBar displays "8/10 succeeded. Failed: file3 (Permission denied), file7 (No space left on device)." or equivalent.

---

**REQ-F-035: Distinguishable Error Reasons**

The system shall capture and report the specific error reason for each failed item (e.g., EACCES → "Permission denied", ENOSPC → "No space left on device", EEXIST → "File exists", EIO → "I/O error", etc.).

Acceptance Criterion: A test creates scenarios for each common error type (permission denied, disk full, I/O error) and verifies each is reported with the correct human-readable reason in the summary.

---

### Cross-Filesystem Move Handling

**REQ-F-036: Detect Cross-Filesystem Move (EXDEV)**

The system shall, when a move operation calls `rename()` and receives EXDEV (invalid cross-device link), transparently detect this condition and fall back to a copy-then-delete-source strategy using the same task/progress/conflict machinery.

Acceptance Criterion: A test uses a loopback or tmpfs fixture to create two distinct filesystems. Performs `dd` on a file in filesystem A, navigates to a directory on filesystem B, presses `p`. The operation completes (file is moved from A to B) with no visible difference in UI or behavior compared to a same-filesystem move.

---

**REQ-F-037: Cross-Filesystem Move is Transparent to User**

The system shall not expose the EXDEV fallback to the user. Progress, conflicts, and final summary shall appear identical whether the move is same-filesystem (fast rename) or cross-filesystem (copy + delete).

Acceptance Criterion: A test performs the same `dd`/`p` operation on two separate pairs of directories (one same-filesystem, one cross-filesystem). The user sees identical ModeStatusBar progress and behavior; the only difference is execution time (cross-filesystem naturally takes longer).

---

**REQ-F-038: Complete Source Retention**

The system shall remove a source after a merge or EXDEV copy only when all descendants transferred successfully, none were skipped, and cancellation has not been requested. Otherwise it shall retain the entire source tree and report an incomplete top-level item, nested failures separately from skips, and “source retained; some destination copies exist”.

Acceptance Criterion: Safety regression tests exercise this behavior and preserve failed sources and existing destinations.

---

### Symlink Handling (Confirmed)

**REQ-F-039: Copy Symlink Without Dereferencing**

The system shall, when copying a symlink, create a new symlink with the same target string at the destination, never dereference and copy the target's content.

Acceptance Criterion: A test creates a symlink "link → /etc/passwd", performs `yy` + `p` to copy it. The destination contains a new symlink with the same target string. (Test remains valid even if /etc/passwd is not accessible or has changed.)

---

**REQ-F-040: Move Symlink Without Dereferencing**

The system shall, when moving a symlink, relocate the link itself with its target string intact, never dereference the target.

Acceptance Criterion: A test creates a symlink "link → /some/path", performs `dd` + `p` to move it to another directory. The symlink appears at the destination pointing to the same target; the original symlink is gone.

---

### Per-Partition Trash Handling (Full Freedesktop Compliance)

**REQ-F-041: Filesystem Boundary Detection**

The system shall determine whether a file being trashed resides on the same filesystem (device) as `$XDG_DATA_HOME` by comparing device identifiers (e.g., `st_dev` from `stat`), before selecting which trash directory to use.

Acceptance Criterion: A test trashes a file that resides on the home filesystem and verifies it is placed in `$XDG_DATA_HOME/Trash`. A second test, using a loopback/tmpfs-mounted second filesystem, trashes a file on that filesystem and verifies boundary detection selects the per-partition path (REQ-F-042/043) instead.

---

**REQ-F-042: Per-Partition Trash Directory (`$topdir/.Trash/$uid`)**

When a file to be trashed resides on a filesystem other than `$XDG_DATA_HOME`'s, the system shall check for `$topdir/.Trash` (where `$topdir` is the mount point of that filesystem). If it exists, is not a symlink, and has the sticky bit (`S_ISVTX`) set, the system shall use — creating if necessary, mode `0700` — the per-user subdirectory `$topdir/.Trash/$uid` as the trash directory for that file, per the freedesktop.org Trash specification's security requirements for shared top-level trash directories. If `$topdir/.Trash` exists but is a symlink or lacks the sticky bit, it shall be rejected outright (never used), matching the spec's anti-tampering requirement.

Acceptance Criterion: A fixture creates `$topdir/.Trash` with the sticky bit set on a loopback-mounted filesystem; trashing a file there creates `$topdir/.Trash/$uid/files` and `$topdir/.Trash/$uid/info` containing the trashed file and its metadata. A second test creates `$topdir/.Trash` as a symlink (or without the sticky bit); verifies it is rejected and the system falls through to REQ-F-043 instead of using it.

---

**REQ-F-043: Per-Partition Trash Directory Fallback (`$topdir/.Trash-$uid`)**

If `$topdir/.Trash` does not qualify per REQ-F-042 (missing, is a symlink, or lacks the sticky bit), the system shall attempt to use `$topdir/.Trash-$uid`, creating it (mode `0700`) if it does not already exist and the user has permission to create it.

Acceptance Criterion: A fixture filesystem with no `$topdir/.Trash` present; trashing a file on it creates `$topdir/.Trash-$uid/files` and `$topdir/.Trash-$uid/info`, and the file is moved there.

---

**REQ-F-044: Trash Failure Leaves Source Untouched**

If source lookup, trash directory creation or validation, metadata creation, or same-filesystem relocation fails, the system shall leave the source untouched, report the failure kind, affected path and underlying reason, and continue the batch. It shall never copy across filesystems into home trash or offer permanent deletion.

Acceptance Criterion: Safety regression tests exercise this behavior and preserve failed sources and existing destinations.

---

**REQ-F-049: Retired**

Retired by the approved safety-fixes plan. Permanent-delete confirmation is removed; this identifier is reserved.

Acceptance Criterion: Safety regression tests exercise this behavior and preserve failed sources and existing destinations.

---

**REQ-F-050: Retired**

Retired by the approved safety-fixes plan. Permanent-delete execution is removed; this identifier is reserved.

Acceptance Criterion: Safety regression tests exercise this behavior and preserve failed sources and existing destinations.

---

**REQ-F-051: Regular Cross-Filesystem Move Is Unaffected**

REQ-F-044's removal of the home-trash cross-device fallback applies only to trash (`D`) operations. Regular copy/move operations (`p` pasting a copy or cut register across filesystems) continue to use the transparent EXDEV copy-then-delete-source fallback exactly as specified in REQ-F-036/037/038, unchanged.

Permanent deletion is unavailable. Trash failures leave sources untouched (REQ-F-044); REQ-F-049/050 are retired.

---

**REQ-F-045: Relative Path in Per-Partition Trash Metadata**

When using a per-partition trash directory (`$topdir/.Trash/$uid` or `$topdir/.Trash-$uid`), the system shall record the `Path` field in the corresponding `.trashinfo` file relative to `$topdir`, not as an absolute path, per the freedesktop.org Trash specification. (Home trash, per REQ-F-018, continues to use an absolute path.)

Acceptance Criterion: A test trashes a file at `$topdir/sub/file.txt` into `$topdir/.Trash-$uid`; verifies the `.trashinfo` `Path` field reads `sub/file.txt` (URL-encoded), not the absolute filesystem path.

---

**REQ-F-046: Independent Trash Directories Are Not Merged**

The system shall treat each trash directory in use (home trash and any per-partition trash directories) as fully independent, each with its own `files/` and `info/` subdirectories; no cross-directory deduplication, indexing, or merging is performed.

Acceptance Criterion: In one session, trash one file to home trash and another file to a per-partition trash on a loopback fixture; verify both trash directories exist independently with their own correct entries, and neither file is duplicated into or moved between the two.

---

### Recursive Operation Granularity

**REQ-F-047: Directory Paste Counts as a Single Progress Item**

When a paste or move operation includes a directory, the system shall count that directory as exactly one item toward the "N/M items" progress reported under REQ-F-028, regardless of how many files or subdirectories it recursively contains.

Acceptance Criterion: A test pastes a directory containing 500 nested files together with 2 other top-level files (3 top-level entries total); ModeStatusBar progress shows ".../3" at each step, never a count reflecting the 502 total files.

---

**REQ-F-048: Nested Name Collisions Resolve Automatically**

When copying or moving a directory recursively, if a file or subdirectory *below the top level* collides with an existing name at the corresponding destination path, the system shall automatically resolve that collision as "skip" (per REQ-F-022) without prompting the user. Only a top-level name collision (the item directly named in the register, per REQ-F-021) triggers an interactive conflict prompt.

Acceptance Criterion: A test pastes a directory tree containing one nested file that already exists at the corresponding destination path; verifies no conflict prompt appears for it, the pre-existing nested file is left untouched, and the rest of the tree copies normally. A second test confirms that if the top-level directory's own name collides with an existing destination entry, the interactive REQ-F-021 prompt still appears for it.

---

## Non-Functional Requirements

**REQ-NF-001: UI Thread Must Not Block During I/O**

The system shall execute copy, move, and trash operations in a worker thread so that the GUI thread remains responsive. Long-running operations shall not freeze the UI or prevent the user from typing, scrolling, or interacting with the directory display.

Acceptance Criterion: A test initiates a copy of 1000 files on a slow simulated filesystem. While copying, the user scrolls the file listing, types other commands, and presses keybindings; the UI remains smooth and responsive. Verify via instrumentation (thread logs or performance profiler) that I/O runs off-thread.

---

**REQ-NF-002: Cancellation (Ctrl+C) Must Be Responsive**

The system shall respond to a Ctrl+C cancellation request within 100 milliseconds (or at most one I/O system call's latency), not wait for the current file's transfer to complete.

Acceptance Criterion: A test copies a very large single file (gigabytes), presses Ctrl+C, and verifies the operation stops within ~100ms of keypress (measured via logs or profiler). The partial destination file is cleaned up immediately.

---

**REQ-NF-003: Task Queue Must Scale to Hundreds of Files**

The system shall handle queues of 100+ files in a single operation without noticeable performance degradation, memory exhaustion, or task stalls.

Acceptance Criterion: A test performs `yy` on a directory with 500 files, navigates elsewhere, presses `p`, and verifies all 500 files are copied or moved without crashes, memory leaks, or hangs. Task progress remains smooth.

---

**REQ-NF-004: Conflict Prompt Response Time**

The system shall present a conflict prompt within 50 milliseconds of detecting a name collision, so that the user perceives the confirmation as immediate.

Acceptance Criterion: A test creates a conflict scenario (thousands of pre-existing destination files), monitors ModeStatusBar latency from first destination check to prompt display, verifies it is under 50ms.

---

**REQ-NF-005: Memory Efficiency**

The system shall not buffer entire file contents in memory during copy/move operations. Large files shall be streamed (read/write in chunks) to avoid out-of-memory errors on systems with limited RAM.

Acceptance Criterion: A test copies a file larger than available RAM (simulated by limiting process memory or using a >10GB file), verifies the operation completes without OOM killer or crash. Instrumentation confirms chunk-based I/O, not full-buffer.

---

## Constraints

**REQ-C-001: Sequential Queue Only (No Parallelism)**

The system shall execute at most one file operation task at a time. No concurrent or parallel execution of copy/move/trash tasks is permitted in v1.

Acceptance Criterion: Code review confirms TaskManager queues and runs one task, blocking on the next until completion. Instrumentation shows no overlapping I/O operations.

---

**REQ-C-002: Single Clipboard Register**

The system shall maintain exactly one clipboard register (not named, not aliased). No vim-style "a–"z registers or register history/stack.

Acceptance Criterion: Code review confirms a single register data structure. Test verifies that `yy` followed by `dd` overwrites the register (not appends to a stack).

---

**REQ-C-003: Freedesktop Trash Storage**

The system shall use home and per-partition freedesktop trash storage, validate the directories and complete metadata before moving the source. It shall refuse trashing when no valid same-filesystem location is available (REQ-F-044/056).

Acceptance Criterion: Code review and regression evidence confirm this constraint.

---

**REQ-C-004: No New Modal or Overlay UI**

All conflict resolution, confirmation, and progress prompts shall be surfaced exclusively in the existing ModeStatusBar.qml. No new modal dialog, overlay, pop-up window, or on-screen notification layer is permitted.

Acceptance Criterion: Code review and visual inspection confirm no new UI components are added. ModeStatusBar shows all prompts and progress.

---

**REQ-C-005: Mkdir, Touch, Rename Remain Synchronous**

Mkdir, touch, and rename operations (implemented in Stage 3) shall remain synchronous in Stage 4. These operations shall NOT be ported to TaskManager or executed asynchronously.

Acceptance Criterion: Code review confirms mkdir, touch, rename are unchanged from Stage 3. No async worker thread or TaskManager integration for these operations.

---

**REQ-C-006: Vim Modal Keybindings Only**

File operations are accessible exclusively through Vim-modal keybindings (yy/dd/y/d/p/D). No menu, toolbar, mouse context menu, or drag-and-drop alternative is provided.

Acceptance Criterion: Code review confirms keybindings are integrated into VimModeController. No non-modal UI for file operations is present.

---

**REQ-C-007: No Confirmation Bypass in v1**

Trash confirmation (D operation) is mandatory and unconditional for all items. No config toggle, environment variable, or runtime flag shall allow users to skip or auto-confirm trash operations in v1.

Acceptance Criterion: Code review confirms trash confirmation logic has no conditional bypass. Test verifies every `D` operation prompts, regardless of single item or VISUAL selection size.

---

**REQ-C-008: Escape During Prompts**

While a trash confirmation is displayed, Escape shall decline it. While a conflict prompt is displayed, Escape shall leave it unresolved. Prompt keys shall preserve the current mode, editor text, cursor and selection. Outside prompts Escape retains its existing mode behavior.

Acceptance Criterion: Safety regression tests exercise this behavior and preserve failed sources and existing destinations.

---

**REQ-C-009: Ctrl+C is Sole Cancellation Mechanism (for a Running Task)**

File operations that are actively running (mid-copy, mid-move, mid-trash) are cancelled only via Ctrl+C. No other key, prompt option, or UI element interrupts an in-progress task. This is distinct from REQ-F-025's "cancel" conflict-prompt option: a conflict/confirmation prompt is a paused, not-yet-resumed state reachable only after a task has already stopped to ask a question, so choosing "cancel" there resolves that specific open prompt rather than acting as a second, general-purpose interruption hotkey. Both paths end at the same internal abort-and-drop-queue behavior; REQ-C-009 governs interrupting active I/O, REQ-F-025 governs declining to resume it.

Acceptance Criterion: Code review confirms Ctrl+C is the only mechanism that interrupts an actively-running (non-paused) task. Test verifies other keys (Escape, Backspace, Q, etc.) do not cancel a running task. Separately, test verifies the conflict prompt's "cancel" option (REQ-F-025) is only reachable, and only has effect, while that prompt is displayed — it is not a standing hotkey during normal task progress.

---

**REQ-C-010: No Undo**

The system shall not implement undo, operation history, snapshots or a reverse-operation queue. Temporary copy staging preserves existing destinations on failure; it is not an undo facility.

Acceptance Criterion: Code review and regression evidence confirm this constraint.

---

**REQ-C-011: No Permanent Delete**

The system shall provide no permanent-delete fallback, prompt, command or public deletion primitive. Recursive source removal shall be private to successful move cleanup.

Acceptance Criterion: Safety regression tests exercise this behavior and preserve failed sources and existing destinations.

---

## Verification & Testing Notes

### Test Fixtures Required

1. **Cross-Filesystem Loopback/Tmpfs Fixture:**
   - Two distinct filesystems (loopback device or tmpfs mount) to trigger EXDEV and exercise cross-filesystem move fallback.
   - Tests cannot rely on same-filesystem rename for this requirement.
Permanent deletion is unavailable. Trash failures leave sources untouched (REQ-F-044); REQ-F-049/050 are retired.

2. **Redirected XDG_DATA_HOME Fixture:**
   - Isolate trash tests from the user's real `~/.local/share/Trash`.
   - Each test run uses a clean, temporary `$XDG_DATA_HOME` to verify Trash structure and metadata format.

3. **Permission-Denied File Fixture:**
   - A file or directory with insufficient read/write permissions to trigger partial-failure scenarios.
   - Used to verify skip-and-continue and error reporting.

4. **Disk-Full Simulation (Optional but Recommended):**
   - Simulated or actual full-disk condition to test ENOSPC error handling.
   - If simulated (e.g., via loopback with quota), ensure the simulation is reliable and repeatable.

### Test Coverage Baseline

- At least one test per REQ-F-XXX requirement (minimum 51+ functional tests).
- At least one test per REQ-NF-XXX requirement where verifiable via integration testing.
- Cross-test coverage: multi-file operations, conflict scenarios, partial failures, cancellation, and cross-filesystem boundaries.
- Acceptance criteria are the test's pass condition; no separate "acceptance test" suite.

### Integration with Existing Systems

- **VimModeController:** Stage 4 integrates new keybindings (yy/dd/y/d/p/D) into the existing dispatcher. No changes to NORMAL/VISUAL/SEARCH/INSERT state machine itself; new keys are handled as new commands within the dispatcher.
- **ModeStatusBar.qml:** Reused for conflict prompts, confirmation prompts, and progress display. No new UI components introduced.
- **DirectoryModel & DirectoryController:** Existing directory listing and refresh; Stage 4 triggers refreshes after paste/trash operations to update the displayed listing.
- **PreviewService & ThumbnailService:** Stage 4 does not interact with preview/thumbnail generation during operations; these remain independent.

### Performance Baselines (Informational)

- Copy/move operations should sustain >50 MB/s throughput on local filesystems (benchmarked on test hardware).
- Conflict detection should occur within milliseconds of destination access.
- Cancellation (Ctrl+C) should stop I/O within 100 milliseconds.
- UI thread FPS should remain ≥30 during heavy background file I/O.

---

## Acceptance Criteria Summary

Every REQ-F-XXX, REQ-NF-XXX, and REQ-C-XXX requirement above includes an inline, independently-verifiable acceptance criterion. Acceptance is contingent on:

1. **Code review:** Confirms all functional and constraint requirements are implemented and integrated correctly.
2. **Automated test suite:** Passes all tests corresponding to each requirement's acceptance criterion.
3. **Manual verification:** UI/UX review confirms ModeStatusBar prompt behavior, keybinding responsiveness, and progress display match the spec.
4. **Specification compliance:** Trash structure and metadata format conform to freedesktop.org Trash spec (validated via inspection of actual Trash directories post-operation).

No requirement lacks an acceptance criterion. Stage 4 implementation cannot proceed to code review until this spec is approved.

---

## Related Documents

- **CLAUDE.md** — Project state, relationship to HoloNight umbrella, Stage 0 grill session notes.
- **BACKLOG.md** — Stage 4 task breakdown and feature hints (FileOperationService, TaskManager).
- **DESIGN.md** — Architectural patterns (async worker-thread model, signal/slot delegation) established in Stages 1–3.
- **TASKS.md** — Active and completed tasks; updated as implementation progresses.
- **/home/andrii/.claude/projects/*/memory/*** — Session memory: Vim modal architecture, async pattern confirmation, Stage 1–3 completion notes.

## Approved safety revision (2026-09-10)

The implementation plan supplied by the user approves this revision.

- **REQ-F-052: Safe transfer endpoints.** Before mutation, reject identical entries,
  hard-link aliases and directory destinations inside the source, resolving symlinked
  parents. Stage regular files and symlinks in sibling temporary entries and atomically
  commit without following destination symlinks. Rename without replacement unless
  overwrite was selected; directory overwrite merges, incompatible directory types fail.
- **REQ-F-053: Unsupported copy types.** Reject FIFOs, sockets and devices before
  opening them. Same-device moves may rename them; EXDEV fallback fails safely.
- **REQ-F-054: Prompt lifetime.** Task and prompt identifiers shall reject stale
  callbacks/responses; reset every response and recheck cancellation after semaphore
  wakeup and before commit/removal. Ctrl+C and conflict Cancel discard all queued work.
  Queue trash confirmation with its task; show it only at the front, before trash I/O.
- **REQ-F-055: Prompt input priority.** Capture prompt keys before text fields and
  application shortcuts in all modes, preserve mode/text/cursor/selection and restore
  editor focus. Close Quick Look upon prompt arrival. Ctrl+C cancels from focused editors.
- **REQ-F-056: Validated trash.** Validate directory type, symlink status, ownership
  and owner-only permissions, rejecting unexpected existing directories without repair.
  Keep the validated shared `.Trash/$uid` to `.Trash-$uid` fallback. Finish exclusive
  metadata creation before same-filesystem non-overwriting rename; roll back this
  attempt's metadata if rename fails or cancellation arrives.

Reference: [freedesktop Trash specification](https://specifications.freedesktop.org/trash/latest/).
