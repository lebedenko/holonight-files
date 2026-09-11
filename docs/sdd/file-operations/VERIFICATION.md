# File operations safety verification

Safety revision, 2026-09-10. The user-supplied implementation plan approves the
revised SPEC.md, DESIGN.md and T-130–135. This record supersedes the earlier
verification claims about permanent deletion and incomplete-move cleanup.

## Changes verified

- Endpoints are checked before mutation: same entries, hard-link aliases, directory
  self-moves and descendant destinations through ordinary or symlinked parents fail.
  Files and symlinks stage output beside the destination; commit replaces the entry
  without following destination symlinks. Non-overwrite moves use RENAME_NOREPLACE.
- Directory merges record nested skips separately from I/O failures. Any incomplete
  merge or EXDEV copy retains the entire source tree. Summaries count an incomplete
  top-level item once and explicitly explain retained sources and destination copies.
- FIFO, socket and device copies fail before opening; same-device FIFO moves rename
  successfully. EXDEV special-file fallback fails without removing the source.
- Trash directory selection and writes expose failure kind, path and reason. Existing
  private directories require ownership by the current user and mode 0700; symlinks
  and unexpected permissions are rejected without repair. Shared sticky .Trash
  retains its validated user-directory to .Trash-$uid fallback.
- Trash metadata is completed before a same-filesystem non-overwriting rename.
  Failed moves and failed metadata writes roll back only this attempt's metadata.
  There is no permanent-delete prompt, API or fallback, nor cross-device home copy.
- Tasks and prompts carry IDs. Stale callbacks/responses are discarded, cancellation
  is checked after semaphore wakeup, and Ctrl+C/conflict Cancel discard queued work.
  Trash confirmation waits at the front of the queue before any trash I/O.
- Window event filtering captures prompt keys ahead of editors and shortcuts. Escape
  declines trash but leaves conflicts unresolved. Quick Look closes on prompt arrival.
  Editor text, cursor, selection, mode and focus survive prompt responses and Ctrl+C.
  Incomplete moves report whether destination output was actually committed.

Trash behavior follows the [freedesktop Trash specification](https://specifications.freedesktop.org/trash/latest/),
using its option to refuse trashing when no suitable location is available.
Owner-only validation is the stricter policy approved for this application.

## Regression evidence

| Area | Tests |
| --- | --- |
| Identity and descendants | `FileOperationService.SameEntryHardLinkAndDescendantDestinationsAreRejectedBeforeMutation` |
| Safe destination replacement | `OverwriteReplacesDestinationSymlinkWithoutTouchingTarget`, `FailedOrCancelledOverwritePreservesDestinationAndNonOverwriteMoveRefusesIt`, `CancellationDuringStagedOverwritePreservesOldDestination` |
| Merge source retention | `IncompleteMergeRetainsEverySourceChild`, `DirectoryMergeWithNestedErrorRetainsAllSourceEntries`; `TaskManager.IncompleteDirectoryMoveCountsOneItemAndReportsSkipsSeparately` |
| EXDEV, ENOSPC, cancellation | `CrossFilesystem.NestedErrorsRetainCompleteSourceAndExistingDestination`, `SkippedMergeChildrenAndUnsupportedFallbackRetainSource`, `CancellationDuringDirectoryCopyRetainsEntireSourceTree` |
| Unsupported entries | `UnsupportedFifoCopyDoesNotOpenOrBlockAndMoveCanRenameIt`, `SocketAndDeviceCopiesAreRejectedWithoutReading` |
| Trash validation and rollback | `TrashService.ExistingNonPrivateOrSymlinkTrashIsRejectedWithoutRepair`, `MetadataFailureAndCancelledTrashLeaveSourceAndExistingMetadataUntouched`, `FailedMoveRollsBackOnlyItsOwnMetadata`; `CrossFilesystem.FailedTrashLeavesSourceUntouched`, `InvalidSharedUserDirectoryFallsBack`, `MetadataDiskFullLeavesSourceAndNoOrphanInfo` |
| Prompt races and queue | `TaskManager.CancellationBeforePromptDeliveryDiscardsQueuedCallbacks`, `StaleResponseCannotResolveNextPromptOrReusePreviousAnswer`, `TrashConfirmationWaitsBehindConflictAndCompletionCannotClearIt`, `ConflictCancelDropsSeparateQueuedTasksAndConfirmations` |
| Mixed trash batch | `TaskManager.TrashMixedSuccessLeavesFailedSourceUntouchedWithoutAnotherPrompt` |
| Keyboard and focus | `Files.PromptsCaptureKeysAndCtrlCInEveryModeWithoutChangingEditorState`: NORMAL, VISUAL, SEARCH, INSERT, Quick Look; conflict keys, shortcuts, Escape, Ctrl+C, editor text/cursor/selection/focus |

The before-delivery race waits on the worker's atomic prompt publication marker
without dispatching GUI events, cancels, then drains callbacks. Response-ID tests
send a previous answer to the next prompt. Large sparse-file cancellation tests
observe the temporary output before requesting cancellation; they do not claim a
specific byte-offset interruption or a memory/throughput benchmark.

## Commands and outcomes

| Command / run | Result |
| --- | --- |
| `task deps` | Pass; installed HoloNight packages under build/deps/prefix were up to date; no sibling source edits |
| Debug and release builds | Pass; final incremental verification recorded under build/safety-*-final.log |
| Focused regressions | 51 cases passed initially; isolated socket rerun passed after sandbox bind denial. All regressions subsequently passed in the full suite |
| `task test` | Pass: 6/6 CTest targets; 226/228 main cases passed (2 opt-in skips), 16/16 filesystem cases passed; build/safety-tests-final.log |
| `task format-check` | Pass |
| `task tidy` | Pass, 40 translation units; build/safety-tidy-final.log. Four changed units rechecked successfully afterward in build/safety-tidy-last-changes.log |
| `task qml-lint` | Pass |
| `task license-check` | Pass, 119/119 files; outside sandbox after Python multiprocessing socket denial |
| `task install-check` | Pass; staged executable reports version 0.1.0 |
| Native Wayland prompt/focus run | Pass, three rendered tests including the five-context keyboard matrix; build/safety-native.log |
| Prompt visual inspection | Pass for conflict/trash captures at default size/theme; build/safety-review-fileops-{conflict,trash-confirm}.png |

All persistent logs and build artifacts live under build/. The filesystem test
binary redirects TMPDIR under its build fixture directory before creating tmpfs
mountpoints and temporary sources. Its private user/mount namespace provides real
EXDEV and ENOSPC while keeping privilege changes out of the permission-test process.
The socket fixture requires running outside the sandbox; the full suite is run
there rather than skipping that regression.

## Findings during verification

- An obsolete filesystem test still expected successful permanent deletion. It now
  requires a reported trash failure with its source preserved.
- The metadata ENOSPC regression found that calling QFile::remove on a file whose
  flush failed could leave an orphan sidecar. Cleanup now closes and explicitly
  unlinks the exclusively created file; the existing-metadata collision path never
  removes someone else's sidecar.
- Tidy prompted extraction of streaming and endpoint-ancestry helpers and explicit
  event-type/POSIX interface annotations. No safety checks were suppressed.
- Spark delegation was attempted for isolated implementation work, but all attempts
  failed at the service usage limit before any edits. Implementation and review were
  completed locally.

## Remaining acceptance

The full suite skips the existing opt-in `Files.NativeInspectionAcceptance` and
`DirectoryPerformance.RenderedRowsAndInteractionWhileLoading`. The separate native
run above covers this revision's prompt/focus behavior, not those broader checks.

- T-118/119/122: a single rendered VISUAL selection spanning real filesystems with
  mixed trash-validation outcomes is not implemented as one combined E2E test.
  Individual selection, per-partition, mixed-batch and prompt-lifetime tests pass.
  T-122 is reopened rather than retaining the previous false completion claim.
- T-127: sustained throughput and peak memory for a completed multi-GB transfer are
  not benchmarked. Sparse-file cancellation coverage is not a substitute.
- T-128: native automated prompt/focus tests passed, but a full manual visual,
  accessibility and fractional-scaling acceptance matrix remains pending.
- No concurrent hostile filesystem mutation, crash-durability or undo guarantee is
  claimed. Cancellation is cooperative: source cleanup starts only after complete
  transfer and a final cancellation check; already committed work is not rolled back.

## Completion review (2026-09-11)

Stage 4 is implemented but **not fully accepted**. T-118, T-119, T-122, T-127
and T-128 remain open as described above. This commit does not close those gates.

The review corrected the VISUAL trash requirement's accidental `d` prefix and
found two implementation defects, now covered by regressions:

- `FileOperationService.CopyReadOnlyDirectoryPreservesDirectoryMetadata`: new
  directories must stay writable while children copy, then receive source modes
  and timestamps. Metadata errors prevent successful move cleanup (T-136).
- `DirectoryControllerFileOps.YankInterruptsPendingCutChord`: `dyd` must not arm
  a move; every intervening non-`d` key clears the pending cut chord (T-137/T-093).

Both regressions failed against objects built before their corresponding fixes.
Spark was assigned the independent audit and isolated implementation fix, but its
service usage limit prevented any edits; the main agent completed both locally.
Existing default-theme conflict and trash captures were inspected again; this is
not new evidence for fractional scaling or the broader native acceptance matrix.

Fresh review logs are under `build/fileops-review-*.log`. Dependencies, final
Debug/release builds, formatting, QML lint, license and staged installation checks
passed. The license check required running outside the sandbox because Python's
multiprocessing socket was denied. The full test run likewise requires the socket
fixture to run outside the sandbox; filesystem cases use private namespaces.

`task test` passed all 6 CTest targets: 228/230 main cases passed, with the two
existing opt-in skips, and 16/16 filesystem cases passed. See
`build/fileops-review-test-complete.log` and CTest's `LastTest.log` for individual
case results. Both new review regressions passed in this final run.

`task tidy` checked 40 translation units. Its only finding was designated
initializers in the new test's `timespec` array; after that style correction,
`clang-tidy -removed-arg=-mno-direct-extern-access -p=build/test tests/file_operation_service_test.cpp`
passed (`build/fileops-review-tidy-corrected.log`). The other 39 units passed in
`build/fileops-review-tidy.log`. Final formatting and whitespace checks passed.
