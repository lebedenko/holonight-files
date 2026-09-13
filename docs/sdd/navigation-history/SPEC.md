# Navigation History Feature — Requirements Specification

**Document ID:** SDD-NAV-HIST
**Version:** 1.0
**Date:** 2026-09-13
**Status:** Complete

---

## 1. Overview and Purpose

This specification defines the navigation history feature for HoloNight Files, a keyboard-driven Vim-modal file manager. The feature enables users to traverse a jump list of previously visited directories using Ctrl+O (back) and Ctrl+I (forward) key bindings, maintaining the cursor position at each directory through a pending restore mechanism. This specification also formalizes the parent-navigation cursor fix (independent of history) which restores the cursor to the entry whose name is the basename of the directory being exited.

---

## 2. Scope

**In Scope:**
- Jump list mechanics (addition, deduplication, traversal, capacity management)
- Pending cursor restore mechanism (by entry name, keyed to async directory load completion)
- Parent-navigation cursor fix (cursor positioned on parent's entry name)
- Keyboard bindings: Ctrl+O (back), Ctrl+I (forward), with count prefix support
- Missing directory detection and handling (stat check, skip/continue traversal)
- UI elements: back/forward header buttons with enabled/disabled state
- Gating: NORMAL mode only, no effect when prompt is open
- Quick Look closure on navigation
- In-memory history per window, maximum 100 entries

**Out of Scope:**
- Persistence across application restarts
- Per-tab or per-window history variants
- Browser-style truncation of forward entries when navigating from middle of list
- History dropdown/list UI for manual entry selection
- Tracking renames and moves of directories
- Mouse back/forward buttons (extra mouse buttons)
- Modification to `h` (navigateParent) or `l` (navigateInto) key semantics beyond the cursor fix
- History for files (only directories)
- Per-directory restore preferences or customization

---

## 3. Glossary

| Term | Definition |
|------|-----------|
| **Jump List** | An ordered, in-memory list of up to 100 directory paths visited by the user, with associated cursor entry name. Vim-style semantics (back/forward traversal without reordering). |
| **Entry** | A file or directory in the current directory listing. Identified primarily by name (case-sensitive). |
| **Cursor Entry Name** | The name (basename) of the file/directory under the cursor at the moment of navigation away from a directory. Stored in the jump list to restore cursor position on return. |
| **Current Index** | The position (0–99) within the jump list of the directory currently displayed. Used by back (Ctrl+O) and forward (Ctrl+I) traversal. |
| **Restore Target** | A name (string) identifying which entry the cursor should be placed on when a directory loads. Matched case-sensitively against visible entries in the sorted/filtered listing. |
| **Pending Cursor Restore** | A deferred cursor movement operation, keyed to the completion of asynchronous directory listing load. Cancelled if user moves cursor explicitly before load completes. |
| **NORMAL Mode** | The primary Vim modal state in which navigation keys (h, l, j, k, Ctrl+O, Ctrl+I, etc.) are active. Defined by `vim_mode_controller.h`. |
| **Prompt** | A modal confirmation or conflict dialog managed by TaskManager (`hasPrompt` property). Navigation keys are inactive while a prompt is open. |
| **Missing Entry** | A jump list entry whose directory path no longer exists as a directory on the filesystem at the time of traversal. Such entries are removed in-place and traversal continues to the next valid entry. |

---

## 4. Functional Requirements

### 4.1 Jump List Structure and Management

**REQ-F-001: Jump list initialization and invariant**
Ubiquitous: The system shall own one empty jump list per DirectoryController instance, and after every navigation or traversal the current directory shall be the entry at the current index (`entries[index].path == currentPath`).

- **Acceptance Criteria:**
  - A freshly constructed jump list has size 0, index −1, and canGoBack/canGoForward both false.
  - After any sequence of open()/back/forward calls in a unit test, `entries[index].path` equals the last successfully navigated path.
  - Two DirectoryController instances in one test process do not observe each other's entries.

**REQ-F-002: Initial directory adds entry**
When the application starts and loads the initial startup directory (e.g., home directory), the system shall add that directory as the first entry to the jump list with cursor entry name set to empty string or a default placeholder.

- **Acceptance Criteria:**
  - Initial directory appears at index 0 of the jump list.
  - Entry path is absolute and matches the startup directory.
  - Its stored cursor entry name is empty until the user leaves the directory (REQ-F-038).
  - Current index is 0; canGoBack and canGoForward are false.

**REQ-F-003: Entry deduplication on addition**
When open() navigates to a path that differs from the current directory, the system shall remove any existing entry with that path, append the path as the last entry (without truncating entries after the current index), and set the current index to the last entry.

- **Acceptance Criteria:**
  - List [A, B, C, D], index 1 (B); open(C) yields [A, B, D, C], index 3 — D is retained (no browser-style truncation).
  - List [A, B, C], index 2; open(A) yields [B, C, A], index 2; size stays 3.
  - List [A, B], index 1; open(E) yields [A, B, E], index 2.
  - Path equality is exact string comparison of the cleaned absolute path (REQ-C-003).

**REQ-F-004: No-op navigation does not add entry**
When open() is called with a path equal to the current directory, the system shall leave the jump list entries and current index unchanged (the directory itself may still be reloaded as it is today).

- **Acceptance Criteria:**
  - List [A, B, C], index 1 (current B); open(B) leaves the list [A, B, C] and index 1, so canGoForward remains true.
  - List [A], index 0; open(A) leaves size 1.

**REQ-F-005: Capacity management and entry eviction**
If adding a new entry would exceed 100 entries in the jump list, the system shall remove the oldest entry (lowest index) first.

- **Acceptance Criteria:**
  - List never exceeds 100 entries.
  - When at capacity (100) and a new entry is added, size remains 100.
  - The entry at original index 0 is removed; all remaining entries shift down by 1.

### 4.2 Back Navigation (Ctrl+O)

**REQ-F-006: Back navigation with valid target**
When the user presses Ctrl+O while in NORMAL mode and no prompt is open, and a valid back target exists (current index > 0), the system shall move the current index backward by 1 (or more if count prefix is used), navigate to the directory at that index, and apply the pending cursor restore with the cursor entry name stored in that entry.

- **Acceptance Criteria:**
  - Index is at 3, Ctrl+O pressed, current index becomes 2 and the directory at index 2 is opened.
  - Directory load completes; cursor is positioned to the entry named in the stored cursor entry name for that directory.
  - The directory path and entry name are correct for the jumped-to directory.

**REQ-F-007: Back navigation with count prefix**
When the user presses Ctrl+O preceded by a count (e.g., `3 Ctrl+O`), the system shall move backward by the specified count, skipping missing entries (REQ-F-025) without counting them, and landing on the count-th valid back entry.

- **Acceptance Criteria:**
  - List [A, B, C, D, E, F], index 5, all valid; `3 Ctrl+O` opens C (index 2).
  - Same list with D and E deleted; `3 Ctrl+O` opens A (valid steps C, B, A), D and E are removed, and the index is 0.
  - List [A, B, C], index 2 (all valid); `3 Ctrl+O` lands on A (index 0) — as far as possible — and opens only A (intermediate entries are not opened).
  - Count is consumed (cleared) after traversal.

**REQ-F-008: Current directory is recoverable by forward after back**
When the user presses Ctrl+O from the last entry, the system shall keep the current directory as the last entry so that Ctrl+I returns to it. (Because open() always appends the destination — REQ-F-003 — the current directory is already the last entry; no extra recording step is needed, unlike Vim's cursor-jump model.)

- **Acceptance Criteria:**
  - open(A), open(B), open(C) gives [A, B, C], index 2; Ctrl+O opens B (index 1) and the list is still [A, B, C]; Ctrl+I opens C (index 2).
  - Ctrl+O never changes list size except by removing missing entries (REQ-F-025).

**REQ-F-009: Back with no target does nothing**
If Ctrl+O is pressed while the current index is 0, then the system shall not navigate, shall not change the list or index, and shall not change the status message.

- **Acceptance Criteria:**
  - List [A], index 0; Ctrl+O leaves currentPath == A, statusMessage unchanged, and no navigated() signal is emitted.
  - List [A, B], index 1; `2 Ctrl+O` opens A; a further Ctrl+O is a no-op per the first criterion.

### 4.3 Forward Navigation (Ctrl+I)

**REQ-F-010: Forward navigation with valid target**
When the user presses Ctrl+I while in NORMAL mode and no prompt is open, and a valid forward target exists (current index < list size − 1), the system shall move the current index forward by 1 (or more if count prefix is used), navigate to the directory at that index, and apply the pending cursor restore with the cursor entry name stored in that entry.

- **Acceptance Criteria:**
  - Index is 2, forward target exists at index 3, Ctrl+I pressed, index becomes 3.
  - Directory loads; cursor is positioned to the entry named in the stored cursor entry name.
  - Traversal respects count prefix and missing entry skipping (REQ-F-015).

**REQ-F-011: Forward navigation with count prefix**
When the user presses Ctrl+I preceded by a count (e.g., `5 Ctrl+I`), the system shall move forward by the specified count, skipping missing entries, and land on the count-th valid forward entry.

- **Acceptance Criteria:**
  - Index is 2, list size is 10; `5 Ctrl+I` moves forward to index 7 (if no missing entries), or to the highest valid entry forward if fewer than 5 valid entries remain.
  - Count is consumed after traversal.

**REQ-F-012: Forward with no valid target does nothing**
If Ctrl+I is pressed and no valid forward target exists (current index is already at list size − 1), the system shall not change the current directory or index, and the forward button shall be disabled.

- **Acceptance Criteria:**
  - Index is 4, list size is 5 (indices 0–4); Ctrl+I does nothing.
  - Forward button is disabled (dimmed).

### 4.4 Pending Cursor Restore Mechanism

**REQ-F-013: Cursor restore applied on directory load completion**
When an asynchronous directory listing load completes (worker thread finishes loading entries), the system shall check if a pending cursor restore exists for that directory. If it does, and the restore target (entry name) exists in the visible, sorted, and filtered listing, the system shall move the cursor to that entry's row.

- **Acceptance Criteria:**
  - Restore target is "Pictures", listing is sorted by name, "Pictures" is at row 5; cursor moves to row 5.
  - If "Pictures" is not present (filtered out or deleted), cursor falls back to row 0 (REQ-F-014).
  - Restore is applied exactly once per load completion.

**REQ-F-014: Cursor restore fallback to row 0**
If the pending cursor restore target entry is not found in the visible listing (because it is hidden, was deleted, or is filtered out), the system shall move the cursor to row 0 (first entry) instead.

- **Acceptance Criteria:**
  - Restore target is "SecretDir"; listing does not show "SecretDir" (hidden files not shown).
  - Cursor moves to row 0 instead of hanging or erroring.
  - A watcher-triggered refresh does not re-apply this fallback (REQ-F-017).

**REQ-F-015: Cursor restore cancelled by explicit user cursor movement**
If the user explicitly moves the cursor (via j, k, gg, G, count+j/k motions, or mouse) before the asynchronous directory load completes, the system shall cancel the pending cursor restore. The load completion shall not move the cursor.

- **Acceptance Criteria:**
  - Pending restore target is "Documents", user presses `j` or `3j` before load completes; pending restore is discarded.
  - Load completes; cursor position (from user's `j` motion) is not overwritten.
  - No "restore on every update" behavior.

**REQ-F-016: Pending restore replaced on new navigation**
When a new navigation starts, the system shall discard any pending restore and set only the restore target belonging to the new navigation: basename of the exited directory for navigateParent(), the stored cursor entry name for back/forward, and none (row 0) for any other open().

- **Acceptance Criteria:**
  - Press `h` from ~/Pictures (pending "Pictures") and, before the load completes, click Places → /tmp; when /tmp finishes loading the cursor is at row 0 even if /tmp contains an entry named "Pictures".
  - Press `h` from ~/a/b then immediately `h` again before the load completes; after ~ loads the cursor is on "a", not "b".

**REQ-F-017: Watcher refresh does not re-apply restore**
If a QFileSystemWatcher-triggered refresh (due to external changes) reloads the directory after a cursor restore has been applied, the system shall not re-apply the restore. The cursor position remains at its current location.

- **Acceptance Criteria:**
  - Restore applied, cursor at row 3 (entry "Documents").
  - External process adds/deletes a file; watcher triggers refresh.
  - Listing reloads; cursor remains at row 3, not re-positioned to "Documents".

### 4.5 Parent-Navigation Cursor Fix (Independent Requirement)

**REQ-F-018: Cursor positioned on parent's entry name after navigateParent()**
When navigateParent() is called (e.g., via `h` key) from directory D, and the parent directory P is successfully loaded, the system shall apply a pending cursor restore targeting the entry in P whose name equals basename(D).

- **Acceptance Criteria:**
  - Current directory is "~/Pictures" (basename "Pictures"), `h` key pressed.
  - Parent "~" is loaded; cursor is positioned to the entry named "Pictures".
  - If "Pictures" does not exist in the listing (deleted, or hidden), cursor falls back to row 0.
  - This restore mechanism is independent of history navigation; it applies whether or not the parent was visited before.

**REQ-F-019: Parent navigation restore fallback to row 0**
If the parent navigation restore target (basename of exited directory) is not found in the loaded parent listing, the cursor shall move to row 0.

- **Acceptance Criteria:**
  - Directory "~/old-name" is current; parent is "~"; `h` pressed; parent loads but "old-name" does not exist (renamed or deleted).
  - Cursor moves to row 0 of parent.

### 4.6 Key Bindings and Input Handling

**REQ-F-020: Ctrl+O and Ctrl+I key recognition**
The system shall recognize Ctrl+O and Ctrl+I by their key codes (Qt::Key_O and Qt::Key_I with Qt::ControlModifier) and not by their text representation, to ensure they are distinguished from Tab and other characters and to prevent confusion in text-input contexts.

- **Acceptance Criteria:**
  - Ctrl+I is detected via (key == Qt::Key_I && modifiers & Qt::ControlModifier).
  - Ctrl+O is detected via (key == Qt::Key_O && modifiers & Qt::ControlModifier).
  - Comparison is by key code, not by event.text() ("\t" or "\x0f").
  - In a text input field (INSERT mode), Ctrl+I does not trigger forward navigation.

**REQ-F-021: Navigation disabled in non-NORMAL modes**
Ctrl+O and Ctrl+I shall have no navigation effect while vim mode is VISUAL, SEARCH, or INSERT. In INSERT mode, Ctrl+I in a text field shall not navigate (normal text-field behavior is preserved).

- **Acceptance Criteria:**
  - In VISUAL mode, Ctrl+O and Ctrl+I produce no directory change.
  - In INSERT mode (editing a filename), Ctrl+I does not navigate.
  - Return to NORMAL mode; Ctrl+O and Ctrl+I work normally.

**REQ-F-022: Navigation disabled when prompt is open**
Ctrl+O and Ctrl+I shall have no navigation effect when TaskManager.hasPrompt is true (a modal confirmation or conflict dialog is open).

- **Acceptance Criteria:**
  - Modal prompt displayed; Ctrl+O and Ctrl+I do nothing.
  - Prompt dismissed; Ctrl+O and Ctrl+I are active again.

**REQ-F-023: Count prefix consumed**
When a navigation action (Ctrl+O or Ctrl+I) is performed with a pending count prefix, the count shall be consumed (cleared) whether or not the navigation succeeds.

- **Acceptance Criteria:**
  - Count 5 is pending; `5 Ctrl+O` moves back (or does nothing if impossible); count is cleared.
  - Subsequent Ctrl+O is treated as count 1.

**REQ-F-024: Quick Look closed on navigation**
When any navigation is performed (Ctrl+O, Ctrl+I, or any existing open/navigateInto/navigateParent action), if Quick Look is open, it shall be closed as part of the navigation action.

- **Acceptance Criteria:**
  - Quick Look open (quickLookOpen true); Ctrl+O pressed; Quick Look closes.
  - Navigation completes normally.

### 4.7 Missing Directory Handling

**REQ-F-025: Missing entry detection and removal during traversal**
When a traversal target (back or forward) is reached, if that directory's path no longer exists as a directory on the filesystem, the system shall remove that entry from the jump list in place and continue traversal in the same direction to the next valid entry (not missing).

- **Acceptance Criteria:**
  - Entry at index 3 points to "~/deleted-dir"; user Ctrl+O from index 4.
  - stat() check on "~/deleted-dir" fails (directory no longer exists).
  - Entry at index 3 is removed; remaining entries shift; traversal continues to index 2.
  - User lands on index 2 and the directory at index 2 is opened.

**REQ-F-026: Skip status message for missing entries**
When one or more entries are skipped due to missing directories (REQ-F-025), the system shall display a status message indicating the skipped path(s), e.g., "Skipped missing: ~/deleted-dir".

- **Acceptance Criteria:**
  - List [A, B, C, D], index 3, B and C deleted from disk; Ctrl+O opens A, the list becomes [A, D], index 0, and statusMessage contains both B's and C's paths prefixed by "Skipped missing:".
  - With no skipped entries, a successful traversal does not set a "Skipped missing" message.

**REQ-F-027: No navigation if all entries missing in direction**
If every candidate entry in the traversal direction is missing, then the system shall remove those entries, shall not navigate, shall keep the current directory as the current entry, and shall show the "Skipped missing" status message.

- **Acceptance Criteria:**
  - List [A, B], index 1, A deleted; Ctrl+O leaves currentPath == B, the list becomes [B], index 0, canGoBack becomes false, and statusMessage names A.

**REQ-F-028: Existence check is a single synchronous stat per candidate**
The system shall determine whether a candidate entry is missing with one synchronous existence check (`QFileInfo::isDir` or equivalent) on the UI thread per candidate examined, and shall not list or read any directory during traversal. canGoBack/canGoForward shall be computed from the index alone (no filesystem access).

- **Acceptance Criteria:**
  - `canGoBack` is true for list [A, B], index 1, even when A has been deleted (it becomes false only after a traversal prunes A).
  - If a directory disappears between the existence check and the asynchronous load, the normal directoryError is shown and the application does not crash (controller test deleting the directory in between via a test hook or by racing a removal before the worker load).

**REQ-F-029: Directory exists but fails to open is not skipped**
If a traversal target exists as a directory but fails to open (e.g., permission denied), it shall NOT be skipped. Navigation shall occur; the normal directory error shall be displayed; the entry shall remain in the jump list and be recorded as visited.

- **Acceptance Criteria:**
  - Entry points to "~/restricted" (no read permission); Ctrl+I traverses to it.
  - Directory listing fails; error is shown (e.g., "Permission denied").
  - Entry is not removed from jump list.

### 4.8 UI Elements: Header Buttons

**REQ-F-030: Back button placed at far left of header**
The system shall place an HnIconButton (back/← symbol) at the far left of AppHeaderBar, within the region x < sidebarWidth (200 px), vertically centered, with appropriate margins/padding.

- **Acceptance Criteria:**
  - Button is visible at x position between 0 and sidebarWidth.
  - Button is vertically centered in the header bar.
  - Button is rendered before (left of) the forward button.

**REQ-F-031: Forward button placed right of back button**
The system shall place an HnIconButton (forward/→ symbol) at the far left of AppHeaderBar, immediately to the right of the back button, still within x < sidebarWidth.

- **Acceptance Criteria:**
  - Forward button is rendered to the right of back button.
  - Both buttons fit within sidebarWidth (200 px) at minimum window width (420 px).

**REQ-F-032: Button enabled state reflects navigation availability**
The back button shall be enabled iff canGoBack is true AND vim mode is NORMAL AND TaskManager.hasPrompt is false. The forward button shall be enabled iff canGoForward is true AND vim mode is NORMAL AND TaskManager.hasPrompt is false. Disabled buttons shall render dimmed (HnIconButton disabled state).

- **Acceptance Criteria:**
  - canGoBack true, mode NORMAL, no prompt: button enabled (normal appearance).
  - canGoBack true, mode VISUAL, no prompt: button disabled (dimmed).
  - canGoBack true, mode NORMAL, prompt open: button disabled (dimmed).
  - canGoBack false: button disabled.

**REQ-F-033: Button click performs navigation**
Clicking an enabled back button shall perform the equivalent of Ctrl+O with count 1. Clicking an enabled forward button shall perform the equivalent of Ctrl+I with count 1.

- **Acceptance Criteria:**
  - Back button clicked; navigation occurs as if Ctrl+O was pressed.
  - Forward button clicked; navigation occurs as if Ctrl+I was pressed.
  - Count 1 is used; multiple clicks are treated as separate navigations.

**REQ-F-034: Buttons do not take focus**
Both back and forward buttons shall have focusPolicy set to Qt::NoFocus. After clicking either button, keyboard focus shall remain with the file listing, and Vim keys shall continue to be active.

- **Acceptance Criteria:**
  - Button clicked; activeFocus remains on the listing (not the button).
  - Subsequent `j`, `k`, `h`, `l`, etc., keys work normally.

**REQ-F-035: Buttons fit within header at minimum window width**
The back and forward buttons, including their margins/padding and icon size, shall fit entirely within the sidebarWidth (200 px) at the application's minimum window width (420 px), leaving space for the breadcrumb and other header elements.

- **Acceptance Criteria:**
  - Window resized to minimum width 420 px.
  - Both buttons' right edges (mapped to header coordinates) are ≤ the breadcrumb container's x, and both buttons have non-zero width and height.

**REQ-F-036: Breadcrumb container position unchanged**
Adding the back and forward buttons shall not change the x position of the breadcrumb pill container, which shall remain `breadcrumbLeftInset - breadcrumbPadding` as computed before this feature.

- **Acceptance Criteria:**
  - In a rendered window test, `breadcrumbContainer.x` equals `appHeaderBar.breadcrumbLeftInset - appHeaderBar.breadcrumbPadding` (±1 px) at default width and at 420 px.
  - Changing `window.sidebarWidth` in the test moves the breadcrumb by the same delta, as before.

**REQ-F-037: Button accessible names and tooltips**
The back button shall have an accessible name "Back" (for screen readers). The forward button shall have an accessible name "Forward". Tooltips are optional.

- **Acceptance Criteria:**
  - Back button accessible name is "Back".
  - Forward button accessible name is "Forward".
  - Screen reader can identify buttons by these names.

### 4.9 Cross-Cutting: Cursor Entry Name Capture

**REQ-F-038: Cursor entry name captured on navigation away**
When the user initiates a navigation away from the current directory (open, navigateInto, navigateParent, Ctrl+O, Ctrl+I), the system shall capture the name (basename) of the entry currently under the cursor in the current directory.

- **Acceptance Criteria:**
  - Current directory "~" with "Pictures" at row 3; cursor is on "Pictures".
  - Navigate to "~/Pictures" (via `l` or Ctrl+I, etc.).
  - Jump list entry for "~" records cursor entry name "Pictures".
  - When user returns to "~", cursor is restored to "Pictures" (or row 0 if not found).

**REQ-F-039: Empty or unloaded directory captures an empty name**
If the user leaves a directory whose listing has no row under the cursor (empty directory, error, or load not yet complete), then the system shall store an empty cursor entry name for that entry, and returning to it shall place the cursor at row 0.

- **Acceptance Criteria:**
  - open(E) where E is empty, then open(F), then Ctrl+O: E's stored name is empty and cursorRow is 0 after E reloads.
  - open(A) and immediately open(B) before A finishes loading: A's stored name is empty (not a stale name from a previous directory).

---

## 5. Non-Functional Requirements

**REQ-NF-001: History operations O(n) complexity**
All jump list operations (add, remove, traversal, search for missing entries) shall have O(n) time complexity where n ≤ 100.

- **Acceptance Criteria:**
  - Code review: add (dedup + append + evict) and a single traversal each iterate the list at most a constant number of times.

**REQ-NF-002: No filesystem access beyond existence check**
The system shall not perform any filesystem operations (read, list, stat, etc.) beyond a single synchronous existence check per candidate entry examined during traversal (REQ-F-028).

- **Acceptance Criteria:**
  - Existence check is one `QFileInfo`/stat call per examined candidate; none occur when computing canGoBack/canGoForward.
  - No directory listing or other I/O occurs as part of history traversal.

**REQ-NF-003: History logic testable without QML**
The history logic (jump list, pending restore, traversal semantics, count handling) shall be implemented in a form that is unit-testable without instantiating QML or the full application UI.

- **Acceptance Criteria:**
  - A plain C++ class or unit-test-compatible component can be instantiated in isolation.
  - Jump list operations can be exercised and verified without QML.
  - Tests do not require launching the GUI.

**REQ-NF-004: C++ and Qt conventions**
The implementation shall follow C++23 standards and Qt6 conventions, consistent with the existing codebase (e.g., existing controller patterns, naming, signal/slot usage).

- **Acceptance Criteria:**
  - Code compiles with the project's C++23 compiler settings.
  - Naming follows CamelCase for classes and methods, snake_case for member variables.
  - Qt signals and slots are used for asynchronous events.

**REQ-NF-005: Code quality tools pass**
The implementation shall pass all project tooling: clang-tidy (including cognitive complexity threshold 25), clang-format, qmllint (if any .qml files are added), and GoogleTest.

- **Acceptance Criteria:**
  - `task format-check` passes.
  - `task tidy` passes (no warnings or errors).
  - `task build` succeeds.
  - `task test` passes all tests, including new unit and controller tests.
  - `task qml-lint` passes (if applicable).

**REQ-NF-006: QML files registered in Taskfile**
If any new .qml files are added, they shall be registered in Taskfile.yml in the appropriate qml format check list (qml-sources or similar).

- **Acceptance Criteria:**
  - Any new .qml files are listed in Taskfile.yml.
  - `task qml-lint` includes these files.

---

## 6. Constraints

**REQ-C-001: In-memory, per-window history**
History shall be stored in memory and shall not be persisted across application restarts or saved to disk.

- **Acceptance Criteria:**
  - Code review: no QSettings, file, or other persistent storage is read or written by the jump list implementation.
  - A newly constructed DirectoryController in a test has an empty jump list regardless of what previous instances in the same process did.

**REQ-C-002: Maximum 100 entries**
The jump list shall never contain more than 100 entries. When a new entry is added at capacity, the oldest entry (lowest index) shall be removed.

- **Acceptance Criteria:**
  - List size is capped at 100.
  - Adding the 101st entry results in removal of entry 0.

**REQ-C-003: Stored paths match currentPath**
Paths stored in the jump list shall be exactly the value `currentPath` takes after open() (absolute, cleaned of "." / ".." / duplicate separators); symlinks shall NOT be resolved, so the breadcrumb shown after back/forward matches the one shown originally.

- **Acceptance Criteria:**
  - open("/tmp/x/../y") followed by open("/tmp/y") yields a single entry "/tmp/y".
  - open(L) where L is a symlink to directory T, then open(elsewhere), then Ctrl+O: currentPath equals L, not T.

**REQ-C-004: Entry names are case-sensitive**
Cursor entry name matching for restore shall be case-sensitive (unix filesystem convention).

- **Acceptance Criteria:**
  - Restore target "Pictures" does not match "pictures".
  - On case-insensitive filesystems, matching is still case-sensitive (string comparison, not fs-level compare).

**REQ-C-005: No rename/move tracking**
Renamed or moved directories are not tracked. An entry whose directory has been renamed or moved will be detected as missing (REQ-F-025) and removed during traversal.

- **Acceptance Criteria:**
  - Directory "~/old-name" navigated to; later renamed to "~/new-name".
  - Entry still points to "~/old-name"; traversal removes it as missing.
  - No automatic alias or symlink-following is performed.

**REQ-C-006: No history UI list**
No dropdown, list view, or selectable history UI shall be implemented. Navigation is only via Ctrl+O and Ctrl+I keys or header buttons.

- **Acceptance Criteria:**
  - No history dropdown in the UI.
  - No `:jumps` command or similar history listing.

**REQ-C-007: Buttons use bundled SVG icons**
The back and forward button icons shall be SVG files created for and bundled with the application (Qt resources), and shall not be resolved from the system icon theme. They shall be tinted through the design system (`HnIcon`) so enabled, hover, pressed and disabled states follow the palette.

- **Acceptance Criteria:**
  - `apps/files/icons/go-back.svg` and `go-forward.svg` exist with SPDX headers and are listed in the QML module `RESOURCES`.
  - The buttons' `icon.source` values begin with `qrc:` (no `image://icon/` theme lookup); a rendered window test asserts each button's icon has no load error.
  - With a nonexistent system icon theme (e.g. `QIcon::setThemeName("does-not-exist")` in the test), both icons still load.

---

## 7. Acceptance Criteria Summary

Each requirement above includes its own acceptance criteria bullet list (see sections 4 and 5). In addition to passing the criteria listed, the implementation must:

1. **Unit Tests:** Jump list logic (add, dedup, traversal, count, missing skip) verified by GoogleTest unit tests (plain C++, no QML).
2. **Controller Tests:** Cursor fix, pending restore, gating by mode/prompt, Quick Look closure verified by controller/integration tests.
3. **Rendered Window Tests:** Back/forward buttons present, positioned, enabled/disabled correctly, click navigates, breadcrumb x position unchanged, focus retained after click; verified by a rendered window test or manual inspection with screenshots.
4. **Tooling:** `task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint` all pass.

---

## 8. Traceability and Verification Matrix

| Requirement ID | Title | Verification Method | Test Type |
|---|---|---|---|
| REQ-F-001 | Jump list initialization | Unit test: instantiate JumpList, verify size == 0 | Unit Test |
| REQ-F-002 | Initial directory adds entry | Integration test: start app, verify first entry in list | Controller Test |
| REQ-F-003 | Entry deduplication on addition | Unit test: add A, add B, add A again; verify path appears once | Unit Test |
| REQ-F-004 | No-op navigation does not add entry | Unit test: add entry, navigate to same path, verify list unchanged | Unit Test |
| REQ-F-005 | Capacity management and entry eviction | Unit test: fill list to 100, add 101st, verify size == 100 and oldest removed | Unit Test |
| REQ-F-006 | Back navigation with valid target | Controller test: set up list with 3+ entries, Ctrl+O, verify index and directory change | Controller Test |
| REQ-F-007 | Back navigation with count prefix | Unit test: `3 Ctrl+O` with missing entries, verify landing on count-th valid entry | Unit Test |
| REQ-F-008 | Current directory recoverable by forward | Unit test: open A,B,C; back; forward; verify list unchanged and C restored | Unit Test |
| REQ-F-009 | Back with no target does nothing | Unit test: index at 0, Ctrl+O, verify no change | Unit Test |
| REQ-F-010 | Forward navigation with valid target | Controller test: Ctrl+I, verify index and directory change | Controller Test |
| REQ-F-011 | Forward navigation with count prefix | Unit test: `5 Ctrl+I`, verify landing on count-th valid entry | Unit Test |
| REQ-F-012 | Forward with no valid target does nothing | Unit test: at end of list, Ctrl+I, verify no change | Unit Test |
| REQ-F-013 | Cursor restore applied on load completion | Controller test: navigate back with restore target, async load completes, verify cursor on entry | Controller Test |
| REQ-F-014 | Cursor restore fallback to row 0 | Controller test: restore target not in listing, verify cursor at row 0 | Controller Test |
| REQ-F-015 | Cursor restore cancelled by user cursor movement | Controller test: navigate, press j before load, load completes, verify cursor not moved by restore | Controller Test |
| REQ-F-016 | Pending restore replaced on new navigation | Controller test: `h` then Places open / double `h` before load completes | Controller Test |
| REQ-F-017 | Watcher refresh does not re-apply restore | Controller test: restore applied, watcher triggers refresh, verify cursor not re-positioned | Controller Test |
| REQ-F-018 | Parent navigation cursor fix | Controller test: `h` from D to parent P, verify cursor on entry named basename(D) | Controller Test |
| REQ-F-019 | Parent navigation restore fallback to row 0 | Controller test: `h` to parent, target entry not found, verify cursor at row 0 | Controller Test |
| REQ-F-020 | Ctrl+O and Ctrl+I key recognition | Unit test: verify key codes, not text matching | Unit Test |
| REQ-F-021 | Navigation disabled in non-NORMAL modes | Controller test: Ctrl+O/I in VISUAL/INSERT mode, verify no navigation | Controller Test |
| REQ-F-022 | Navigation disabled when prompt is open | Controller test: prompt open, Ctrl+O/I, verify no navigation | Controller Test |
| REQ-F-023 | Count prefix consumed | Controller test: count set, Ctrl+O pressed, verify count cleared | Controller Test |
| REQ-F-024 | Quick Look closed on navigation | Controller test: Quick Look open, Ctrl+O pressed, verify Quick Look closed | Controller Test |
| REQ-F-025 | Missing entry detection and removal | Unit test: entry with missing path, traversal skips it, verify entry removed | Unit Test |
| REQ-F-026 | Skip status message for missing entries | Controller test: skip missing entries, verify status message shown | Controller Test |
| REQ-F-027 | No navigation if all entries missing | Unit test: all entries in direction are missing, verify no navigation | Unit Test |
| REQ-F-028 | Single synchronous stat per candidate | Unit test (canGoBack without fs access) + controller test (vanishing directory) | Unit + Controller Test |
| REQ-F-029 | Directory exists but fails to open not skipped | Controller test: inaccessible directory, navigate to it, verify error shown and entry retained | Controller Test |
| REQ-F-030 | Back button placed at far left | Rendered window test: verify back button x position < sidebarWidth | Rendered Window Test |
| REQ-F-031 | Forward button placed right of back | Rendered window test: verify forward button x > back button x | Rendered Window Test |
| REQ-F-032 | Button enabled state reflects availability | Rendered window test: verify dimmed/enabled state matches canGoBack/canGoForward | Rendered Window Test |
| REQ-F-033 | Button click performs navigation | Rendered window test: click back, verify navigation occurs | Rendered Window Test |
| REQ-F-034 | Buttons do not take focus | Rendered window test: click button, verify focus remains on listing | Rendered Window Test |
| REQ-F-035 | Buttons fit at minimum window width | Rendered window test: resize to 420px, verify buttons fully visible | Rendered Window Test |
| REQ-F-036 | Breadcrumb container position unchanged | Rendered window test: measure breadcrumb x before and after feature, verify no change | Rendered Window Test |
| REQ-F-037 | Button accessible names | Rendered window test / accessibility test: verify button accessible names | Rendered Window Test |
| REQ-F-038 | Cursor entry name captured on navigation away | Controller test: verify cursor entry name stored in jump list entry | Controller Test |
| REQ-F-039 | Empty name for empty/unloaded directory | Controller test: empty dir and rapid double open | Controller Test |
| REQ-NF-001 | History operations O(n) complexity | Code review; complexity analysis | Code Review |
| REQ-NF-002 | No filesystem access beyond existence check | Code review; profile to verify no extra I/O | Code Review |
| REQ-NF-003 | History logic testable without QML | Unit test compilation and execution | Unit Test |
| REQ-NF-004 | C++ and Qt conventions | Code review; clang-tidy/clang-format pass | Code Review |
| REQ-NF-005 | Code quality tools pass | Automated: `task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint` | Automated Build |
| REQ-NF-006 | QML files registered in Taskfile | Manual inspection of Taskfile.yml | Tooling Check |
| REQ-C-001 | In-memory, per-window history | Code review + controller test: fresh instance has empty list | Code Review + Controller Test |
| REQ-C-002 | Maximum 100 entries | Unit test: verify eviction at capacity | Unit Test |
| REQ-C-003 | Stored paths match currentPath | Controller test: cleaned path dedup; symlink path preserved | Controller Test |
| REQ-C-004 | Entry names are case-sensitive | Unit test: verify "Pictures" ≠ "pictures" in restore | Unit Test |
| REQ-C-005 | No rename/move tracking | Controller test: rename directory, navigate back, verify entry removed as missing | Controller Test |
| REQ-C-006 | No history UI list | Code review / visual inspection | Code Review |
| REQ-C-007 | Buttons use bundled SVG icons | Rendered window test (qrc source, no load error, missing theme) + visual inspection | Rendered Window Test |

---

## 9. Open Items and Risks

### 9.1 Open Items

1. **Button Icons** — resolved: bundled SVGs (REQ-C-007), see DESIGN.md §4.4.

2. **Integration with Existing Cursor Row Index Storage**
   - The pending restore mechanism relies on cursor entry NAME, not row index. Confirm that directoryModel or the listing can reliably map names to rows (after sorting/filtering).
   - Responsibility: Implementation review during cursor fix development.

3. **Stat Check Performance on Slow Filesystems**
   - If the stat check for missing entries adds noticeable latency during traversal (e.g., remote filesystems), consider caching or batching checks. Current spec allows O(n) per traversal (acceptable for n ≤ 100).
   - Responsibility: Performance testing during implementation.

4. **Watcher Refresh Differentiation**
   - The system must distinguish between directory listings loaded due to user navigation vs. watcher-triggered refresh. Verify that pending restore is only applied to user-initiated navigations, not watcher events.
   - Responsibility: Implementation review; signal/slot architecture verification.

5. **Quick Look Integration**
   - Confirm that Quick Look closure (REQ-F-024) is correctly signaled and handled; verify interaction with both Ctrl+O/I and header button clicks.
   - Responsibility: Integration test.

6. **Count Prefix Interaction with Vim Mode**
   - Verify that count prefix handling is compatible with existing handleKey dispatcher and Vim mode logic.
   - Responsibility: Integration with vim_mode_controller; review during implementation.

### 9.2 Risks

1. **Async Load Race Condition**
   - **Risk:** Directory load completes after a new navigation is initiated, restoring cursor to an old target.
   - **Mitigation:** Pending restore is keyed to directory load events; new navigation replaces pending restore. Strict sequencing in handleKey -> navigateX -> pending restore set.
   - **Verification:** Controller test for back-to-back rapid navigations.

2. **Missing Entry Loop Infinite**
   - **Risk:** All entries in a direction are missing; traversal logic loops endlessly trying to find a valid entry.
   - **Mitigation:** Traversal skips missing entries one by one and halts when the end of the list is reached. No navigation occurs if no valid entry is found.
   - **Verification:** Unit test with all entries missing; verify no hang.

3. **Cursor Position Corruption on Concurrent Updates**
   - **Risk:** Watcher refresh and user cursor movement occur simultaneously; restore applies incorrectly or cursor is moved unexpectedly.
   - **Mitigation:** Watcher events are coalesced by QFileSystemWatcher; cursor movement cancels pending restore. Serialization via event loop.
   - **Verification:** Stress test with rapid external file changes and user cursor movements.

4. **Icon Unavailability**
   - **Risk:** Chosen icon names are not available in the theme, resulting in missing/broken button icons.
   - **Mitigation:** Fallback to standard Qt icon names; test on target theme(s) early.
   - **Verification:** Visual inspection on multiple themes.

5. **Breadcrumb Layout Regression**
   - **Risk:** Adding buttons causes breadcrumb to shift or overlap due to layout changes.
   - **Mitigation:** Buttons are placed at the left edge within sidebarWidth; breadcrumb x is derived from sidebarWidth (no dynamic calculation). No layout change to breadcrumb.
   - **Verification:** Rendered window test; manual visual inspection at multiple window widths.

6. **Performance on Large History**
   - **Risk:** Although capped at 100, deduplication and traversal with count > 1 could be slow on edge cases.
   - **Mitigation:** O(n) is acceptable for n ≤ 100; profiling during implementation.
   - **Verification:** Performance test with full 100-entry list and large count values.

---

## 10. Notes for Implementer

- **Cursor Entry Name vs. Row Index:** The restore mechanism is based on entry NAME to handle sort/filter/watcher changes that shift row indices. Coordinate with directoryModel to ensure name-based lookup is available.
- **NORMAL Mode Gating:** Verify that vim_mode_controller exposes the current mode as a readable property or signal; navigation handlers check this before acting.
- **Prompt Gating:** Verify that TaskManager.hasPrompt is accessible and connected to button enabled state.
- **Quick Look Signal:** Confirm how quickLookOpen is managed; ensure closure is signaled correctly on navigation.
- **Parent Navigation Fix:** This is a first-class bug fix independent of history. Implement the restore mechanism in navigateParent() before extending it to history traversal.
- **Taskfile Integration:** If new .qml files are added, register them in Taskfile.yml immediately.

---

**End of Specification**
