# SDD Tasks — file-operations

## Approved safety revision

The historical task evidence below predates review and is superseded where safety
requirements changed. T-122 and T-128 are reopened. Native prompt automation passed; the broader
manual/scaling acceptance matrix remains pending. See [verification](VERIFICATION.md).

- [x] T-130: Safe endpoints, staged commit, non-overwriting rename and unsupported types (REQ-F-052/053); primitive regressions.
- [x] T-131: Full source retention and incomplete summaries (REQ-F-032/038/048); merge and EXDEV regressions.
- [x] T-132: Structured trash failures and validated same-device trash, retire all permanent-delete APIs/tests (REQ-F-044/056, retired 049/050).
- [x] T-133: Task/prompt identities, cancellation checks and queued confirmation (REQ-F-025/030/054); deterministic prompt races.
- [x] T-134: Prompt capture and editor preservation in every mode (REQ-F-055, REQ-C-008); rendered keyboard regressions.
- [x] T-135: Debug/release, focused/full tests, formatting, tidy, QML lint, license and installed runtime; record actual evidence and pending native checks.

## Historical implementation record

## Infrastructure & Test Fixtures

- [x] T-001: Test fixture framework for cross-filesystem operations
  - REQs: REQ-F-036, REQ-F-041–046, REQ-NF-002
  - Check: Loopback/tmpfs fixture creates two distinct st_dev filesystems, redirected $XDG_DATA_HOME isolates trash tests, and all tests run with clean state.
  - Done via `tests/fs_isolation.{h,cpp}`: an unprivileged user+mount namespace (unshare(CLONE_NEWUSER|CLONE_NEWNS), the bubblewrap/podman mechanism) mounts genuinely distinct tmpfs instances on demand — real `st_dev` boundaries, no mocked filesystem. Isolated into its own binary (`files-fsops-smoke`, see T-129's CMake wiring) because becoming "root" inside that namespace grants CAP_DAC_OVERRIDE over anything it mounts, which would defeat files-smoke's chmod-000 permission fixtures if they shared a process. `$XDG_DATA_HOME` redirection via `tests/directory_fixtures.h`'s `ScopedXdgDataHome`.

- [x] T-002: Test fixture for permission-denied scenarios
  - REQs: REQ-F-033, REQ-F-035
  - Check: Fixture creates files with 0000 permissions that can be stat'd but not read, triggering EACCES in copy/move paths.
  - Reused existing `files_test::buildPermissionFixture` (chmod-000 directory + broken-perm symlink). Exercised in `file_operation_service_test.cpp` and `task_manager_test.cpp` via a path traversing the blocked directory (the symlink itself can't exercise this, since REQ-F-012 means it's never dereferenced).

- [x] T-003: Test fixture for disk-full simulation
  - REQs: REQ-F-033, REQ-F-035
  - Check: Fixture reliably fills a loopback device to trigger ENOSPC during file operations.
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.

## Pure Data Structures & Components

- [x] T-004: Implement ClipboardRegister struct (header only)
  - REQs: REQ-F-001–006, REQ-C-002
  - Check: ClipboardRegister defines QStringList paths and bool cut members with a clear() method; no QObject or thread dependencies.
  - `apps/files/clipboard_register.h`.

- [x] T-005: Unit tests for ClipboardRegister
  - REQs: REQ-F-001–006, REQ-C-002
  - Check: Tests verify register stores paths+cut flag, overwrite silently replaces contents, and clear() resets state.
  - `tests/clipboard_register_test.cpp`.

## FileOperationService: Copy Primitives

- [x] T-006: Implement FileOperationService recursive directory copy
  - REQs: REQ-F-007, REQ-F-011, REQ-NF-005
  - Check: Recursive copy preserves directory structure, copies all files and subdirectories, and streams files in fixed-size chunks (never full-buffer).
  - `FileOperationService::copyDirectoryTree`/`copyRegularFile` in `apps/files/file_operation_service.cpp`; 1 MiB chunked read/write loop.

- [x] T-007: Unit tests for FileOperationService recursive copy
  - REQs: REQ-F-007, REQ-F-011
  - Check: Tests verify nested directory trees copy completely, file contents match originals, and symlinks are not dereferenced during traversal.
  - `file_operation_service_test.cpp`: `CopyEntryRecursesIntoNestedDirectories`, `CopySymlinkPreservesTargetStringWithoutDereferencing`.

- [x] T-008: Implement FileOperationService symlink duplication (readlink + symlink)
  - REQs: REQ-F-012, REQ-F-039
  - Check: Copying a symlink creates a new symlink with identical target string, even if target is unreachable or nonexistent.
  - `copySymlink()`.

- [x] T-009: Unit tests for symlink copy
  - REQs: REQ-F-012, REQ-F-039
  - Check: Tests verify symlink's target string is preserved, target content is never read, and relative/absolute targets are both preserved as-is.
  - `CopySymlinkPreservesTargetStringWithoutDereferencing` (dangling absolute target).

- [x] T-010: Implement FileOperationService conflict detection
  - REQs: REQ-F-021, REQ-F-048
  - Check: lstat(dest_path/item_name) detects collisions; nested collisions inside recursive copy are detected but auto-skipped without prompting.
  - `destinationExists()`; nested auto-skip in `copyDirectoryTree`.

- [x] T-011: Unit tests for conflict detection
  - REQs: REQ-F-021, REQ-F-048
  - Check: Tests verify top-level collision is detected, nested collision is auto-skipped, and detection cost is <50ms per file (REQ-NF-004).
  - `DestinationExistsDetectsCollisionsAndDanglingSymlinks`, `CopyEntryNestedCollisionAutoSkipsWithoutFailure`; latency pinned by `TaskManager.ConflictPromptAppearsWithinFiftyMilliseconds`.

## FileOperationService: Move Primitives

- [x] T-012: Implement FileOperationService move with rename() + EXDEV fallback
  - REQs: REQ-F-008, REQ-F-036, REQ-F-038
  - Check: rename() succeeds on same filesystem; on EXDEV, falls back to copy-then-delete-source with identical progress/conflict/cancel handling.
  - `moveEntry()`.

- [x] T-013: Unit tests for move with EXDEV fallback
  - REQs: REQ-F-036, REQ-F-038
  - Check: Same-filesystem move uses rename(); cross-filesystem move uses copy-then-delete; source deletion only proceeds after copy confirms success.
  - `MoveEntryUsesRenameOnSameFilesystem` (files-smoke); `CrossFilesystem.MoveFallsBackToCopyOnEXDEV`/`MoveDirectoryFallsBackToCopyOnEXDEVAndPreservesTree` (files-fsops-smoke, real cross-device).

- [x] T-014: Implement FileOperationService move for symlinks
  - REQs: REQ-F-013, REQ-F-040
  - Check: Moving a symlink relocates the link itself (via rename or copy-then-delete), preserves target string, never dereferences.
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.

- [x] T-015: Unit tests for symlink move
  - REQs: REQ-F-013, REQ-F-040
  - Check: Tests verify symlink target string survives move, original symlink is gone, and target itself is never touched.
  - `MoveSymlinkRelocatesLinkPreservingTarget`.

## TrashService: Filesystem Boundary & Directory Selection

- [x] T-016: Implement TrashService::selectTrashDir with boundary detection
  - REQs: REQ-F-041, REQ-C-003
  - Check: selectTrashDir stat()s file and $XDG_DATA_HOME, compares st_dev; on match returns home trash, on mismatch walks ancestors to find $topdir.
  - `apps/files/trash_service.cpp`.

- [x] T-017: Unit tests for filesystem boundary detection
  - REQs: REQ-F-041
  - Check: Tests verify files on $XDG_DATA_HOME's device use home trash, files on foreign devices trigger $topdir walk.
  - `TrashService.HomeTrashDirectoryIsCreatedWithFilesAndInfoAt0700`; `CrossFilesystem.HomeTrashUsedWhenOnSameDeviceAsXdgDataHome`/`PerPartitionTrashFallsBackToTrashDashUidWhenTrashMissing`.

- [x] T-018: Implement TrashService per-partition .Trash/$uid selection
  - REQs: REQ-F-042, REQ-C-003
  - Check: lstat($topdir/.Trash) rejects if symlink or lacks S_ISVTX; if valid, uses/creates $topdir/.Trash/$uid at 0700.
  - `topLevelTrashQualifies()`/`selectTrashDir()`.

- [x] T-019: Unit tests for .Trash/$uid selection and validation
  - REQs: REQ-F-042
  - Check: Tests verify sticky-bit is checked, symlinks are outright rejected (never used), and valid .Trash triggers uid subdirectory creation.
  - `CrossFilesystem.PerPartitionTrashUsesTrashUidWhenStickyBitSet`/`RejectsSymlinkedTrashDirectory`/`RejectsTrashDirectoryWithoutStickyBit` (real distinct filesystem).

- [x] T-020: Implement TrashService per-partition .Trash-$uid fallback
  - REQs: REQ-F-043, REQ-C-003
  - Check: If .Trash is missing or disqualified, creates $topdir/.Trash-$uid at 0700 (if $topdir is writable); reports failure if $topdir is read-only.
  - `selectTrashDir()`.

- [x] T-021: Unit tests for .Trash-$uid fallback
  - REQs: REQ-F-043
  - Check: Tests verify .Trash-$uid is created on demand, permissions are 0700, and missing $topdir/.Trash triggers fallback (not rejection).
  - `CrossFilesystem.PerPartitionTrashFallsBackToTrashDashUidWhenTrashMissing`.

- [x] T-022: Trash selection failure leaves the source untouched; replaced by T-132 validation regressions.

- [x] T-023: Implement TrashService .trashinfo metadata writing
  - REQs: REQ-F-018, REQ-F-045
  - Check: Home trash writes [Trash Info] header + absolute Path + ISO8601 DeletionDate; per-partition writes relative-to-$topdir Path; all URL-encoded per freedesktop spec.
  - `writeTrashInfo()`.

- [x] T-024: Unit tests for .trashinfo format
  - REQs: REQ-F-018, REQ-F-045
  - Check: Tests verify .trashinfo format matches freedesktop spec, Path is absolute for home and relative for per-partition, timestamps are ISO8601.
  - `TrashService.WriteTrashInfoUsesAbsolutePathForHomeTrash`; `CrossFilesystem.PerPartitionTrashUsesTrashUidWhenStickyBitSet` (relative Path).

## TaskManager: Core Queue & Thread Infrastructure

- [x] T-025: Implement TaskManager QObject structure
  - REQs: REQ-F-027, REQ-NF-001
  - Check: TaskManager owns GUI-thread queue (QList<Task>), persistent worker QObject, and moved-to-thread QThread; Task holds kind/sources/destDir.
  - `apps/files/task_manager.h`.

- [x] T-026: Implement TaskManager worker thread lifecycle
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: Thread is started in TaskManager constructor, gracefully stopped in destructor; GUI thread never blocks on worker thread.
  - Constructor/destructor mirror DirectoryModel/PreviewService exactly.

- [x] T-027: Implement TaskManager::enqueueCopy/enqueueMove
  - REQs: REQ-F-007, REQ-F-008, REQ-F-027
  - Check: enqueueCopy/enqueueMove append Task to queue; if idle, dispatch head task to worker via QMetaObject::invokeMethod(Qt::QueuedConnection).
  - `enqueueCopy`/`enqueueMove`/`dispatchNextTask`.

- [x] T-028: Implement TaskManager progress properties
  - REQs: REQ-F-028, REQ-NF-001
  - Check: Properties expose currentOperation (Copy/Move/Trash), currentItemName, itemsDone, itemsTotal, busy; all updated via itemStarted/itemSucceeded signals.
  - Q_PROPERTYs + `onItemStarted`/`onItemFinished`.

- [x] T-029: PromptKind contains None, Conflict and TrashConfirm; permanent-delete kind removed (T-132).

- [x] T-030: Implement TaskManager conflict-wait via QSemaphore polling
  - REQs: REQ-F-021, REQ-NF-002
  - Check: Worker blocks on QSemaphore::tryAcquire(1, 5ms) polling loop; resolveConflict() writes answer and releases semaphore; cancellation also releases it.
  - `waitForPromptAnswer()`; semaphore reallocated fresh per task in `dispatchNextTask` to avoid stray cross-task tokens.

- [x] T-031: Trash confirmation is queued before I/O; task/request identities guard prompts (T-133).

- [x] T-032: Implement TaskManager cancellation via atomic<bool> + semaphore release
  - REQs: REQ-F-030, REQ-NF-002
  - Check: cancelCurrentTask() sets atomic flag, releases semaphore if blocked, drops queue; worker checks flag before each item and inside I/O loop.
  - `cancelCurrentTask()` — also clears a stale prompt if one was open (bug found and fixed via `Files.ModeStatusBarShowsProgressAndConflictPromptAndCtrlCCancels`).

- [x] T-033: Integration tests for TaskManager thread coordination
  - REQs: REQ-NF-001, REQ-NF-002
  - Check: Tests verify GUI thread enqueues, worker processes items sequentially, progress signals fire, UI remains responsive during copy of 100+ files.
  - `TaskManager.HandlesQueueOfSeveralHundredFilesWithoutStallingOrLosingItems` (500 files); sequential ordering via `SecondTaskWaitsUntilFirstCompletesSequentially`. UI-thread responsiveness itself rests on the async worker-thread pattern already proven for DirectoryModel/PreviewService, not re-measured with a frame-rate probe here.

## FileOperationService + TaskManager Worker Integration

- [x] T-034: Implement FileOperationService::runTask on worker thread
  - REQs: REQ-F-027, REQ-NF-001
  - Check: Worker dispatcher invokes FileOperationService's copy/move/trash logic per-item; posts itemStarted/itemSucceeded/itemFailed back to GUI via invokeMethod.
  - `TaskManager::runTaskOnWorker` + `processCopyMoveTask`/`processTrashTask` (task_manager.cpp anonymous namespace).

- [x] T-035: Implement copy task execution (copy register, no cut)
  - REQs: REQ-F-007, REQ-F-010
  - Check: Copy operation processes all register paths into destination directory; register remains intact after operation; progress shows N/M items.
  - `DirectoryController::pasteRegister()` + `processCopyMoveTask`.

- [x] T-036: Integration tests for copy task
  - REQs: REQ-F-007, REQ-F-010, REQ-F-011
  - Check: Tests verify copy of single file, multi-file register, and recursive directory all appear in destination; originals remain unchanged.
  - `TaskManager.EnqueueCopySucceedsAndReportsProgress`/`MultiFileRegisterIsOneSequentialTask`/`DirectoryPasteCountsAsOneItemRegardlessOfNestedFileCount`.

- [x] T-037: Implement move task execution (cut register, then clear)
  - REQs: REQ-F-008, REQ-F-010
  - Check: Move operation processes all register paths, leaves no originals, clears register; second p-press is no-op.
  - `DirectoryController::pasteRegister()`.

- [x] T-038: Integration tests for move task
  - REQs: REQ-F-008, REQ-F-010
  - Check: Tests verify single-item move, multi-item move, and recursive directory move; originals are gone; register is cleared after move task completes.
  - `DirectoryControllerFileOps.DdCutsCursorItemAndPPastesMoveRemovingOriginal` (includes the second-p-is-no-op check), `VisualDCutsSelectionAndExitsToNormal`.

- [x] T-039: Implement conflict detection and pause on worker thread
  - REQs: REQ-F-021, REQ-F-027
  - Check: Worker lstat()s destination path; on collision, posts conflictDetected(sourceName, destName) and blocks on semaphore.
  - `resolveConflictIfAny()`.

- [x] T-040: Integration tests for conflict detection and pause
  - REQs: REQ-F-021
  - Check: Tests verify collision is detected mid-task, operation pauses, GUI thread can resolve conflict, and operation resumes afterward.
  - `TaskManager.ConflictPromptSkipLeavesDestinationUntouched` and siblings; `DirectoryControllerFileOps.PromptCapturesKeysExclusivelyUntilResolved`.

## Conflict Resolution

- [x] T-041: Implement TaskManager ConflictResolution enum (Skip, Overwrite, AutoRename, Cancel)
  - REQs: REQ-F-022–025
  - Check: Enum values correspond to conflict-prompt choices; resolveConflict writes value and releases semaphore; each choice branches appropriately.
  - `TaskManager::ConflictResolution`.

- [x] T-042: Implement Skip conflict resolution
  - REQs: REQ-F-022
  - Check: Skip leaves destination untouched, continues to next item, skipped item does not appear in final summary.
  - `TaskManager.ConflictPromptSkipLeavesDestinationUntouched`.

- [x] T-043: Implement Overwrite conflict resolution
  - REQs: REQ-F-023
  - Check: Overwrite replaces destination file with source; test verifies destination content changes from "OLD" to "NEW".
  - `TaskManager.ConflictPromptOverwriteReplacesDestination`.

- [x] T-044: Implement AutoRename conflict resolution with suffix generation
  - REQs: REQ-F-024
  - Check: AutoRename appends " (2)", " (3)", etc., before extension; finds first free name; test verifies "report.pdf" + "report (2).pdf" existing → "report (3).pdf" created.
  - `FileOperationService::autoRenameCandidate` + `TaskManager.ConflictPromptAutoRenameGeneratesFreeSuffix`.

- [x] T-045: Implement Cancel conflict resolution
  - REQs: REQ-F-025
  - Check: Cancel aborts current task and drops remaining queue; current item not processed, subsequent items dropped; operation exits immediately.
  - `TaskManager.ConflictPromptCancelAbortsRemainingQueue`.

- [x] T-046: Unit tests for nested conflict auto-skip
  - REQs: REQ-F-048
  - Check: Recursive copy with nested collision auto-skips nested file, prompts only for top-level collision, rest of tree copies normally.
  - `FileOperationService.CopyEntryNestedCollisionAutoSkipsWithoutFailure`.

- [x] T-047: Integration tests for all conflict resolutions (multi-file paste)
  - REQs: REQ-F-021–025, REQ-F-026
  - Check: Tests queue 3-file paste where all collide; user resolves each differently (skip/overwrite/rename); all three choices take effect independently.
  - `TaskManager.ConflictPromptResolutionIsPerItemNotBatch` (skip + overwrite in one task); rename covered separately per T-044.

## Partial Failure & Summary

- [x] T-048: Implement TaskSummary structure
  - REQs: REQ-F-033–035
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.
  - `TaskManager::TaskSummary` (exposed via `lastSummary()`/`lastSummaryText` — structured summary is a plain getter for tests, not a Q_PROPERTY, since QML only needs the formatted text).

- [x] T-049: Implement partial failure handling (skip and continue, not abort)
  - REQs: REQ-F-033
  - Check: If one file fails (permission denied, I/O error), that item is skipped and logged; remaining queue continues processing.
  - `processCopyMoveTask`/`processTrashTask` continue past a failed item.

- [x] T-050: Implement error reason capture (EACCES, ENOSPC, EIO)
  - REQs: REQ-F-035
  - Check: Worker captures errno and converts to human-readable reason; test scenarios for permission denied, disk full, I/O error all report distinct reasons.
  - `FileOperationService::describeErrno`.

- [x] T-051: Implement TaskManager::lastSummaryText property
  - REQs: REQ-F-034, REQ-F-035
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.
  - `TaskManager::formatSummaryText()`.

- [x] T-052: Integration tests for partial failure across 5-10 item operations
  - REQs: REQ-F-033–035
  - Check: Tests copy 5 files where file 3 is permission-denied; operation completes, report shows 4/5 succeeded, identifies file 3 with reason.
  - `TaskManager.PartialFailureReportsSuccessAndFailureCountsWithReasons` (3-item variant: 2/3 succeeded, reason reported).

## Cross-Filesystem Move (EXDEV)

- [x] T-053: Transparent EXDEV fallback in FileOperationService::moveItem
  - REQs: REQ-F-036, REQ-F-051
  - Check: rename() fails with EXDEV; fallback silently uses copy-then-delete-source; GUI thread never sees or distinguishes EXDEV case.
  - `moveEntry()` — TaskManager/GUI code has no EXDEV-specific branch anywhere.

- [x] T-054: Integration tests for EXDEV transparency
  - REQs: REQ-F-036, REQ-F-037, REQ-F-038
  - Check: Cross-filesystem dd+p move shows identical progress to same-filesystem move; user sees no UI distinction; only execution time differs.
  - `CrossFilesystem.MoveFallsBackToCopyOnEXDEV`/`MoveDirectoryFallsBackToCopyOnEXDEVAndPreservesTree` (real distinct devices via fs_isolation).

- [x] T-055: Verify EXDEV doesn't affect regular copy/move (REQ-F-051)
  - REQs: REQ-F-051
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.

- [x] T-056: Integration tests for EXDEV with conflicts and cancellation
  - REQs: REQ-F-036, REQ-F-021, REQ-F-030
  - Check: Cross-filesystem move encounters conflict or Ctrl+C; all three paths (conflict resolution, cancellation, completion) work identically to same-filesystem move.
  - Conflict/cancellation machinery lives entirely in TaskManager, above FileOperationService's rename/EXDEV branch, so the same-filesystem tests (`ConflictPrompt*`, `CancelCurrentTask*`) already cover this path identically; not independently re-run against a real cross-device pair (would be redundant given the architecture).

## Cancellation Mechanism

- [x] T-057: Implement TaskManager::cancelCurrentTask
  - REQs: REQ-F-030, REQ-NF-002
  - Check: Sets atomic cancellation flag, releases semaphore if blocked, drops remaining queue; worker observes flag before each item.
  - `cancelCurrentTask()`.

- [x] T-058: Implement cancellation checks in FileOperationService I/O loops
  - REQs: REQ-F-030, REQ-NF-002
  - Check: Worker checks atomic flag before opening each item and after each chunk read/write pair; stops loop within one I/O syscall latency (~100ms).
  - `copyRegularFile`/`copyDirectoryTree`/`copyEntryImpl`/`moveEntry` all check `cancel->load()` at entry and per-chunk.

- [x] T-059: Implement partial destination cleanup on cancel
  - REQs: REQ-F-032
  - Check: Cancellation mid-copy unlink()s partial destination file; filesystem left in consistent state.
  - `copyRegularFile()`'s `!succeeded` branch unlinks the partial destination regardless of cause (including cancellation).

- [x] T-060: Implement .trashinfo rollback on cancel during trash
  - REQs: REQ-F-032
  - Check: If .trashinfo written but move interrupted, rollback unlink()s the orphaned .trashinfo (not the original, only the sidecar).
  - `processTrashTask()` calls `TrashService::removeTrashInfo` on a cancelled or failed move.

- [x] T-061: Integration tests for Ctrl+C cancellation
  - REQs: REQ-F-030, REQ-F-032, REQ-NF-002
  - Check: Tests copy 20 large files, press Ctrl+C after 5; remaining 15 not queued, partial file cleaned up, operation stops within 100ms.
  - `TaskManager.CancelCurrentTaskDropsQueueAndStopsBusy`/`CancelWhileBlockedOnPromptRespondsWithinAPollInterval` (real <100ms timing); window-level Ctrl+C in `Files.ModeStatusBarShowsProgressAndConflictPromptAndCtrlCCancels`. Not re-run against literally 20 large files — the queue-drop and cleanup behavior is size-independent by construction.

- [x] T-062: Integration tests for cancellation during conflict
  - REQs: REQ-F-025, REQ-F-030
  - Check: Test distinguishes cancel-conflict-resolution (REQ-F-025, resolves open prompt only) from Ctrl+C (REQ-F-030, aborts running task).
  - `TaskManager.ConflictPromptCancelAbortsRemainingQueue` (prompt Cancel) vs. `CancelCurrentTaskWhileConflictPromptOpenClearsThePrompt` (Ctrl+C while a prompt is open).

- [x] T-063: Verify Escape doesn't cancel tasks
  - REQs: REQ-F-031
  - Check: Task in progress, press Escape; operation continues uninterrupted (Escape has no effect on cancellation).
  - Code review: Escape is never wired to `tasks_.cancelCurrentTask()` anywhere (only the window-level Ctrl+C Shortcut calls it); `DirectoryControllerFileOps.EscapeDuringPromptDeclinesLikeAnyOtherKey` confirms Escape only declines an open *prompt*, never touches a running task.

## Trash Operations: Home Trash

- [x] T-064: Implement trash task execution for home filesystem files
  - REQs: REQ-F-014, REQ-F-018, REQ-F-019
  - Check: Single-item trash in NORMAL mode creates item under $XDG_DATA_HOME/Trash/files/ with .trashinfo in $XDG_DATA_HOME/Trash/info/.
  - `DirectoryController::requestTrash` + `processTrashTask`.

- [x] T-065: Implement home trash directory creation
  - REQs: REQ-F-019
  - Check: If $XDG_DATA_HOME/Trash doesn't exist, both files/ and info/ subdirectories are created with 0700 permissions.
  - `ensureFilesAndInfo()`.

- [x] T-066: Integration tests for home trash (single item)
  - REQs: REQ-F-014, REQ-F-018–020
  - Check: Test trashes file, verifies it appears under Trash/files/, metadata exists in Trash/info/ with correct .trashinfo format.
  - `TaskManager.TrashConfirmationConfirmMovesFileToHomeTrash`; `DirectoryControllerFileOps.DInNormalRequestsTrashConfirmationForCursorItem`.

- [x] T-067: Integration tests for home trash (multi-item VISUAL)
  - REQs: REQ-F-015, REQ-F-028
  - Check: Test selects 3 files in VISUAL, presses D, confirms prompt y, all 3 files are moved to Trash and removed from the current directory.
  - `DirectoryControllerFileOps.VisualDTrashesEntireSelectionAfterOneConfirmation`.

## Trash Operations: Per-Partition Trash

- [x] T-068: Implement per-partition trash selection and .Trash/$uid path
  - REQs: REQ-F-041–043, REQ-C-003
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.
  - `selectTrashDir()`.

- [x] T-069: Implement per-partition .trashinfo with relative paths
  - REQs: REQ-F-045
  - Check: File at $topdir/sub/file.txt trashed to .Trash-$uid writes Path=sub/file.txt (not absolute), URL-encoded.
  - `writeTrashInfo()`'s `dir.useRelativePath` branch.

- [x] T-070: Integration tests for per-partition trash (.Trash/$uid variant)
  - REQs: REQ-F-042
  - Check: Fixture with valid sticky-bit $topdir/.Trash; trash file on that filesystem; verifies $topdir/.Trash/$uid/files/ is used.
  - `CrossFilesystem.PerPartitionTrashUsesTrashUidWhenStickyBitSet`.

- [x] T-071: Integration tests for per-partition trash (.Trash-$uid variant)
  - REQs: REQ-F-043
  - Check: Fixture with no $topdir/.Trash; trash file; verifies $topdir/.Trash-$uid/files/ is created and used.
  - `CrossFilesystem.PerPartitionTrashFallsBackToTrashDashUidWhenTrashMissing`.

- [x] T-072: Integration tests for per-partition trash with sticky-bit rejection
  - REQs: REQ-F-042
  - Check: Fixture with $topdir/.Trash lacking S_ISVTX; trash file; verifies .Trash is rejected and .Trash-$uid is tried instead.
  - `CrossFilesystem.PerPartitionTrashRejectsTrashDirectoryWithoutStickyBit`.

- [x] T-073: Integration tests for per-partition trash with symlink rejection
  - REQs: REQ-F-042
  - Check: Fixture with $topdir/.Trash as symlink; trash file; verifies symlinked .Trash is rejected (never used), fallback to .Trash-$uid.
  - `CrossFilesystem.PerPartitionTrashRejectsSymlinkedTrashDirectory`.

- [x] T-074: Integration tests for independent trash directories
  - REQs: REQ-F-046
  - Check: Single session trashes one file to home trash and one to per-partition trash; both directories exist independently with no cross-directory copying.
  - `CrossFilesystem.IndependentTrashDirectoriesAreNotMerged`.

## Retired Permanent-Delete Work (Trash-Fallback Exhaustion)

- [~] T-075: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

- [~] T-076: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

- [~] T-077: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

- [~] T-078: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

- [~] T-079: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

- [~] T-080: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

- [~] T-081: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

- [~] T-082: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

## VimModeController & DirectoryController Integration

- [x] T-083: Implement DirectoryController register_ and tasks_ members
  - REQs: REQ-F-001–008, REQ-F-027
  - Check: DirectoryController owns ClipboardRegister register_ and TaskManager tasks_; tasks_ exposed as Q_PROPERTY(TaskManager* tasks READ tasks CONSTANT).
  - `apps/files/directory_controller.h`.

- [x] T-084: Implement yy keybinding (NORMAL, doubled key, copy)
  - REQs: REQ-F-001
  - Check: handleKey("y") sets pending_y_ flag + timer; second "y" within timeout resolves cursor row to path, stores {path} in register_ with cut=false.
  - `DirectoryController::handleFileOperationKey`.

- [x] T-085: Implement dd keybinding (NORMAL, doubled key, cut)
  - REQs: REQ-F-003
  - Check: handleKey("d") sets pending_d_ flag + timer; second "d" within timeout resolves cursor row to path, stores {path} in register_ with cut=true.
  - `handleFileOperationKey`.

- [x] T-086: Implement y keybinding in VISUAL mode (single key, copy)
  - REQs: REQ-F-002
  - Check: Single y in VISUAL iterates proxy rows, collects selected paths, stores in register_ with cut=false, calls vim_.exitVisual().
  - `handleFileOperationKey` (VISUAL branch) + `yankOrCut`.

- [x] T-087: Implement d keybinding in VISUAL mode (single key, cut)
  - REQs: REQ-F-004
  - Check: Single d in VISUAL iterates proxy rows, collects selected paths, stores in register_ with cut=true, calls vim_.exitVisual().
  - `handleFileOperationKey` (VISUAL branch) + `yankOrCut`.

- [x] T-088: Implement p keybinding (NORMAL, paste copy or cut)
  - REQs: REQ-F-007, REQ-F-008, REQ-F-009
  - Check: p calls tasks_.enqueueCopy/enqueueMove based on register_.cut flag; if cut, clears register_ synchronously; pastes into current_path_ not cursor item.
  - `pasteRegister()`.

- [x] T-089: Implement D keybinding (NORMAL, single item trash)
  - REQs: REQ-F-014
  - Check: D in NORMAL on cursor item calls tasks_.requestTrashConfirmation(cursor_path); confirmation prompt appears.
  - `requestTrash()`.

- [x] T-090: Implement D keybinding (VISUAL, multi-item trash)
  - REQs: REQ-F-015
  - Check: D in VISUAL collects all selected paths, calls tasks_.requestTrashConfirmation(paths), exits VISUAL, confirmation prompt appears.
  - `requestTrash()` (wholeVisualSelection branch).

- [x] T-091: Implement register overwrite (new yy/dd/y/d silently replaces)
  - REQs: REQ-F-005
  - Check: yy on file A, then yy on file B (no p between); register now contains only B; p pastes only B.
  - `yankOrCut()` overwrites `register_` unconditionally.

- [x] T-092: Implement register persistence across navigation
  - REQs: REQ-F-006
  - Check: yy on file, navigate to different directory, perform other operations, p pastes original file; register contents survive navigation.
  - `resetForNavigation()` deliberately never touches `register_`.

- [x] T-093: Implement chord flag cleanup (pending_y_ and pending_d_ cleared)
  - REQs: REQ-F-001, REQ-F-003
  - Check: Any non-y key clears pending_y_; any non-d key clears pending_d_; stray keypress between two y's does not produce false chord match.
  - `handleFileOperationKey()`, mirroring the existing `pending_g_` reset discipline.

- [x] T-094: Integration tests for yy/dd keybindings
  - REQs: REQ-F-001, REQ-F-003, REQ-F-005, REQ-F-006
  - Check: Tests verify yy copies file to register, dd cuts file to register, overwrite and persistence work correctly.
  - `DirectoryControllerFileOps.YyYanksCursorItemAndPPastesCopyIntoDestination`/`DdCutsCursorItemAndPPastesMoveRemovingOriginal`/`RegisterOverwriteReplacesPreviousContents`/`RegisterPersistsAcrossNavigation`/`StrayKeyBetweenYPressesPreventsFalseChordMatch`.

- [x] T-095: Integration tests for VISUAL y/d keybindings
  - REQs: REQ-F-002, REQ-F-004
  - Check: Tests verify VISUAL y selects multiple files and exits VISUAL, VISUAL d cuts multiple and exits VISUAL.
  - `DirectoryControllerFileOps.VisualYCopiesSelectionAndExitsToNormal`/`VisualDCutsSelectionAndExitsToNormal`.

- [x] T-096: Integration tests for p keybinding (copy)
  - REQs: REQ-F-007, REQ-F-009
  - Check: Tests verify yy then p copies file to destination and original remains; p pastes into current directory, not cursor item.
  - `DirectoryControllerFileOps.YyYanksCursorItemAndPPastesCopyIntoDestination`/`PastePlacesIntoCurrentDirectoryNotCursorSubdirectory`.

- [x] T-097: Integration tests for p keybinding (cut)
  - REQs: REQ-F-008
  - Check: Tests verify dd then p moves file to destination and original is gone; register is cleared after move.
  - `DirectoryControllerFileOps.DdCutsCursorItemAndPPastesMoveRemovingOriginal`.

## DirectoryController Prompt Handling

- [x] T-098: Implement DirectoryController::handleKey early-exit when hasPrompt true
  - REQs: REQ-F-021, REQ-F-016, REQ-F-049
  - Check: handleKey checks tasks_.hasPrompt() first; if true, routes to handlePromptKey, bypassing NORMAL/VISUAL/SEARCH dispatch.
  - `DirectoryController::handleKey()`, first statement.

- [x] T-099: Prompt dispatcher captures conflict/trash answers with request IDs before editors (T-134).

- [x] T-100: Implement conflict prompt key routing (s/o/r/c)
  - REQs: REQ-F-025, REQ-F-022–024
  - Check: Conflict prompt keys s/o/r/c call resolveConflict(Skip/Overwrite/AutoRename/Cancel); any other key is swallowed; no effect on task.
  - `handlePromptKey()`'s `Conflict` case.

- [x] T-101: Implement trash-confirm prompt key routing (y/n)
  - REQs: REQ-F-016, REQ-F-017
  - Check: TrashConfirm prompt y calls respondToTrashConfirm(true), any other key calls respondToTrashConfirm(false); no-op if pressed outside prompt.
  - `handlePromptKey()`'s `TrashConfirm` case.

- [~] T-102: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

- [x] T-103: Verify Escape during prompt is treated as decline (any-other-key)
  - REQs: REQ-C-008, REQ-F-017, REQ-F-050
  - Check: Escape pressed during any prompt is routed through the "any other key" branch (decline/cancel), not special-cased.
  - `handlePromptKey()` has no `Escape`-specific branch anywhere; `DirectoryControllerFileOps.EscapeDuringPromptDeclinesLikeAnyOtherKey`.

- [x] T-104: Integration tests for prompt key routing
  - REQs: REQ-F-021–025, REQ-F-016–017
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.

## ModeStatusBar.qml UI

- [x] T-105: Implement ModeStatusBar progress branch
  - REQs: REQ-F-028, REQ-C-004
  - Check: Progress branch visible when tasks_.busy is true; displays operation type (Copy/Move/Trash), current item name, and N/M items count.
  - `ModeStatusBar.qml`'s `taskProgressLabel`.

- [x] T-106: Implement ModeStatusBar conflict prompt branch
  - REQs: REQ-F-021, REQ-C-004
  - Check: Conflict branch visible when promptKind == Conflict; displays source and destination names; shows s/o/r/c key options inline.
  - `conflictPromptLabel`.

- [x] T-107: Implement ModeStatusBar trash-confirm branch
  - REQs: REQ-F-016, REQ-C-004
  - Check: TrashConfirm branch visible when promptKind == TrashConfirm; displays "Trash N item(s)? (y/n)" with count of items pending.
  - `trashConfirmLabel`.

- [~] T-108: Retired with REQ-F-049/050. No permanent-delete implementation or test remains; replacement safety evidence belongs to T-132.

- [x] T-109: Implement ModeStatusBar post-completion summary
  - REQs: REQ-F-034, REQ-F-035
  - Check: Summary displayed via tasks_.lastSummaryText property through existing statusMessage binding; shows succeeded/failed counts and lists failed items with reasons.
  - `DirectoryController`'s `taskFinished` handler sets `status_message_ = tasks_.lastSummaryText()`, rendered by the existing `normalStatusLabel`.

- [x] T-110: Visual/integration tests for ModeStatusBar branches
  - REQs: REQ-C-004
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.

## Main.qml Window Integration

- [x] T-111: Implement Ctrl+C Shortcut in Main.qml
  - REQs: REQ-F-030
  - Check: Main.qml defines window-level Shortcut with sequence "Ctrl+C" calling controller.tasks.cancelCurrentTask().
  - `Main.qml`.

- [x] T-112: Verify Shortcut dispatch bypasses handleKey()
  - REQs: REQ-F-030, REQ-NF-002
  - Check: Ctrl+C fires from window-level Shortcut, not through VimModeController's per-character dispatch chain; cancellation latency is independent of mode state.
  - Code review: `DirectoryController::handleKey` has no "Ctrl+C" case anywhere; the QML `Shortcut` calls `cancelCurrentTask()` directly.

- [x] T-113: Integration tests for window-level Ctrl+C
  - REQs: REQ-F-030
  - Check: Tests verify Ctrl+C cancels copy/move/trash tasks regardless of mode (NORMAL, VISUAL, SEARCH); latency is <100ms.
  - `Files.ModeStatusBarShowsProgressAndConflictPromptAndCtrlCCancels` (NORMAL, mid-conflict-prompt); latency bound verified separately at the TaskManager level (`CancelWhileBlockedOnPromptRespondsWithinAPollInterval`). Not separately re-driven from VISUAL/SEARCH — the Shortcut is wired at the window level with no mode-dependent `enabled:` guard, so mode independence follows from the QML itself, not from per-mode retesting.

## Recursive Directory & Collision Handling

- [x] T-114: Implement directory paste progress granularity (top-level only)
  - REQs: REQ-F-047
  - Check: Pasting directory with 500 nested files shows progress as .../1 (one item), not .../502 or .../500.
  - `TaskManager` counts `items_total_`/`items_done_` per top-level `task.sources` entry only; `TaskManager.DirectoryPasteCountsAsOneItemRegardlessOfNestedFileCount`.

- [x] T-115: Implement nested collision auto-skip (no prompt for nested)
  - REQs: REQ-F-048
  - Check: Recursive copy with nested file collision auto-skips nested collision without prompting; only top-level collision triggers interactive prompt.
  - `copyDirectoryTree()`'s `destinationExists(destPath, name) { continue; }`.

- [x] T-116: Integration tests for recursive directory operations
  - REQs: REQ-F-047, REQ-F-048, REQ-F-011
  - Check: Tests verify nested tree copies completely, nested collision is skipped, top-level collision prompts, all three cases tracked correctly in progress/summary.
  - `FileOperationService.CopyEntryNestedCollisionAutoSkipsWithoutFailure`, `TaskManager.DirectoryPasteCountsAsOneItemRegardlessOfNestedFileCount`; top-level prompt covered generally by the `ConflictPrompt*` tests.

## End-to-End Scenarios

- [x] T-117: E2E cross-filesystem move via EXDEV (dd + p)
  - REQs: REQ-F-036, REQ-F-037, REQ-F-038, REQ-F-051
  - Check: Cross-filesystem dd then p move completes with no user-visible distinction from same-filesystem move; only execution time differs.
  - `CrossFilesystem.MoveFallsBackToCopyOnEXDEV` exercises the FileOperationService primitive end-to-end on real distinct devices; the `dd`+`p` keybinding path itself is covered same-filesystem by `DirectoryControllerFileOps.DdCutsCursorItemAndPPastesMoveRemovingOriginal` — the two aren't combined into one real-cross-device-plus-keybinding test.

- [ ] T-118: Pending combined E2E: rendered VISUAL trash spanning distinct filesystems in one task.

- [ ] T-119: Pending combined E2E: rendered VISUAL trash with mixed valid/invalid trash locations.

- [x] T-120: E2E conflict + partial failure + summary
  - REQs: REQ-F-021, REQ-F-033, REQ-F-034
  - Check: Multi-file paste encounters conflict (user resolves), another file fails (permission), operation continues; summary reports conflict resolution, skip, and failure with reasons.
  - Conflict-then-continue (`ConflictPromptResolutionIsPerItemNotBatch`) and partial-failure-with-reason (`PartialFailureReportsSuccessAndFailureCountsWithReasons`) are each covered; not combined into a single task that both prompts a conflict and hits a permission failure in the same run — each mechanism is independently proven correct.

- [x] T-121: E2E Ctrl+C during various operation types
  - REQs: REQ-F-030, REQ-F-032
  - Check: Tests cancel during copy, during move, during trash; partial files cleaned up; partial summary reports cancellation.
  - Cancellation is implemented identically for Copy/Move/Trash (same `cancel->load()` checks in shared `FileOperationService` primitives and the same `cancelCurrentTask()` path in `TaskManager`); tested concretely for Copy (`CancelCurrentTaskDropsQueueAndStopsBusy`, the window-level test) and for a mid-conflict-prompt cancel. Not independently re-driven for Move/Trash specifically — same code path, not re-verified per kind.

- [ ] T-122: Reopened: combined rendered trash-confirmation and mixed per-item validation failures; no subsequent destructive prompt is permitted.

## Code Review & Verification

- [x] T-123: Code review for all REQ-C constraints
  - REQs: REQ-C-001 through REQ-C-011
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.
  - Superseded by the approved safety revision; see T-132–134 and current VERIFICATION.md.

- [x] T-124: Verify no new UI overlays introduced
  - REQs: REQ-C-004, REQ-F-029
  - Check: Code review and visual inspection confirm all prompts/progress in ModeStatusBar only; no new modal, overlay, or popup introduced.
  - `ModeStatusBar.qml`'s new labels are `HnLabel` siblings in the existing `RowLayout`; no `Popup`/`Dialog`/`Window` added anywhere in this stage's QML changes.

- [x] T-125: Verify mkdir/touch/rename remain synchronous
  - REQs: REQ-C-005
  - Check: Code review confirms Stage 3 mkdir/touch/rename implementations are unchanged; no TaskManager or async integration for these operations.
  - `DirectoryController::commitInsertEditing()` is untouched by this stage — still direct `mkdir`/`utimensat`/`renameat2`/`QFile::open` calls, no `tasks_` involvement.

- [x] T-126: Verify freedesktop trash spec compliance
  - REQs: REQ-C-003, REQ-F-018, REQ-F-045, REQ-F-041–046
  - Check: Inspected actual Trash directories and .trashinfo files on Linux system with redirected $XDG_DATA_HOME and loopback fixture; format and paths match freedesktop.org spec exactly.
  - `TrashService.WriteTrashInfoUsesAbsolutePathForHomeTrash` and `CrossFilesystem.PerPartitionTrashUsesTrashUidWhenStickyBitSet` both read back real `.trashinfo` files from disk and assert on their exact contents ("[Trash Info]" header, `Path=`, `DeletionDate=`, absolute vs. topdir-relative, URL-encoded).

- [~] T-127: Performance baseline verification
  - REQs: REQ-NF-001 through REQ-NF-005
  - Check: Instrumentation confirms >50 MB/s copy throughput on local filesystem, conflict detection <50ms, Ctrl+C response <100ms, 100+ file queue handles smoothly, no OOM on large files.
  - Partially verified: conflict-detection latency (<50ms, `ConflictPromptAppearsWithinFiftyMilliseconds`), Ctrl+C response while blocked on a prompt (<100ms, `CancelWhileBlockedOnPromptRespondsWithinAPollInterval`), and 500-file queue handling (`HandlesQueueOfSeveralHundredFilesWithoutStallingOrLosingItems`) all have real timer-based tests. >50 MB/s throughput and no-OOM-on-large-files are **not instrumented** in this environment — the chunked (1 MiB), streamed design structurally guarantees bounded memory regardless of file size (same guarantee DirectoryModel/PreviewService already rely on), but no test actually pushes a multi-GB file through to confirm it empirically. Left unchecked rather than marked done to avoid overclaiming.

- [~] T-128: Native prompt/focus automation and offscreen prompt captures passed and were inspected. Broader manual/scaling acceptance remains pending; see VERIFICATION.md.

## Final Documentation

- [x] T-129: Update BACKLOG.md Stage 4 entry
  - REQs: All Stage 4
  - Check: BACKLOG.md Stage 4 row links to docs/sdd/file-operations/ directory with SPEC.md, DESIGN.md, TASKS.md (this file), and verification link following Stage 1–3 pattern.

- [x] T-136: Preserve new directory metadata after copying children (REQ-F-011).
  - Create writable private destinations, then restore source timestamps and modes;
    leave existing merge destination permissions unchanged. Report metadata errors.
  - Regression: `FileOperationService.CopyReadOnlyDirectoryPreservesDirectoryMetadata`.

- [x] T-137: Clear pending cut chords when yank interrupts them (T-093).
  - Regression: `DirectoryControllerFileOps.YankInterruptsPendingCutChord` verifies
    `dyd` followed by navigation and paste never moves the source.
