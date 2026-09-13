# SDD Tasks — navigation-history

- [x] T-001: Pending cursor restore mechanism and `h` parent-navigation fix
  - REQs: REQ-F-013, REQ-F-014, REQ-F-018, REQ-F-019, REQ-F-024, REQ-C-004
  - Check: `DirectoryController.NavigateParentPositionsCursorOnChildBasename`, `NavigateParentFallsBackToRowZeroWhenBasenameMissing`, `RestoreAppliedOnLoadCompletionToNamedRow`, `RestoreFallsBackToRowZeroWhenTargetNotVisible` and `RestoreNotAppliedAgainstEmptyListingDuringLoadReset` pass, and every pre-existing `DirectoryController.*` test still passes.

- [x] T-002: Restore cancellation, replacement and watcher-refresh immunity
  - REQs: REQ-F-015, REQ-F-016, REQ-F-017
  - Check: `DirectoryController.ExplicitJCancelsPendingRestoreBeforeLoadCompletes`, `SecondNavigationBeforeSettleReplacesPendingRestore` (double-`h` and `h`-then-Places cases) and `WatcherRefreshAfterRestoreDoesNotMoveCursor` pass.

- [x] T-003: `JumpList` value class with unit tests
  - REQs: REQ-F-001, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-025, REQ-F-027, REQ-F-028, REQ-NF-001, REQ-NF-002, REQ-NF-003, REQ-C-002, REQ-C-005
  - Check: every test listed in DESIGN §8.1 exists in `tests/jump_list_test.cpp` and passes, including `RecordVisitDedupsWithoutTruncatingForwardEntries`, `TraverseBackWithCountSkipsMissingAndLandsOnValidStep` and `TraverseInvokesPredicateOncePerExaminedCandidate`, with no QML or filesystem use in that file.

- [x] T-004: Integrate `JumpList` into `DirectoryController` (history API, gating, status)
  - REQs: REQ-F-002, REQ-F-021, REQ-F-022, REQ-F-023, REQ-F-026, REQ-F-029, REQ-F-033, REQ-F-038, REQ-F-039, REQ-C-001, REQ-C-003
  - Check: the DESIGN §8.2 history tests pass, including `InitialOpenAddsFirstJumpListEntryWithEmptyCursorName`, `CursorEntryNameCapturedOnEveryNavigationAwayMethod`, `EmptyOrUnloadedDirectoryCapturesEmptyCursorName`, `BackSkipsMissingEntriesAndReportsSkippedStatusMessage`, `AllEntriesMissingInDirectionShowsStatusAndDoesNotNavigate`, `InaccessibleDirectoryIsTraversedNotSkippedAndKeptInHistory`, `HistoryNavigationInertInVisualSearchInsertModes`, `HistoryNavigationInertWhilePromptOpen`, `GoBackAndGoForwardAlwaysUseCountOneIgnoringPendingCount` and `HistoryPathIsCleanedAbsoluteNotSymlinkResolved`.

- [x] T-005: Bundled back/forward SVG icons
  - REQs: REQ-C-007
  - Check: `apps/files/icons/go-back.svg` and `go-forward.svg` exist with SPDX headers and literal `stroke="#000000"`, are listed in the QML module `RESOURCES`, any repo licensing declaration covering `apps/files/icons/` includes them, and `task build` succeeds.

- [x] T-006: Header back/forward buttons with rendered window tests
  - REQs: REQ-F-030, REQ-F-031, REQ-F-032, REQ-F-033, REQ-F-034, REQ-F-035, REQ-F-036, REQ-F-037, REQ-C-007
  - Check: new `tests/history_navigation_window_test.cpp` tests `BackButtonSitsLeftOfForwardButtonWithinSidebarWidth`, `ButtonsEnabledStateTracksCanGoBackForwardModeAndPrompt`, `ClickingBackButtonNavigatesLikeCtrlO`, `ClickingForwardButtonNavigatesLikeCtrlI`, `ClickingButtonLeavesFocusOnListingAndVimKeysStillWork`, `BothButtonsFitWithinSidebarAtMinimumWindowWidth`, `BreadcrumbContainerXUnchangedAtDefaultAndMinimumWidth`, `ButtonsHaveBackAndForwardAccessibleNames` and `HistoryButtonIconsLoadFromBundledResources` (including with a nonexistent icon theme) pass.

- [x] T-007: Ctrl+O / Ctrl+I shortcuts with key-delivery window tests
  - REQs: REQ-F-007, REQ-F-011, REQ-F-020, REQ-F-021, REQ-F-022, REQ-F-023, REQ-F-024
  - Check: window tests `CtrlOAndCtrlIKeyClicksNavigateFromListingFocus`, `CtrlIDoesNotMoveFocusOrNavigateInInsertMode`, `CountThenCtrlOTraversesCountEntries` and `QuickLookOpenThenCtrlOClosesItAndNavigates` pass, with the DESIGN §4.3 fallbacks applied only if the corresponding test fails without them.

- [x] T-008: Manual verification on the real Hyprland session (1.5× scale)
  - REQs: REQ-F-018, REQ-F-020, REQ-C-007
  - Check: on the user's display, physical Ctrl+O/Ctrl+I navigate back/forward from the listing, `h` out of `~/Pictures` lands on `Pictures`, and both arrow icons render crisp and palette-tinted (dimmed when disabled) — confirmed by the user.

- [x] T-009: Final tooling pass
  - REQs: REQ-NF-004, REQ-NF-005, REQ-NF-006, REQ-C-006
  - Check: `task build`, `task test`, `task format-check`, `task tidy` and `task qml-lint` all exit 0, and no history list/dropdown UI or `:jumps` command exists.
