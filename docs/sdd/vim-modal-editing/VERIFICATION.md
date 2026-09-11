# Vim modal editing verification

Review remediation, 2026-09-10. The approved implementation plan authorizes this
cycle. Existing workspace changes were preserved; only Files sources were edited.
Installed dependencies were used by task deps. Generated logs/captures are under build/.

## Project checks

| Command | Result | Evidence |
| --- | --- | --- |
| `task deps` | Pass | `build/review-deps.log` |
| `task build` | Pass | `build/review-build.log` |
| `task test` | Pass: 5/5 CTest targets; 154 passing cases, 2 opt-in skips | `build/review-test.log`, `build/test/Testing/Temporary/LastTest.log` |
| `task build PRESET=release` | Pass | `build/review-release.log` |
| `task format-check` | Pass | `build/review-format-check.log` |
| `task tidy` | Pass: all 30 translation units | `build/review-tidy.log` |
| `task qml-lint` | Pass | `build/review-qml-lint.log` |
| `task license-check` | Pass | `build/review-license.log` |
| `task install-check` | Pass | `build/review-install.log` |

REUSE's first sandbox run could not create its multiprocessing socket. The approved
escalated rerun passed. Initial test runs caught cursor initialization, hidden-editor
focus restoration, SEARCH focus during cursor jumps, and shared font-weight binding
issues; fixes are covered by the passing window test. Initial tidy findings were
missing braces and an implicit narrowing conversion. A later concurrent test edit
invalidated one analyzer snapshot (null-character diagnostics; current source has
zero null bytes). That transient log is retained as `build/review-tidy-transient.log`;
the clean final rerun passed on unchanged sources, with no user-code diagnostics.
The touch fixture uses future atime to prevent unrelated preview reads from causing
Linux relatime updates while checking the touch operation.

## Requirement evidence

| Requirements | Passing regression evidence |
| --- | --- |
| REQ-R-001/002; F-012–017, F-037–041 | ExclusiveCreationPreservesRacingCollisionAndRetainsEditor; CreationRejectsFilesDirectoriesAndDanglingLinks; UnchangedTouchPreservesAtimeForFilesDirectoriesAndLinks; MissingTouchFailsWithoutRecreatingAndPermissionsRetainCreate; RenameDirectoriesAndLinksUsesCapturedPathsAndNeverOverwrites |
| REQ-R-003/004 | PendingScansCannotMoveEditorAndCancelReconciles; SuspensionRejectsQueuedInitialAndRefreshDeliveriesThenReconciles; ShutdownCompletesWhileSuspendedWithQueuedDeliveries; NavigationCancelsAllModesAndClearsChordsAndSearch |
| REQ-R-005; F-010/011 | PlaceholderAdjacentToExactAnchorAcrossDirectionsGroupsAndTies; EmptyPlaceholderAndEditingLocks |
| REQ-R-006; F-025–029 | SearchRepeatInvalidatesAndEscapeRestoresFilenameIdentity; NAndShiftNRepeatTheLastSearchInNormalModeWithWraparound; FuzzyMatcher and VimModeController suites |
| F-001–011, F-018–024, F-042; NF-001 | ModalEditingWindowKeyboardAndHighlighting plus controller/mode/validator suites |
| F-003/004; C-002/003 | QuickLookConsumesSpaceBeforeDelegateActivationAndRestoresFocus; existing populated/window fullscreen tests |

The modal window test uses actual QQuickWindow key events and checks focus, all six
INSERT entry variants, Enter/Escape, literal n/N, shortcut suppression, navigation
cancellation, literal markup characters, match font weight and cursor positions.
The entire suite runs 156 cases across 17 suites. The two skips are the older
opt-in `Files.NativeInspectionAcceptance` and
`DirectoryPerformance.RenderedRowsAndInteractionWhileLoading`; this review does not
claim to close those Stage 1/2 performance gates.

## Visual and latency evidence

`python3 build/review-capture.py` runs the modal window regression with
`QT_QPA_PLATFORM=offscreen`, `QSG_RHI_BACKEND=software`, the installed QML/library
paths, `HOLONIGHT_APPEARANCE_FILE=tests/fixtures/{dark,light}.toml`,
`QT_SCALE_FACTOR={1,1.25,1.5}`, and `FILES_CAPTURE_PREFIX=build/review-visual/...`.
All six runs passed. All 24 NORMAL/INSERT/SEARCH/VISUAL images were inspected:
literal `<b>nN&.txt`, highlighted nN runs, metadata, editor placement, and mode
indicators remain readable and aligned. At 1.25× the installed shared text-field
border has minor fractional rasterization at its corners; no Files content overlaps.

Each run measured 20 alternating valid/invalid editor updates through the QML
`hasError` feedback binding. Maximum milliseconds per run:

| Theme | 1× | 1.25× | 1.5× |
| --- | --- | --- | --- |
| Dark | 0.642 | 0.588 | 0.603 |
| Light | 0.531 | 0.524 | 0.512 |

These 120 measurements meet the 200 ms validation-feedback gate; they measure
binding feedback, not display scanout. Logs and images are in `build/review-visual/`.

An approved native Wayland run of the same window test also passed (20 edits,
maximum 0.539 ms). `build/review-native.log` and `native-modal-*.png` record the
run; all four captures were inspected. This uses the active compositor's supplied
window sizing/scaling, not a claim of a full native theme/scale matrix.

## T-021 — Stage 1-2 keybinding backward compatibility (2026-09-11)

`DirectoryController.Stage1And2KeybindingsStillDispatchThroughHandleKeyUnchanged`
(`tests/directory_controller_test.cpp`) exercises j/k, gg/G, `.` hidden-files toggle,
`s` sort-direction toggle and Space Quick Look entirely through `handleKey()`, confirming
the Stage 3 dispatcher rewrite (`handleNormalOnlyKey`/`handleNormalToggleAndNavigationKey`/
`handleModeTransitionKey`) did not change Stage 1-2 behavior. `ctest --test-dir build`:
6/6 suites pass (37/37 `DirectoryController` cases); `format-check` clean.

## Remaining acceptance

Manual IME and assistive-technology interaction and a full native desktop theme/scale
matrix remain pending. Native Stage 1/2 performance acceptance is separate, as above.
Large-list fuzzy-search latency has no measured guarantee: scoring is quadratic in
candidate length times query length, with an existing subsequence prefilter.
No COMMAND palette or VISUAL filesystem operations were added.

## Work ownership

Spark was assigned exclusive comparator/proxy regression work but failed before
execution because its model quota was exhausted (reset reported September 16).
The main agent completed that isolated work locally under the plan's fallback.
Previous unsupported completion claims are superseded by this evidence record.
