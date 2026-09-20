# SDD Tasks — quick-look-text-viewer

- [x] T-001: Implement TextLines::decodeUtf8 and TextLines::splitLines
  - REQs: F-019, F-020
  - Check: Pure C++ functions parse LF/CRLF/CR line endings, omit phantom empty line after trailing terminator.

- [x] T-002: Add text_lines_test.cpp unit tests
  - REQs: F-017, F-018, F-019, F-020
  - Check: Tests pass for empty input yielding `[""]`, UTF-8 replacement of invalid bytes, all three terminator types, and mid-sequence UTF-8 tail trimming.

- [x] T-003: Update TextPreviewService::readHead to 102400-byte cap
  - REQs: F-015
  - Check: Reading a 200 KiB file yields exactly 102,400 bytes and `wasTruncated` is true.

- [x] T-004: Add TextPreviewResult::lines field and remove ::content
  - REQs: F-015, F-016
  - Check: Result carries `QStringList lines` and truncation metadata, and existing code paths compiling.

- [x] T-005: Add text_preview_service_test.cpp tests for truncation and encoding
  - REQs: F-015, F-018, F-021
  - Check: Tests pass for 102,400-byte boundary files, unreadable files yielding empty lines, and UTF-8 decoding.

- [x] T-006: Implement TextLineModel QAbstractListModel
  - REQs: F-003
  - Check: Model exposes `lineText` and `lineNumber` roles with rowCount matching input lines.

- [x] T-007: Add TextLineModel unit tests
  - REQs: F-003
  - Check: Tests pass for role data, rowCount after setLines, and clear reset.

- [x] T-008: Add PreviewService currentLineIndex property and moveCurrentLine methods
  - REQs: F-006, F-007, F-008, F-011, NF-001, C-005
  - Check: Property reads 0-based index clamped at 0 and lineCount-1, moveCurrentLineDown/Up emit signal only on change.

- [x] T-009: Implement PreviewService::quickLookEligible gating logic
  - REQs: F-001, F-022, F-021
  - Check: Method returns true for image/* and text/plain MIME types and application/x-zerosize, false for other text/* and non-eligible entries.

- [x] T-010: Add gate_mime field to PreviewResult for worker MIME reporting
  - REQs: F-001, F-021, F-022
  - Check: Worker reports sniffed MIME or extension-based fallback for unreadable files without I/O on UI thread.

- [x] T-011: Add preview_service_test.cpp tests for currentLine state and navigation
  - REQs: F-006, F-007, F-008, F-011, NF-001, NF-004, C-005
  - Check: Tests pass for init to line 1, clamping at boundaries, signal once per change, -1 for empty lines, reset on setQuickLookActive(true), and worker blocking latency.

- [x] T-012: Add FileCommand::MoveQuickLookLine command kind to router
  - REQs: F-007, F-008, F-009, F-010
  - Check: Command carries count +1/-1 and is parsed by handleKey in Quick Look context.

- [x] T-013: Implement FileCommandRouter::handleQuickLookKey for Quick Look context
  - REQs: F-002, F-007, F-008, F-009, F-010, F-014, NF-001
  - Check: Space/Escape close overlay, j/ArrowDown/k/ArrowUp emit MoveQuickLookLine, all other keys consumed no-op.

- [x] T-014: Add DirectoryController::execute handler for MoveQuickLookLine
  - REQs: F-007, F-008, F-009, F-010, NF-001
  - Check: Invokes preview.moveCurrentLine(count) and does not emit listing changed.

- [x] T-015: Implement PreviewSelection pin guard for Quick Look open state
  - REQs: F-002, F-014, F-022
  - Check: While Quick Look open, cursor divergence from pinned file closes overlay and resyncs.

- [x] T-016: Add InspectionKeys arrow normalization for popup context
  - REQs: F-009, F-010
  - Check: Qt::Key_Up/Down are normalized to "ArrowUp"/"ArrowDown" and passed through popup filter in Quick Look.

- [x] T-017: Add directory_controller_test and integration tests for pinning and gating
  - REQs: F-001, F-002, F-007, F-008, F-009, F-010, F-014, F-022, NF-001, C-002, C-003
  - Check: Gating test: Space opens for image and text/plain, closes no-op for directory/.json/.md/.tar.gz; pinning test: j/k keep cursor and preview on same file for 10 presses.

- [x] T-018: Rewrite preview_integration_test and smoke.cpp j/k live-update tests
  - REQs: C-002
  - Check: Tests expecting "j updates preview to next file" are rewritten to "j moves current line in pinned file", and test suite passes.

- [x] T-019: Replace QuickLookOverlay Flickable+TextEdit with ListView text viewer
  - REQs: F-003, F-004, F-005, C-001
  - Check: ListView delegates render line number gutter, line text with no wrap/clip, highlight current row, and preserve `quickLookText`/`quickLookLine` object names.

- [x] T-020: Add QuickLookOverlay caption truncation notice
  - REQs: F-016
  - Check: Caption displays "size · truncated" when file exceeds 100 KiB, and "size" only when within limit.

- [x] T-021: Add QML Connections for auto-scroll and wheel independence
  - REQs: F-012, F-013
  - Check: currentLineIndexChanged signal triggers positionViewAtIndex(Contain), and mouse wheel moves contentY without changing currentLineIndex.

- [x] T-022: Add rendered smoke tests for gutter/highlight/clipping/scroll/wheel/binding-loops
  - REQs: F-003, F-004, F-012, F-013, NF-003, C-001
  - Check: Tests pass for 100+ j presses with no "Binding loop detected" log, long line clipped and no horizontal scroll, and viewport follows index movement.

- [x] T-023: Add test fixtures for empty, CRLF, CR, invalid-UTF-8, and size-boundary text files
  - REQs: F-017, F-018, F-019, F-020, F-015, F-016
  - Check: Fixtures in preview_fixtures.h are callable and generate test files matching spec corner cases.

- [x] T-024: Update README and docs for pinned-file and current-line behavior
  - REQs: C-004
  - Check: Documentation describing j/k behavior in Quick Look shows current-line movement only, no file navigation.

- [ ] T-025: Final verification: all REQs covered, no binding loops, smoke suite passes
  - REQs: NF-001, NF-002, NF-003, NF-004, C-005, C-006, C-007
  - Check: Every F-*, NF-*, C-* requirement maps to at least one task; rendered acceptance test suite passes.
  - Manual native acceptance: user confirmed wheel/drag scrolling, focus restoration, copy behavior and responsiveness on 2026-09-21 with no issues.
  - Pending: isolated runtime verification before publication; see VERIFICATION.md.

- [x] T-026: Fix review findings: block history traversal while Quick Look is pinned and reconcile SDD status
  - REQs: F-002, F-014
  - Check: Both Ctrl+O/Ctrl+I preserve the open overlay, directory, cursor and preview; history works after close. Direct controller history calls obey the same guard.
