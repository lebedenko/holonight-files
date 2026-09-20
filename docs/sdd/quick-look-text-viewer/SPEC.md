# SDD Spec — quick-look-text-viewer

## Overview

This specification defines a text viewer feature in Quick Look, enabling keyboard-driven inspection of plain-text files directly from the file listing. The feature replaces the existing wrapping text panel in `QuickLookOverlay.qml` with a line-number gutter, current-line highlighting, and arrow-key/vim navigation. The implementation gates Quick Look triggering to image files and `text/plain` MIME type only, pins the overlay to a single file (preventing j/k from navigating to other files), and caps file loading at 100 KiB to protect against pathologically large text files.

The current-line state (which row is "active") is owned by PreviewService on the C++ side, while QML only renders it and forwards keyboard input. This maintains the architectural invariant that C++ owns logic and QML is presentation.

## Non-Goals

- **Text wrapping** — Long lines are clipped to the viewport width; lines do not wrap.
- **Page navigation** — No Page Up/Page Down, Home/End, or g/G keys; only line-by-line movement via j/k/arrow keys.
- **Find/search** — No in-viewer search, regex, or jump-to-line.
- **Rich text** — No syntax highlighting, rendering of markup, or ANSI codes.
- **Persistent state** — Current line is transient; it resets to line 1 whenever Quick Look opens.
- **Line editing** — The viewer is read-only; no insert/delete/modify of line content.
- **Selective truncation** — Truncation is all-or-nothing: if the file exceeds 100 KiB, the entire load is capped; individual lines are not trimmed.

## Glossary

- **Current line** — The row highlighted by the viewer and moved by j/k/arrow keys. Transient state, always reset to line 1 when Quick Look opens. Owned by PreviewService on the UI thread.
- **Line ending** — One of `\n`, `\r\n`, or `\r`; all three are recognized as line breaks and treated equivalently.
- **Truncated** — File exceeded 100 KiB; only the first 102,400 bytes were read and decoded. A caption notice indicates truncation.
- **Pinned file** — Quick Look is bound to a single entry; j/k do not change the file being previewed. The user must close Quick Look and navigate the listing to view another file.
- **Text/plain MIME** — Files with the MIME type string `text/plain`. Other `text/*` types (e.g., `text/markdown`, `text/x-shellscript`, `text/json`) are NOT treated as previewable text and do not open Quick Look.

---

## Functional Requirements

### Gating and File Selection

#### REQ-F-001 — Quick Look gating to previewable types
**Ubiquitous:** The system shall open Quick Look only when the user presses Space on an image file or a file with MIME type `text/plain`. Quick Look shall not open for directories, archives, binary files, or other `text/*` types (such as JSON, Markdown, or source code); pressing Space on such entries shall have no effect.

**Acceptance Criterion:** Test the Space key on the following entries: (1) a JPEG image — Quick Look opens; (2) a plain-text `.txt` file — Quick Look opens; (3) a directory — Quick Look does not open, no overlay appears; (4) a `.json` file (MIME `text/json`) — Quick Look does not open; (5) a `.md` markdown file (MIME `text/markdown`) — Quick Look does not open; (6) a `.tar.gz` archive — Quick Look does not open.

#### REQ-F-002 — Pinned file in Quick Look
**Ubiquitous:** While Quick Look is open, the preview shall remain bound to the single file that was selected when Quick Look was opened. Pressing j, k, or any other navigation key shall not change the file being previewed or update the directory listing cursor; the listing remains visible behind the overlay but frozen.

**Acceptance Criterion:** Open Quick Look on file A, then press j (or k, or arrow keys) 10 times while Quick Look is still open. Verify that (1) the preview still shows file A's content, (2) the listing cursor has not moved to an adjacent file, and (3) the overlay remains open on file A. Close Quick Look and confirm the listing cursor is still at file A.

With valid back and forward history targets, Ctrl+O and Ctrl+I shall also preserve the open overlay, directory, cursor and preview. History navigation resumes after Quick Look closes. This supersedes navigation-history REQ-F-024's close-and-navigate behavior.

### Text Viewer Layout and Display

#### REQ-F-003 — Line-number gutter and current-row highlight
**Ubiquitous:** The text viewer shall display a left-aligned line-number column (showing sequential line numbers starting from 1) and shall highlight the current row with a distinct visual marker or background color. The line number for the current row and its text shall stand out from non-current rows.

**Acceptance Criterion:** Preview a text file with at least 10 lines. Verify that (1) line numbers 1–N are visible in the left column, (2) one row is visually highlighted (background color, text color, or marker), (3) the highlighted row's line number matches the current-line state reported by PreviewService, and (4) moving the current line (via j/k) updates the highlight to a new row.

#### REQ-F-004 — No line wrapping; lines clipped
**Ubiquitous:** Each file line shall occupy exactly one row. Text longer than the viewport width shall be clipped at the right edge, with no wrapping and no horizontal scrolling. The line-number gutter shall always remain visible.

**Acceptance Criterion:** Open a file containing one 200-character line. Verify that (1) the line occupies a single row, (2) characters beyond the viewport width are not visible, (3) its line number is visible, and (4) no horizontal scrollbar exists and the mouse wheel does not scroll horizontally.

#### REQ-F-005 — Monospace font, read-only text
**Ubiquitous:** The text viewer shall use the theme monospace font (as the existing text panel does), be read-only, and shall not allow user selection, copying, or editing of the displayed text.

**Acceptance Criterion:** Open a text file in Quick Look. Verify that (1) the font is monospace (all characters have equal width), (2) attempting to select text by clicking and dragging does not highlight or select text, (3) keyboard shortcuts for copy (Ctrl+C) do not copy text, and (4) the text input is not interactive.

### Current-Line State and Navigation

#### REQ-F-006 — Current line initialized to line 1
**Ubiquitous:** When Quick Look opens on a text file, the current line shall be set to line 1 (the first line of the file).

**Acceptance Criterion:** Open Quick Look on a text file. Verify that the line number 1 is highlighted, and if the file has multiple lines, the first line's text is highlighted. Close and reopen Quick Look on the same or a different text file; the current line always starts at line 1.

#### REQ-F-007 — j key moves current line down
**Event-driven:** When the user presses the j key while in Quick Look, the system shall move the current line down by one line.

**Acceptance Criterion:** Open a text file with at least 5 lines in Quick Look with current line at 1. Press j once; verify current line is now 2. Press j four more times; verify current line is now 6, then verify that pressing j does not move past the last loaded line.

#### REQ-F-008 — k key moves current line up
**Event-driven:** When the user presses the k key while in Quick Look, the system shall move the current line up by one line.

**Acceptance Criterion:** Open a text file with current line at line 5 (by pressing j four times from line 1). Press k once; verify current line is now 4. Press k four more times; verify current line is now 1, then verify that pressing k does not move above line 1.

#### REQ-F-009 — ArrowDown key moves current line down
**Event-driven:** When the user presses the ArrowDown key while in Quick Look, the system shall move the current line down by one line, behaving identically to the j key.

**Acceptance Criterion:** Open a text file with at least 3 lines. Press ArrowDown once from line 1; verify current line is now 2. Repeat; verify current line moves to 3, then clamping prevents further movement past the last line.

#### REQ-F-010 — ArrowUp key moves current line up
**Event-driven:** When the user presses the ArrowUp key while in Quick Look, the system shall move the current line up by one line, behaving identically to the k key.

**Acceptance Criterion:** Open a text file with current line at 3. Press ArrowUp once; verify current line is now 2. Press ArrowUp again; verify current line is now 1. Verify pressing ArrowUp at line 1 does not move above line 1.

#### REQ-F-011 — Navigation clamping at file boundaries
**State-driven:** While the current line is at the first line of the file, pressing k (or ArrowUp) shall have no effect and the current line shall remain at line 1. While the current line is at the last loaded line of the file (or the last line if the file fits entirely within the 100 KiB cap), pressing j (or ArrowDown) shall have no effect and the current line shall remain at the last line.

**Acceptance Criterion:** (1) Open a 3-line file with current line at 1. Press k five times; verify current line remains 1 and no error occurs. (2) Open the same file and navigate to line 3 (the last line). Press j five times; verify current line remains 3. (3) Open a file with 500 lines, truncated at 100 KiB (last loaded line is, say, 1200). Navigate to the last loaded line and press j; verify no movement past that line.

#### REQ-F-012 — Auto-scroll to keep current line visible
**State-driven:** While the text viewer is displaying a file, the view shall automatically scroll vertically to ensure the current line is always visible within the viewport. If the current line is above the visible area, the view shall scroll up; if below, the view shall scroll down.

**Acceptance Criterion:** Open a text file with 100+ lines. Use a window small enough that only 10–15 lines fit in the viewport. Navigate downward with j until current line reaches line 50 (beyond the initial viewport). Verify that the viewport has scrolled down so that line 50 is visible (and highlighted). Then navigate upward with k to line 5; verify the viewport has scrolled up to show line 5.

#### REQ-F-013 — Mouse wheel scrolls without changing current line
**Event-driven:** When the user scrolls the mouse wheel over the text viewer, the viewport shall scroll vertically, but the current-line state shall remain unchanged. Scrolling the wheel up shall scroll the view up; scrolling down shall scroll the view down.

**Acceptance Criterion:** Open a large text file (100+ lines) with current line at line 30. Scroll the mouse wheel down three times over the viewer. Verify that (1) the viewport has moved down (lines near the bottom of the original view are no longer visible), (2) the current line is still 30 (highlighted wherever it is now in the scrolled view), and (3) repeating with the wheel up scrolls back up while the current line remains 30.

#### REQ-F-014 — Space and Esc keys close Quick Look
**Ubiquitous:** Pressing the Space key or the Escape key while in Quick Look shall close the overlay and return focus to the directory listing. The listing cursor shall remain at the file that was being previewed.

**Acceptance Criterion:** Open Quick Look on a text file. Press Space; verify Quick Look closes immediately. Reopen on the same file, then press Escape; verify Quick Look closes. In both cases, confirm the listing cursor is still at the same file and the listing is interactive.

### Text Loading and Size Constraints

#### REQ-F-015 — 100 KiB size cap
**Ubiquitous:** The system shall load at most 102,400 bytes (100 KiB) from the text file. If the file is larger, only the first 102,400 bytes shall be read and decoded into lines; the remaining file shall not be loaded.

**Acceptance Criterion:** Create a text file with exactly 50,000 bytes and a second file with 200,000 bytes. Open both in Quick Look. For the 50 KiB file, verify that all content is loaded and the caption does not show "truncated". For the 200 KiB file, verify that only the first 100 KiB is loaded (by checking the number of lines and last line content), and pressing j from the last loaded line does not load additional content.

#### REQ-F-016 — Truncation notice in caption
**State-driven:** While a text file exceeds 100 KiB and only a partial load is displayed, the metadata caption shall include the notice "truncated" appended to the file size (e.g., "195.3 KiB · truncated").

**Acceptance Criterion:** Open a 200 KiB text file in Quick Look. Verify the caption reads approximately "200 KiB · truncated". Open a 50 KiB text file; verify the caption reads approximately "50 KiB" with no "truncated" notice.

#### REQ-F-017 — Empty file displays line 1
**Ubiquitous:** If the text file is empty (zero bytes), the viewer shall display a single empty line (line 1) with the current line set to line 1.

**Acceptance Criterion:** Create an empty `.txt` file. Open it in Quick Look. Verify that (1) exactly one line is visible (line number 1), (2) the line content is blank/empty, (3) the line is highlighted as the current line, and (4) pressing j or k does not move the current line.

#### REQ-F-018 — UTF-8 decoding with replacement characters
**Ubiquitous:** The system shall decode file bytes as UTF-8. If the file contains invalid UTF-8 sequences, they shall be replaced with the Unicode replacement character (U+FFFD, displayed as "​�"). No error message or decode failure shall be displayed; decoding shall always succeed with replacement.

**Acceptance Criterion:** Create a text file with valid UTF-8 text (e.g., "hello") followed by invalid byte sequences (e.g., raw `0xFF 0xFE`). Open it in Quick Look. Verify that the valid text is displayed correctly, the invalid bytes are replaced with visible replacement characters (boxes or "?"), and no error appears in the UI or logs.

#### REQ-F-019 — Line ending normalization: \\n, \\r\\n, \\r
**Ubiquitous:** The system shall recognize and treat the following as line breaks: LF (`\n`), CRLF (`\r\n`), and CR (`\r`). All three shall be treated as equivalent line separators.

**Acceptance Criterion:** Create three test files, each with 3 lines separated by (1) LF only, (2) CRLF, and (3) CR only. Open each in Quick Look. Verify that all three files display exactly 3 lines with the same content layout (accounting for platform line-ending display conventions, if any).

#### REQ-F-020 — Trailing line terminator does not create empty final line
**Ubiquitous:** If the file ends with a line terminator (`\n`, `\r\n`, or `\r`), that terminator shall not be interpreted as creating an additional empty final line. The last line of the file is the last content-bearing line, and a trailing terminator is metadata, not a line separator between content and an extra blank line.

**Acceptance Criterion:** Create a text file containing "line1\nline2\n" (content is two lines with a trailing LF). Open it in Quick Look and navigate to the last line. Verify that the last line is line 2 and its content is "line2"; no line 3 exists. Pressing j at line 2 has no effect.

#### REQ-F-021 — Unreadable file
**Unwanted behaviour:** If the text/plain file cannot be read when Quick Look opens (permissions, removed, I/O error), the system shall show the overlay caption with the existing preview error message, an empty viewer body, and shall ignore j/k without crashing.

**Acceptance Criterion:** Open Quick Look on a text/plain file made unreadable (chmod 000). The overlay shows the error message in the caption, no line rows are highlighted, and pressing j/k leaves the app responsive with no state change.

#### REQ-F-022 — Non-eligible target while open
**Conditional:** Where Quick Look is requested for an entry that is neither an image nor text/plain, the system shall leave `quickLookOpen` false and shall not change any other state.

**Acceptance Criterion:** Press Space on a directory and on a `.json` file; `quickLookOpen` stays false and the listing cursor and preview state are unchanged.

### Preview Pane and Docked Text Preview

#### REQ-F-023 — Docked PreviewPane text preview unchanged
**Ubiquitous:** The existing text preview panel in the docked PreviewPane (shown in the right sidebar when not in Quick Look) shall remain unchanged. It shall continue to display the full text content (or truncated text, per the existing truncation cap) without line numbers or current-line highlighting.

**Acceptance Criterion:** Open the file manager and select a text file. Verify the docked preview pane on the right shows the text content as before, with the existing layout, wrapping, and truncation behavior. Confirm that opening Quick Look does not alter the docked pane's display while Quick Look is active.

---

## Non-Functional Requirements

#### REQ-NF-001 — Current-line state owned by PreviewService (C++)
**Ubiquitous:** The current-line index and navigation logic shall reside in PreviewService on the C++ side. QML shall read the current-line state from PreviewService properties and forward key-press events to PreviewService methods; QML shall not maintain or update the current-line state directly.

**Acceptance Criterion:** Inspect the PreviewService interface for a method/property such as `currentLine`, `currentLineChanged`, and a slot `moveCurrentLine(int delta)`. QML code in the viewer does not maintain a local `currentLine` variable. All j/k/arrow-key presses invoke C++ methods to update the state.

#### REQ-NF-002 — QML is presentation only
**Ubiquitous:** The QML layer in QuickLookOverlay shall render the line-number gutter, current-row highlight, and text content; it shall forward keyboard input (j, k, arrow keys) to the PreviewService. QML shall not implement navigation logic, line splitting, or truncation detection.

**Acceptance Criterion:** A code review of `QuickLookOverlay.qml` and related QML files shows no line-splitting logic, no line-clamping loops, and no text-parsing beyond simple property binding. All navigation and state-management calls route to PreviewService via QML.invokable methods.

#### REQ-NF-003 — No binding loops during navigation
**Ubiquitous:** Navigation events (j/k presses) and auto-scroll operations shall not trigger circular bindings or re-evaluate computations in a way that causes Qt's binding-loop detector to fire.

**Acceptance Criterion:** Open Quick Look, navigate with j/k for 100+ presses, resize the window, and inspect the application logs. Zero messages contain "Binding loop detected" in the context of the Quick Look text viewer.

#### REQ-NF-004 — Loading stays off the UI thread
**Ubiquitous:** Reading and line-splitting of the text file shall not block the UI thread.

**Acceptance Criterion:** Opening Quick Look on a 100 KiB file over a slow/blocked read does not freeze the event loop: a timer in the test keeps firing while the load is pending.

---

## Constraints

#### REQ-C-001 — Replace hasText text panel in QuickLookOverlay.qml
**Ubiquitous:** The new text viewer shall replace the existing `quickLookText` TextEdit and its enclosing Flickable in `QuickLookOverlay.qml`. The old wrapping-text implementation shall be removed (or made dead code); the object names `quickLookText` and related elements shall be retained on new components with equivalent roles to preserve test harness references.

**Acceptance Criterion:** A diff of `QuickLookOverlay.qml` shows the old wrapping TextEdit replaced by the new line-gutter viewer. The object names are preserved: `quickLookText` still references a text-display element, and a new `quickLookLineNumbers` (or equivalent) references the line-number gutter. Existing tests that use `findChild("quickLookText", ...)` still pass.

#### REQ-C-002 — Tests that assert old j/k live-update behavior must be removed or rewritten
**Ubiquitous:** Tests in `preview_integration_test.cpp`, `smoke.cpp`, or other test suites that verify j/k navigation updates the Quick Look preview while staying open shall be removed or rewritten to expect the new pinned-file behaviour (j/k no-op for file navigation, only current-line movement in the text viewer).

**Acceptance Criterion:** Run the test suite with `task test` or `task ci`. All tests pass. A diff of the test files shows that assertions expecting "press j, preview updates to file B" have been removed, and new assertions expect "press j, preview stays on file A, current line moves".

#### REQ-C-003 — New tests for gating, pinning, navigation, truncation, and CRLF
**Ubiquitous:** New tests shall be added to verify the following: (1) Quick Look does not open for non-image, non-text/plain files; (2) j/k do not navigate the listing while Quick Look is open; (3) current-line navigation clamping works at file boundaries; (4) files > 100 KiB are truncated and the caption shows "truncated"; (5) empty files display line 1; (6) CRLF and CR line endings are correctly parsed into separate lines; (7) mouse wheel scrolls without changing the current line.

**Acceptance Criterion:** Automated tests exist for each of the seven behaviors listed above, following the naming conventions of the existing test suite, and the full test suite passes.

#### REQ-C-004 — README and documentation must be updated
**Ubiquitous:** The README, user documentation, and any feature descriptions that state "j/k navigate the directory listing while in Quick Look" shall be updated to reflect the new behavior: "j/k move the current line in text files only and do not change the file being previewed in Quick Look."

**Acceptance Criterion:** A search for "Quick Look" and "j/k" in documentation files (README.md, docs/user-guide.md, etc.) finds all references. Each reference is either removed (if outdated) or rewritten to describe the pinned-file and current-line behavior. A manual review confirms the new documentation is accurate.

#### REQ-C-005 — PreviewService interface for text line state and navigation
**Ubiquitous:** PreviewService shall expose a public read-only property `currentLineIndex` (or equivalent) that returns the 0-based or 1-based current line number, and a public Q\_INVOKABLE method `moveCurrentLineDown()` and `moveCurrentLineUp()` that update the state. These methods shall clamp the line index at file boundaries and emit a change signal when the line index changes.

**Acceptance Criterion:** The PreviewService header file includes the property and methods. QML code calls `preview.moveCurrentLineDown()` and reads `preview.currentLineIndex` or observes the currentLineChanged signal. The methods correctly clamp navigation and the signal is emitted on every line change.

#### REQ-C-006 — Text viewer data is separate from the docked text preview
**Ubiquitous:** The viewer's 100 KiB load cap and line splitting shall be implemented in PreviewService for Quick Look only, and shall not alter what the docked PreviewPane text preview loads or displays.

**Acceptance Criterion:** With a text file larger than the docked preview's existing limit selected, the docked PreviewPane content is identical before and after this feature, while Quick Look shows the first 102,400 bytes as lines.

#### REQ-C-007 — Quick Look trigger logic in file_command_router and directory_controller
**Ubiquitous:** The logic that gates Quick Look opening to image files and text/plain MIME types shall be implemented in `DirectoryController::handleKey()` or `FileCommandRouter` (C++ layer), not in QML. QML shall not perform MIME-type checks; it shall only display the overlay if `controller.quickLookOpen` is true.

**Acceptance Criterion:** A code review of `file_command_router.cpp` and `directory_controller.cpp` shows that the Space key handler checks `mimeType == "text/plain"` or `startsWith("image/")` before setting `quickLookOpen = true`. QML code has no conditional checks on MIME type for opening Quick Look.

---

## Test Fixtures and Infrastructure

Tests reuse the helpers in `tests/preview_fixtures.h` and the existing `*_test_access.h` headers where they fit; any new fixtures (large, empty, CRLF, CR, invalid-UTF-8 files) are added there.

---

## Implementation Notes

### Architecture Summary

1. **C++ PreviewService** — Owns the current-line state (`currentLineIndex`). Provides navigation methods (`moveCurrentLineUp()`, `moveCurrentLineDown()`, `resetCurrentLine()`), auto-clamping at file boundaries. Emits `currentLineChanged` signal on every update.
2. **QML QuickLookOverlay** — Binds to `preview.currentLineIndex` and renders the line-number gutter and highlight. Forwards j/k/arrow-key presses to PreviewService methods via C++.
3. **PreviewPane (docked)** — Unchanged; continues to use the existing text preview without line numbers or current-line tracking.
4. **FileCommandRouter / DirectoryController** — Implements the gating logic; only opens Quick Look for image and text/plain files.

### Key State Transitions

- **Open Quick Look** → `currentLineIndex` reset to 0 (or 1, depending on indexing convention).
- **Close Quick Look** → current line is irrelevant until the next open, which resets it.
- **Navigate j/k in text viewer** → current-line index updated, change signal emitted, QML re-renders the highlight.
- **Navigate (j/k at file boundaries)** → no state change; clamping prevents out-of-bounds movement.

---

## Acceptance Criteria Summary

Each requirement includes an independently verifiable acceptance criterion. In addition to per-requirement criteria, the following system-level criteria must be satisfied:

1. **Feature gating** — Quick Look does not open for directories, archives, or non-text/plain text files (REQ-F-001).
2. **File pinning** — j/k presses in Quick Look do not navigate the directory listing (REQ-F-002).
3. **Line navigation** — j/k/arrow keys move the current line within a file; navigation clamps at boundaries (REQ-F-007 through REQ-F-011).
4. **Viewport auto-scroll** — The viewport scrolls to keep the current line visible (REQ-F-012).
5. **Mouse wheel** — Scrolling the mouse wheel does not change the current line (REQ-F-013).
6. **Size cap and truncation** — Files > 100 KiB are loaded only partially; the caption shows "truncated" (REQ-F-015, REQ-F-016).
7. **Special cases** — Empty files show line 1; invalid UTF-8 is replaced; CRLF and CR are recognized; trailing line terminators do not create blank final lines (REQ-F-017 through REQ-F-020).
8. **Architecture** — Current-line logic lives in PreviewService (C++); QML is presentation only (REQ-NF-001, REQ-NF-002).
9. **Tests and docs** — Old j/k live-update tests are removed or rewritten; new tests cover gating, pinning, navigation, truncation, and CRLF. README and docs are updated (REQ-C-002, REQ-C-003, REQ-C-004).

---
