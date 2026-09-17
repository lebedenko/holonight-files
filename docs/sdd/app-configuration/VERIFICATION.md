# App Configuration Verification Record

Date: 2026-09-17

## Scope
Cycle verification for `docs/sdd/app-configuration` (SPEC, DESIGN, TASKS and implementation).

## Command results
- [x] `task check` exits 0: debug and release builds, all 8 CTest targets, clang-format and QML format,
  clang-tidy, qml-lint, REUSE, install check (including the new `libtomlplusplus.so.3` NEEDED assertion)
  and uninstall check. The first run failed clang-tidy on 9 findings in new code; those were fixed and the
  rerun passed.
- [x] Manual native launch (T-014), passed on the user's Hyprland session: with restore enabled and isolated
  `XDG_CONFIG_HOME`/`XDG_STATE_HOME`, the first run saved `last_location = "/home/andrii/Pictures/holonightw"` to
  `state.toml` on window close (state directory mode 0700), and the relaunch reopened that folder with an empty
  status bar and no stderr output.

## Per-requirement evidence (all tests in `files-smoke`)
| REQ | Evidence |
|---|---|
| F-001 | `AppSettings.ReadsFromXdgConfigHome`, `AppSettings.UnsetOrEmptyXdgConfigHomeReadsFromHomeConfig` |
| F-002 | `AppSettings.MissingConfigUsesDefaultsSilentlyAndCreatesNothing` |
| F-003, F-006 | `TomlDocument.SeveralSyntaxErrorsYieldExactlyOneDiagnosticWithLine`, `AppSettings.UnparseableConfigUsesDefaultsWithOneWarningNamingPathAndLine` |
| F-004 | `SettingsRegistry.WrongTypeFallsBack...`, `AppSettings.WrongTypeWarnsNamingKeyAndTypeAndUsesDefault` |
| F-005 | `SettingsRegistry.UnknownKeysAndSectionsWarnByPathAndAreIgnored`, `AppSettings.UnknownEntriesWarnWhileKnownKeysApply` |
| F-007, F-008, C-007 | `StateStore.MissingStateIsSilentlyEmpty`, `StateStore.UnusableStateFilesWarnOnceAndYieldNoLocation`, `StateStore.UnreadableStateFileWarnsOnce` |
| F-009, NF-001, C-004 | `StateStore.SaveCreatesPrivateDirectoryAndRoundTrips`, `StateStore.AbortedWriteLeavesPreviousStateByteIdentical`; `QSaveFile::commit()` is the only write path |
| F-010 | `StateStore.WriteFailureWarnsOnceAndReturnsPromptly`, `DirectoryController.StateWriteFailureWarnsOnceAndStillFinishesShutdown` |
| F-011, F-014, F-015 | `InitialDirectory.PlanStartupPrecedence`, `InitialDirectory.RestoreOutcomeReasonsAreExact` |
| F-012 | `DirectoryController.RestoreCandidateThatIsLocalOpensWithoutReason`, `DirectoryController.SavedLocationIsRestoredByTheNextSession` |
| F-013, F-027 | `DirectoryController.FailedRestoreOpensHomeWithMatchingReason`, `DirectoryModel.ValidateForRestoreReportsEachOutcomeFromTheWorkerThread` |
| F-016, NF-002 | `DirectoryModel.ValidateForRestore...` (worker thread), `DirectoryModel.SlowClassificationKeepsTheEventLoopResponsive`, `DirectoryModel.LoadSucceededFiresOnlyForSuccessfulCurrentLoads` |
| F-017, F-019 | `DirectoryController.ShutdownSavesTheLastLocalFolder` (SIGKILL case follows from saving only in `shutdown()`) |
| F-018 | `DirectoryController.DisabledRestoreNeverWritesState` |
| F-020 | `DirectoryController.ShutdownWithoutAnyLocalLoadLeavesStateUntouched` |
| F-021, C-008 | `StateStore.LastSaveWins`; no locking in `StateStore` |
| F-022..F-026 | `LocationClassifier.*`, `LocationClassifier/NetworkFilesystems.*`, `LastLocationTracker.*` |
| F-028 | `TomlDocument.TomlLibraryIsIncludedOnlyByTheAdapter`; `grep -rn "toml++" apps/files/` matches only `toml_document.cpp` |
| F-029..F-032 | `GeneralSettings` declaration; `SettingsRegistry.*` use a test-only section; `AppSettings` has only const accessors |
| C-001 | `XdgPaths.*` |
| C-002, C-005 | Code review: `main()` reads `config.toml`/`state.toml` once before planning startup; no watcher or reload |
| C-003 | `AppSettings.ExistingConfigIsNeverModified` |
| C-006 | `grep -rnE "QML_ELEMENT|QML_SINGLETON|Q_INVOKABLE|Q_PROPERTY" apps/files/settings apps/files/state` finds nothing |
| C-009, C-010 | `find_package(tomlplusplus 3.4 CONFIG REQUIRED)`; install check asserts NEEDED; `Dockerfile.ci` already installs tomlplusplus and `Dockerfile.runtime-check` is built `FROM files-ci` |
| C-011 | `WarningSink.StderrSinkWritesExactlyOneLineToStderr`; all warnings go through `WarningSink` |

## Deviations from DESIGN.md
- `classifyMount()` also takes the path, because the GVFS rule (REQ-F-023) depends on the path, not only the mount facts.
  An unresolvable mount returns `Network`, so the classifier still returns one of three values and the path is treated as not Local.
- `DirectoryControllerTestAccess` already existed inline in `directory_controller_test.cpp`. It was moved to the new header and extended.
- The format/tidy globs (`CMakeLists.txt`, `Taskfile.yml`) now include `apps/files/settings/` and `apps/files/state/`.
- `install-check` now runs the staged binary with isolated `XDG_CONFIG_HOME`/`XDG_STATE_HOME`, so a user's own config can't add warnings to its log.
- `main()` only reads `state.toml` when restore is enabled and no folder argument was given.

## Review remediation — pending classification (2026-09-17)

- Fixed REQ-F-019 tracking: a successful final batch accepted on the GUI thread remains
  eligible when classification returns after a refresh, navigation or shutdown request.
  Loads superseded before acceptance remain excluded; refreshes do not reclassify.
- Fixed REQ-F-017/019 shutdown ordering: save after all three worker shutdown notifications,
  before `shutdownFinished`, so accepted-load classification reaches the tracker first.
- Added deterministic gated-classifier regressions:
  `DirectoryModel.AcceptedLoadClassificationSurvivesRefreshAndNavigation` and
  `DirectoryController.ShutdownSavesAcceptedLoadWithPendingClassification`.
  The latter covers both direct close and a queued refresh, retains previous state until
  classification completes, and checks the saved path when `shutdownFinished` is emitted.
- Focused controller/model tests: all 94 passed (see `build/review/race-tests.log`).
- `task check` completed debug/release builds and the test suite, then stopped on the existing
  socket-copy test's sandbox-denied bind. Of 429 smoke tests, 426 passed and two opt-in tests
  skipped; all seven other CTest targets passed. The socket-copy test passed when rerun outside
  the sandbox with its exact GTest filter.
- Completed the remaining `task check` stages individually, in order: `task format-check`,
  `task lint` (clang-tidy and QML lint), `task license-check`, `task install-check`, and
  `task uninstall-check` all passed. REUSE needed an outside-sandbox rerun because Python's
  worker pool could not bind its local socket. Logs: `build/review/task-check.log`,
  `build/review/remaining-checks.log`, and `build/review/install-checks.log`.
- Used existing installed provider packages; no sibling sources changed. No native visual
  recheck was needed for this tracking/shutdown change.
- Existing limitation retained: shutdown still waits for a worker blocked in filesystem I/O
  or classification; these fixes introduce no additional worker or blocking GUI wait.
