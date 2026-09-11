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

- T-128: native automated prompt/focus tests passed, but a full manual visual,
  accessibility and fractional-scaling acceptance matrix remains pending.
- No concurrent hostile filesystem mutation, crash-durability or undo guarantee is
  claimed. Cancellation is cooperative: source cleanup starts only after complete
  transfer and a final cancellation check; already committed work is not rolled back.

## Verification-gap review (2026-09-11)

The approved plan preserves all original requirements and acceptance criteria.
The former shared-process 2 GiB benchmark is superseded: subtracting two lifetime
VmHWM values can hide an allocation below an earlier peak. Its 276 KiB result is
not evidence for bounded copy memory. The replacement uses a fresh headless process,
1 GiB RLIMIT_AS, pre-copy current RSS and post-copy peak RSS; instrumentation errors
fail explicitly. A separate controlled-allocation invocation validates the calculation.

Rendered foreign-filesystem success and sequential valid/all-failure batches remain
useful supplemental coverage. They do not close T-118 (selected sources on distinct
filesystems in one task), T-119 (mixed valid/invalid locations), or T-122 (mixed
validation failures). The new nr_inodes=7 batch exercises one success and one metadata
failure in one VISUAL selection, with signal observation for additional prompts.
Metadata failure is not validation failure; those three gates remain pending.

Spark was assigned exclusive ownership of the benchmark source but hit its service
usage limit before editing. The main agent completed the source locally. No
application behavior, public APIs, sibling sources or installation payloads change.

Fresh results are recorded below. Logs are under build/verification-gaps-*;
fixtures are under build/test/tests/fixtures/.
A skipped filesystem fixture is not acceptance evidence.

## Earlier completion review (2026-09-11; counts predate this revision)

Stage 4 is implemented but **not fully accepted**. T-118, T-119 and T-122 remain
pending against their literal criteria; T-128 remains partial. T-127 is closed by
the isolated benchmark and existing latency/queue tests recorded in this revision.

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

## Verification-gap revision results (2026-09-11)

- `task deps`: passed; installed providers refreshed under build/deps without changing
  sibling sources. `task build PRESET=test` and `task build` passed. Logs:
  `build/verification-gaps-deps.log`, `build/verification-gaps-build-focused.log`,
  `build/verification-gaps-build.log`.
- `build/test/tests/files-memory-benchmark --validate-rss-measurement`: passed in a
  separate process. After touching and releasing 320 MiB, baseline current RSS was
  35292 KiB; touching 256 MiB raised current RSS to 297444 KiB. Lifetime peak stayed
  362704 KiB, so old peak subtraction reported 0 KiB, while peak minus current
  baseline reported 327412 KiB, exceeding 200 MiB as required. This conservative
  calculation can include an earlier peak; the real benchmark runs in a fresh
  process without these allocations. `build/verification-gaps-rss-control.log`.
  Final-source rerun also passed: old delta 0 KiB, corrected growth 327404 KiB;
  `build/verification-gaps-rss-control-final.log`.
- `ctest --test-dir build/test -R '^files-memory-benchmark$' -V`: passed with a real
  2 GiB destination and enforced/read-back 1 GiB RLIMIT_AS. Throughput 2091.78 MiB/s;
  pre-copy current RSS 35648 KiB, post-copy peak RSS 36740 KiB, growth 1092 KiB
  (<204800 KiB). `build/verification-gaps-memory.log`. Throughput is host/cache
  dependent; this is the existing default local-filesystem threshold, not a cold-disk
  or durable-write benchmark.
- `ctest --test-dir build/test -R '^files-fsops-window-smoke$' -V`: 3/3 rendered cases
  passed without skips. The mixed batch asserts all seven fixture inodes, one
  confirmation, two processed items, `1/2 succeeded`, successful contents/metadata
  in partition trash, retained failed contents, and no prompt signal after confirming
  through completion plus 50 ms event-loop drain.
  `build/verification-gaps-window.log`.
- Initial sandboxed `task test`: 7/8 CTest targets passed; the existing
  `FileOperationService.SocketAndDeviceCopiesAreRejectedWithoutReading` failed due
  to sandbox socket restrictions. `build/verification-gaps-test.log`.

- `task test` outside the sandbox: passed 8/8 CTest targets in 30.47 seconds, including
  the final metadata-error assertion. `build/verification-gaps-test-unsandboxed.log`.
  Main suite: 229/231 passed with the two existing opt-in skips
  (`Files.NativeInspectionAcceptance`,
  `DirectoryPerformance.RenderedRowsAndInteractionWhileLoading`); filesystem 16/16,
  rendered filesystem 3/3, isolated memory 1/1 passed. No filesystem skips.

- Initial `task check` outside the sandbox passed builds, all eight CTest targets
  and formatting, then found two clang-tidy style issues in the new benchmark
  (identifier length and static method access). Both corrected without suppressions.
  `build/verification-gaps-check.log`.

- Final `task check` outside the sandbox: exit 0. Debug/release builds, 8/8 CTest
  targets, formatting, clang-tidy (42 translation units), QML lint, REUSE (129/129),
  staged install and uninstall checks passed. Final memory result: 2152.06 MiB/s,
  baseline 35680 KiB, peak 36776 KiB, growth 1096 KiB, RLIMIT_AS 1024 MiB.
  `build/verification-gaps-check-final.log`; CTest detail in
  `build/test/Testing/Temporary/LastTest.log`. The same two opt-in skips remain;
  filesystem and rendered filesystem cases all ran. `git diff --check` passed.
  No isolated-runtime rerun was required: installation rules/payloads are unchanged.

T-118/T-119/T-122 remain pending; mixed metadata failure does not close their
literal gates. T-128's broader manual/scaling acceptance remains partial.
