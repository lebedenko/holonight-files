# Verification — quick-look-text-viewer

Date: 2026-09-21. Base: `550e662` plus the uncommitted working tree of this cycle (nothing committed or published).

Status: Implementation and user-reported manual native acceptance complete; isolated runtime verification remains pending.
The review corrections were authorized by the user on 2026-09-21.

## Automated evidence before review corrections

| Check | Result |
|---|---|
| `task check` (debug + release builds, `ctest --preset test`, `format-check`, `tidy`, `qml-lint`, `reuse lint`, `install-check`, `qml-import-check`, `qmltypes-check`) | exit 0; ctest 9/9; no compiler or tidy warnings in the log |
| `files-smoke` gtest suite | 534 passed, 0 failed |

The first `task check` run failed at `tidy` on four findings (identifier length, `modernize-use-ranges`, `readability-qualified-auto`). They were fixed and the full check was rerun from the start.

## Requirement coverage

| REQ | Evidence |
|---|---|
| F-001, F-022 | `DirectoryController.SpaceOpensQuickLookOnlyForImagesAndPlainText`; `PreviewService.QuickLookEligibilityFollowsTheMimeGate` |
| F-002, F-014 | `DirectoryController.QuickLookStaysPinnedToItsFileWhileJKAndArrowsMoveTheCurrentLine`; `QuickLookSwallowsEveryOtherKeyWithoutMovingAnything`; `PreviewIntegration.QuickLookOpensPinned…`; `DirectoryController.HistoryNavigationStaysPinnedUntilQuickLookCloses`; `WindowHistoryNavigation.QuickLookConsumesHistoryShortcutsUntilClosed` |
| F-003, F-005 | `Files.QuickLookTextFrameFillsPreviewBoundsWithMonospaceView`; `Files.QuickLookViewerHighlightsAndScrollsToTheCurrentLine` |
| F-004 | `Files.QuickLookLongLineIsClippedWithoutHorizontalScrolling` |
| F-006 to F-011 | `PreviewService.*CurrentLine*`; `DirectoryController.QuickLookLineMovementClampsAndDoesNotEmitListingChanges`; `InspectionKeys.ArrowKeysReachTheQuickLookLineMoverOnlyInPopups` |
| F-012 | `Files.QuickLookViewerHighlightsAndScrollsToTheCurrentLine` |
| F-013 | `Files.QuickLookWheelScrollsTheViewportWithoutChangingTheCurrentLine` (synthetic in-process wheel events) |
| F-015, F-016 | `TextPreviewService` boundary tests (50,000 / 102,400 / 102,401 / 200,000 bytes); caption assertion in the text-frame smoke test |
| F-017 to F-020 | `TextLines.*`; `TextPreviewService` empty / invalid-UTF-8 / CRLF / CR tests; `Files.QuickLookEmptyFileShowsOneHighlightedEmptyLine` |
| F-021 | `PreviewService.UnreadableTextFileHasNoLinesButStaysEligible`; `Files.QuickLookCompactCardShowsErrorForUnreadableAndCorruptFiles` (skipped when running with permission-bypassing privileges) |
| NF-001, NF-002, C-005, C-007 | State and gating live in `PreviewService` / router / controller; QML has bindings and two `Connections` only (code review of `QuickLookOverlay.qml`) |
| NF-003 | `Files.QuickLookLineNavigationAndResizeProduceNoBindingLoops` (180 line moves plus 60 back-steps, three window sizes, 0 warnings) |
| NF-004 | `PreviewService.LoadingTextNeverBlocksTheUiThread` |
| C-001 | `quickLookText` is the `ListView`; `quickLookLine`, `quickLookLineNumbers`, `quickLookLineText`, `quickLookCurrentLine` added |
| C-002, C-003 | Old live-update tests rewritten (see below); new tests listed above |
| C-004 | `README.md`, `docs/BACKLOG.md` updated |
| C-006 | The 100 KiB cap and line splitting are worker-side and feed only Quick Look |

## Implemented deviations from the spec

- **F-023 / C-006 premise:** the docked `PreviewPane` never had a text panel; nothing else read the old 64 KiB `textContent`, so it was removed. `PreviewPane.qml` is unmodified.
- **F-001 widening:** `application/x-zerosize` is also eligible so an empty file without a `.txt` extension opens as one empty line.
- **F-016 wording:** the caption keeps the app's SI size formatting (`8.3 MB · truncated`), not KiB.
- **F-021:** an unreadable or corrupt file shows the existing compact error card (no empty text frame); the line index is -1 and `j`/`k` are no-ops.
- **C-005 / NF-001 naming:** `currentLineIndex` (0-based, -1 = none) with `currentLineIndexChanged`; `moveCurrentLine(int)` is C++ only, `moveCurrentLineDown()/Up()` are `Q_INVOKABLE`.
- **Pin guard:** if the listing shifts the entry under the cursor while Quick Look is open, Quick Look closes rather than retargeting.

## Removed tests (obsolete under pinning)

`QuickLookImageMetadataLineShowsDimensionsThenSizeOnly`, `QuickLookRetainsSettledGeometryWhilePending`, `QuickLookPendingResizeKeepsCaptionInsideCard` (all relied on `j` stepping to another file while open) and `QuickLookCompactCardShowsDirIconAndStaysSmall` (directories can no longer open Quick Look). The compact error card, reopen geometry and requested-size behaviours remain covered.

## Manual native acceptance

On 2026-09-21, the user confirmed that the requested native checks behaved as expected with no issues:

- Mouse-wheel and drag scrolling.
- Focus returning to the listing after closing Quick Look.
- Ctrl+C copying nothing.
- Responsiveness during use.

These are user-reported manual results, not automated native checks or measured latency benchmarks.

## Not run / pending

- `task isolated-runtime-check` (needs Docker) has not been run; the AGENTS.md rule calls for it before publication.
- Older `quick-look-redesign` and `quick-look-cpp-presentation` verification documents record live-update-on-`j`/`k` behaviour that this cycle supersedes; they are kept as history.

## Review corrections

- History traversal now respects the Quick Look pin in `NavigationSession`, covering window shortcuts, popup key dispatch and direct controller history calls. This supersedes navigation-history REQ-F-024 and its old close-and-navigate tests.
- The design status now describes the implemented state without claiming an unrecorded design approval. T-025 remains open for the pending acceptance checks above.
- Both updated regression tests failed against the pre-fix implementation and passed with the shared history guard.
- Focused run: `files-smoke --gtest_filter='DirectoryController.HistoryNavigationStaysPinnedUntilQuickLookCloses:WindowHistoryNavigation.*:InspectionKeys.*:DirectoryController.*QuickLook*'` with the repository's offscreen Qt environment: 24 passed.
- The sandboxed `task check` stopped because the existing `FileOperationService.SocketAndDeviceCopiesAreRejectedWithoutReading` fixture could not bind a Unix socket (`Permission denied`). An authorized unrestricted rerun passed CTest 9/9; `files-smoke` passed 534 tests with the two opt-in native/performance tests skipped. `task check` then completed with exit 0, including formatting, clang-tidy, QML lint, license checks, staged installation, import policy and QML metadata checks. `git diff --check` also passed. Isolated runtime verification remains pending as listed above; the user subsequently confirmed manual native acceptance.
