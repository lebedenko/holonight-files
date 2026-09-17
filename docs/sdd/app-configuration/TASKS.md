# SDD Tasks — app-configuration

- [x] T-001: Build infrastructure — find_package tomlplusplus and CMakeLists wiring
  - REQs: REQ-C-009
  - Check: CMakeLists.txt contains `find_package(tomlplusplus 3.4 CONFIG REQUIRED)`; apps/files/CMakeLists.txt links tomlplusplus::tomlplusplus; build succeeds.

- [x] T-002: WarningSink — stderr warning channel abstraction
  - REQs: REQ-C-011, REQ-F-010
  - Check: tests/warning_sink_test.cpp passes; StderrWarningSink::warn() outputs exactly one line to stderr.

- [x] T-003: XdgPaths and test fixtures — XDG config/state path resolution
  - REQs: REQ-C-001, REQ-F-001, REQ-F-007
  - Check: tests/xdg_paths_test.cpp: unset/empty/relative/absolute XDG_CONFIG_HOME and XDG_STATE_HOME all resolve correctly; new ScopedXdgConfigHome and ScopedXdgStateHome added to tests/directory_fixtures.h save/restore environment variables.

- [x] T-004: TomlDocument — TOML parsing with single-error guarantee
  - REQs: REQ-F-003, REQ-F-006, REQ-F-028, REQ-C-002
  - Check: tests/toml_document_test.cpp: a file with several syntax errors yields exactly one ParseError diagnostic carrying path and line; declared-type introspection reports Bool/String/Integer/Other; rootKeys() lists non-table top-level keys; grep -rn "toml++" apps/files/ matches only apps/files/settings/toml_document.cpp.

- [x] T-005: Setting and SettingsRegistry — typed settings declarations and validation engine
  - REQs: REQ-F-004, REQ-F-005, REQ-F-029, REQ-F-030, REQ-F-032
  - Check: tests/settings_registry_test.cpp: wrong-type and unknown-key warnings emitted correctly, source tracking works, test-only section declares and applies without modifying registry or other sections.

- [x] T-006: GeneralSettings and AppSettings — configuration loading facade
  - REQs: REQ-F-001, REQ-F-002, REQ-F-031, REQ-C-003, REQ-C-005
  - Check: tests/app_settings_test.cpp: missing config.toml uses defaults silently with no file creation, existing config.toml never written/mtime unchanged, const-only facade prevents modification.

- [x] T-007: LocationClassifier and RealLocationClassifier — path classification interface and implementation
  - REQs: REQ-F-022, REQ-F-023, REQ-F-024, REQ-F-025, REQ-F-026, REQ-F-027
  - Check: tests/location_classifier_test.cpp: classifyMount() with injected MountInfo correctly identifies network filesystems (nfs/cifs/9p/fuse.sshfs/gvfs), removable devices via /sys/block/dev/removable and mount prefixes, default-local for ext4/tmpfs/bind, unclassifiable as not-Local; RealLocationClassifier classifies the test's own QTemporaryDir as Local.

- [x] T-008: RestoreOutcome enum and reason mapping — fallback reason strings
  - REQs: REQ-F-013, REQ-F-014
  - Check: tests/initial_directory_test.cpp: restoreOutcomeReason() maps DoesNotExist/NotDirectory/NotReadable/NotLocal to exactly "last location does not exist", "last location is not a directory", "last location is not readable", "last location is not local" (the fifth reason, "no stored location", is produced by planStartup in T-011).

- [x] T-009: StateStore — atomic state file I/O with XDG compliance
  - REQs: REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-010, REQ-NF-001, REQ-C-004, REQ-C-007
  - Check: tests/state_store_test.cpp: round-trip save/load works, mkdir 0700 on missing directory, missing/corrupt/wrong-version/relative-path states all handled correctly, QSaveFile::cancelWriting leaves prior content byte-identical, read-only-directory failure (chmod 0555) emits warning and doesn't block (skipped if running as root).

- [x] T-010: LastLocationTracker — session-scoped most-recent-local-folder tracking
  - REQs: REQ-F-019, REQ-F-020, REQ-F-026
  - Check: tests/last_location_tracker_test.cpp: tracker records only Local classified paths, ignores Network/Removable, remains empty if no Local ever loads, candidate() returns last-recorded-Local path.

- [x] T-011: planStartup — startup resolution decision table
  - REQs: REQ-F-011, REQ-F-012, REQ-F-014, REQ-F-015
  - Check: tests/initial_directory_test.cpp: CLI arg wins with resolveInitialDirectory's unchanged result even when restore is enabled and a location is stored; restore disabled with a stored location → resolved home + "no folder given"; restore enabled without stored location → resolved home + "no stored location"; restore enabled with stored location → unresolved plan whose pendingCandidatePath equals it, with no filesystem access to that path.

- [x] T-012: DirectoryModel integration — classifier injection, restore validation, load-time tracking signals
  - REQs: REQ-F-016, REQ-F-019, REQ-NF-002
  - Check: tests/directory_model_test.cpp: validateForRestore() runs on worker thread (QThread::currentThread() != GUI), blocking classifier with 2s delay doesn't freeze concurrent 50ms timer, validateForRestore() reports DoesNotExist/NotDirectory/NotReadable (chmod 000, skipped as root)/NotLocal (injected Network classifier)/Ok; loadSucceeded fires only for a successful load() — not refresh(), not a failed load, not a load superseded before its final batch was accepted; DirectoryModelTestAccess::setLocationClassifier() added for injection.

- [x] T-013: DirectoryController integration — restore candidate, tracking, shutdown save
  - REQs: REQ-F-012, REQ-F-013, REQ-F-017, REQ-F-018, REQ-F-019, REQ-F-020, REQ-F-021, REQ-C-008
  - Check: tests/directory_controller_test.cpp: openRestoreCandidate on a valid local dir opens it with empty fallback reason; on a deleted dir / Network-classified dir opens home with the matching reason; an open() issued while validation is pending wins and the late result is discarded; with configureRestore(true) Local A→Network B→shutdown writes A; no Local load → state.toml byte-identical; configureRestore(false) never creates/modifies state.toml; an unwritable state dir emits one warning and shutdownFinished still fires; tests/directory_controller_test_access.h created with setWarningSink() and setLocationClassifier().

- [x] T-014: main.cpp wiring — settings/state load and startup resolution integration
  - REQs: REQ-C-005, REQ-F-001, REQ-F-007, REQ-F-011
  - Check: main() loads AppSettings and StateStore exactly once before planStartup and routes resolved plans to controller.open() and pending candidates to openRestoreCandidate(); an end-to-end test in tests/directory_controller_test.cpp saves via one controller's shutdown and restores the same path via planStartup + a second controller; manual launch of `hn-files` with isolated XDG_CONFIG_HOME/XDG_STATE_HOME and restore enabled reopens the folder closed in the previous run.

- [x] T-015: Code constraint verification — TOML adapter isolation and no QML exposure
  - REQs: REQ-F-028, REQ-C-006
  - Check: grep -rn "toml++" apps/files/ returns includes from toml_document.cpp only (no .h files, no other .cpp); grep -rnE "QML_ELEMENT|QML_SINGLETON|Q_INVOKABLE|Q_PROPERTY" apps/files/settings apps/files/state returns no matches.

- [x] T-016: check-install.sh — readelf assertion for tomlplusplus dependency
  - REQs: REQ-C-010
  - Check: scripts/check-install.sh includes readelf assertion `readelf -d "$stage/usr/bin/hn-files" | rg 'libtomlplusplus\.so\.3'` after staged-install checks; task check passes.

- [x] T-017: README — configuration and state documentation
  - REQs: REQ-F-001, REQ-F-007, REQ-C-003
  - Check: README.md documents the config.toml path and XDG fallback, the `[general] restore_last_location` key with default and an example, that the app never writes config.toml, the state.toml location and that only local folders are remembered, and the tomlplusplus build dependency.

- [x] T-018: Final verification — full project check
  - REQs: all
  - Check: `task check` exits 0 (debug/release builds, all CTest targets, format, clang-tidy, qml-lint, REUSE, install and uninstall checks); VERIFICATION.md records per-REQ evidence.

- [x] T-019: Review remediation — preserve accepted loads across refresh/navigation and drain classification before shutdown save
  - REQs: REQ-F-017, REQ-F-019, REQ-F-020
  - Check: deterministic gated-classifier regressions cover a settled load followed by refresh/navigation and close while classification is pending; existing cancellation/failure tests pass; every `task check` stage passes, with sandbox-required reruns documented in VERIFICATION.md.
