# App Configuration & State Management — Requirements Specification (EARS Format)

## Status

**Draft**

## Overview

App Configuration & State Management introduces persistent user preferences and application state to HoloNight Files. The system reads configuration from an XDG-compliant TOML file at startup (config.toml), stores navigation state in a separate state file (state.toml), and implements a Location Classifier to distinguish Local, Network, and Removable directories. The core feature is "restore last location" — when enabled, the app opens the folder the user last successfully navigated to. Configuration is read-only; files are never created or modified by the application. State writes occur only on clean shutdown, never on crash or forced termination. Configuration and state are strictly separated to preserve user intent and prevent network paths from being re-opened after system reboot.

## Scope

### In Scope

**Configuration File & Format:**
- File location: `$XDG_CONFIG_HOME/holonight-files/config.toml` (or `$HOME/.config/holonight-files/config.toml` if XDG_CONFIG_HOME is unset, empty, or non-absolute).
- Format: TOML, parsed with tomlplusplus 3.4.0 (system shared library via `find_package(tomlplusplus CONFIG)`).
- Single setting in scope: `[general] restore_last_location` (boolean), default false.
- Configuration is read once at startup; no live reload, file watching, or SIGHUP handler.

**State File & Format:**
- File location: `$XDG_STATE_HOME/holonight-files/state.toml` (or `$HOME/.local/state/holonight-files/state.toml` if XDG_STATE_HOME is unset, empty, or non-absolute).
- Top-level fields: `version = 1` and `[navigation] last_location = "/absolute/path"`.
- Written atomically using QSaveFile (temp file + rename pattern).
- State directory created with mode 0700 if missing.

**Startup Resolution & Fallback Reasons:**
- Precedence: CLI folder argument (if provided) always wins, regardless of restore setting.
- Second precedence: restore_last_location is true AND stored location exists AND is a directory AND is readable AND is classified Local.
- Fallback: open home directory with reason: "no stored location", "last location does not exist", "last location is not a directory", "last location is not readable", or "last location is not local".
- When restore is disabled, behavior is identical to today ("no folder given").
- Location classification runs on directory worker thread, never GUI thread.

**Location Classifier:**
- Returns one of three values: Local, Network, or Removable for any given path.
- Behind a replaceable interface (future MountService integration without caller changes).
- Classification criteria (filesystem type via QStorageInfo, mount point checks, removable device detection via `/sys/block/<dev>/removable`).

**State Saving Behavior:**
- Written only on clean application close (DirectoryController::shutdown path) and only while restore_last_location is true.
- Saved value: the most recent folder that loaded successfully AND was classified Local.
- If no local folder ever loaded successfully in the session, state.toml is not modified.
- While restore_last_location is false, state.toml is neither created nor modified on close.
- Multiple instances: last to close wins; no locking or coordination.

**Error Handling:**
- Missing config file: silently use all defaults, no files created, no stderr output.
- Unparseable config file: ignore entire file, use all defaults, emit exactly one warning on stderr with file path, line number, and parse error description.
- Key with wrong type: that key falls back to default, one warning emitted naming key and expected type, other valid keys still apply.
- Unknown keys or sections: warning emitted naming the key path, entry ignored, known keys still apply.
- Missing state file: treated as "no stored location", silent.
- Unreadable/corrupt state file: treated as "no stored location", one stderr warning.
- State write failure: one stderr warning emitted, application quits normally without prompts or delay.

**Architecture Constraints:**
- TOML library included only by one TOML-document adapter unit.
- Settings declared as typed declarations (key, default, description) grouped one file per section.
- Central registry performs validation/warnings and records value source (default vs config file).
- Read-only facade used by consumers.
- Adding a setting to an existing section requires no changes to registry or other sections' facades.
- Settings not exposed to QML in this cycle.

### Out of Scope

- Settings UI, dialogs, or preference panel.
- Writing, creating, or modifying config.toml via the application.
- Live reload of config changes without restart.
- CLI flag for alternate config path.
- Exposing settings to QML.
- Nearest-existing-ancestor fallback (only the exact stored path is restored).
- Places sidebar redesign.
- MountService, D-Bus, udisks2 integration, or advanced mount detection.
- Window geometry, size, position, or other UI state persistence.
- Per-profile or per-workspace configurations.

## Non-Goals

This stage explicitly does not include, and these decisions are firm (not deferred):

- **Settings UI:** No preference dialog, settings panel, or in-app configuration interface. Configuration is exclusively via text file editing outside the app.
- **Config file auto-generation:** The application never creates, modifies, or repairs config.toml. Users must manually create it if they want non-default settings.
- **Live reload:** Configuration changes require a full application restart. No file watching, SIGHUP handler, or hot-reload mechanism.
- **Advanced mount detection:** No integration with systemd mount units, automount monitors, or D-Bus. Classification is based on static filesystem type and mount-point checks.
- **State encryption or compression:** State file is plain TOML, unencrypted, readable by any process with user permissions.
- **Multiple state versions or migration:** Version field is hardcoded to 1; no migration logic or multi-version support in v1.
- **Window/UI state:** Geometry, sidebar width, column widths, sort order, or other transient UI state are not persisted. Only the last successfully-opened folder is remembered.
- **Per-partition state:** Last location is global, not per-mount or per-removable-device.

## Functional Requirements

### Configuration File Loading & Validation

**REQ-F-001: Read Configuration at Startup**

The system shall, at application startup before opening any folder, read the configuration file from the XDG-compliant path. If XDG_CONFIG_HOME is unset, empty, or non-absolute, the system shall fall back to `$HOME/.config/holonight-files/config.toml`.

Acceptance Criterion: A test sets XDG_CONFIG_HOME to unset/empty, places a config.toml in `$HOME/.config/holonight-files/`, starts the application, and verifies the file is read and its settings applied. A second test sets XDG_CONFIG_HOME to a custom directory, places config.toml there, and verifies it is read from that location.

---

**REQ-F-002: Missing Configuration File Uses Defaults**

The system shall, if the configuration file does not exist, silently use all default values, create no files, and emit no output to stderr or the UI.

Acceptance Criterion: A test redirects XDG_CONFIG_HOME to a temp directory (ensuring no config.toml exists), starts the application, verifies it opens the home directory with no error message, and confirms no files are created under the config directory.

---

**REQ-F-003: Unparseable Configuration File Triggers Warning**

When a configuration file exists but contains invalid TOML syntax, the system shall ignore the entire file, use all default values, and emit exactly one warning to stderr including the file path, line number, and parse error description.

Acceptance Criterion: A test creates a config.toml with invalid TOML (e.g., unclosed bracket, invalid date format), starts the application in a subprocess with stderr captured, verifies exactly one warning line appears on stderr naming the file path and line number, and confirms all settings use defaults.

---

**REQ-F-004: Wrong Type for Configuration Key**

When a configuration key exists but its value has the wrong type (e.g., `restore_last_location = "yes"` instead of a boolean), the system shall discard that key, fall back to its default, emit one warning naming the key and expected type, and continue applying other valid configuration keys.

Acceptance Criterion: A test creates a config.toml with `restore_last_location = "yes"` (string instead of boolean), starts the application, verifies exactly one warning names the key and expected type, and confirms the setting uses its default (false).

---

**REQ-F-005: Unknown Configuration Keys or Sections**

When a configuration file contains unknown keys or sections, the system shall emit one warning naming the unknown key path (e.g., `[unknown_section] key` or `[general] unknown_key`), ignore the unknown entry, and continue applying all known keys.

Acceptance Criterion: A test creates a config.toml with both valid and unknown keys/sections, starts the application, verifies warnings are emitted for unknown entries only, and confirms known keys are applied as expected.

---

**REQ-F-006: Single Parse Error Per Invalid File**

The system shall emit at most one warning per invalid or unparseable configuration file, regardless of how many syntax errors it contains.

Acceptance Criterion: A test creates a config.toml with multiple TOML syntax errors, starts the application, verifies exactly one warning appears on stderr (not one per error).

---

### State File Handling

**REQ-F-007: Read State File at Startup**

The system shall, at application startup after loading configuration but before opening the initial folder, read the state file from the XDG-compliant path. If XDG_STATE_HOME is unset, empty, or non-absolute, the system shall fall back to `$HOME/.local/state/holonight-files/state.toml`.

Acceptance Criterion: A test creates a state.toml in `$XDG_STATE_HOME/holonight-files/` with a valid `last_location` entry, starts the application with `restore_last_location = true`, and verifies the application attempts to open that stored location (or falls back if the location no longer exists).

---

**REQ-F-008: Ignore Corrupt or Unreadable State File**

When a state file exists but is unreadable, corrupted, has the wrong version, or contains an invalid/relative/missing `last_location` field, the system shall treat it as "no stored location", emit one warning to stderr naming the state file and the problem, and proceed with the fallback startup resolution. A missing state file is silent.

Acceptance Criterion: A test creates, in turn, an unreadable state.toml (mode 0000), a corrupted state.toml (invalid TOML), a state.toml with `version = 999`, and a state.toml with `last_location = "relative/path"`. For each, startup resolution yields home with reason "no stored location" and exactly one stderr warning; with no state.toml present, the same resolution emits no warning.

---

**REQ-F-009: State File Atomic Write with Temp File**

When the system writes state.toml on shutdown, it shall use atomic write semantics: write to a temporary file, then rename atomically. The state directory shall be created with mode 0700 if it does not exist.

Acceptance Criterion: Code review confirms state.toml is written only through QSaveFile::commit(). A test saves into a non-existent XDG_STATE_HOME subtree and verifies `holonight-files/` is created with mode 0700 and state.toml round-trips; a second test aborts a save (QSaveFile::cancelWriting) over an existing state.toml and verifies the previous content is unchanged.

---

**REQ-F-010: State Write Failure Does Not Block Shutdown**

If writing the state file fails (I/O error, permission denied, disk full, etc.), the system shall emit one warning to stderr and quit normally without prompts, dialogs, or delays.

Acceptance Criterion: A test redirects XDG_STATE_HOME to a read-only directory, closes the application with `restore_last_location = true`, captures stderr, verifies one warning appears, and confirms the application quits promptly without hanging.

---

### Startup Resolution

**REQ-F-011: CLI Argument Takes Precedence**

If the user provides a folder argument on the command line, the system shall open that folder, regardless of whether `restore_last_location` is enabled or a stored location exists.

Acceptance Criterion: A test starts the application with a CLI folder argument and `restore_last_location = true`, verifies the CLI folder is opened (not the stored location), and confirms the fallback reason is unchanged from the existing behavior.

---

**REQ-F-012: Restore Last Location When Enabled**

When `restore_last_location = true` and no CLI argument is provided, if a stored location exists, is a directory, is readable, and is classified Local, the system shall open it without any fallback reason message.

Acceptance Criterion: A test sets `restore_last_location = true`, navigates to a local folder, closes the app (saving state), restarts the app, and verifies it opens the same folder without a fallback reason in the status bar.

---

**REQ-F-013: Fallback to Home on Restore Failure**

When `restore_last_location = true` but the stored location is missing, is not a directory, is not readable, or is not classified Local, the system shall open the home directory and display a specific fallback reason.

Acceptance Criterion: A test creates four scenarios: (1) stored location path does not exist, (2) stored path is a file (not directory), (3) stored path has no read permission, (4) stored path is classified Network. For each, the app opens home and displays the correct reason in the status bar.

---

**REQ-F-014: Fallback Reason Messages**

When restore_last_location is true and the app falls back to home, the status bar shall display one of exactly five fallback reason messages: "no stored location", "last location does not exist", "last location is not a directory", "last location is not readable", or "last location is not local".

Acceptance Criterion: A test exercises all five fallback conditions and verifies the exact fallback reason string appears in the status bar for each.

---

**REQ-F-015: Unchanged Startup When Restore Disabled**

While `restore_last_location` is false and no CLI argument is given, the system shall open home with the existing "no folder given" fallback reason, ignoring any stored location.

Acceptance Criterion: A test writes a valid state.toml pointing to an existing local temp directory, sets `restore_last_location = false`, resolves startup without arguments, and verifies the result is home with reason "no folder given".

---

**REQ-F-016: Location Classification on Worker Thread**

The system shall classify stored locations (Local/Network/Removable check) on the directory worker thread, never on the GUI thread, so that checking a hung network mount does not freeze the UI.

Acceptance Criterion: A test injects a classifier that records `QThread::currentThread()` and verifies it is never the GUI thread, both for restore-time and load-time classification. A second test injects a classifier that blocks for 2 seconds and verifies the GUI event loop keeps processing a 50 ms timer during that time.

---

### State Saving

**REQ-F-017: Save State on Clean Close**

The system shall write state.toml only when the application shuts down cleanly via the DirectoryController::shutdown → shutdownFinished → quit path. Crash, kill -9, or forced termination shall not save state (previous state is retained).

Acceptance Criterion: A test navigates to a folder and quits cleanly, verifies state.toml is updated with the new location. A second test kills the process with SIGKILL mid-operation, verifies state.toml is not modified (prior state retained).

---

**REQ-F-018: Save Only If Restore Enabled**

The system shall write state.toml only if `restore_last_location = true` at close time. While false, state.toml shall never be created, modified, or deleted on shutdown.

Acceptance Criterion: A test sets `restore_last_location = false`, navigates folders, closes the app, and verifies state.toml is not created or modified. A second test deletes state.toml, sets restore to false, closes the app, and verifies the file is not recreated.

---

**REQ-F-019: Save Most Recent Local Folder**

The system shall save the most recent folder that loaded successfully and was classified Local. Folders whose load failed (inline error) are not candidates for saving. Navigating Local A → Network B → close stores path A.

Acceptance Criterion: A test navigates to a local folder (A), then to a network location (B, mocked as Network via classifier seam), then closes. Verifies state.toml contains the path of folder A, not B. Regression coverage also delays classification after the successful final listing batch is accepted: a refresh or subsequent navigation must not discard that load, and a clean close must process its classification before saving state.

---

**REQ-F-020: No Save If No Local Folder Loaded**

If no local folder successfully loaded in the entire session (e.g., only visited network locations, or only CLI argument to a network folder), the system shall not modify state.toml on close.

Acceptance Criterion: A test with classifier seam marks all folders as Network, navigates to several, closes the app, and verifies state.toml is unchanged from its prior state.

---

**REQ-F-021: Last-Close-Wins for Multiple Instances**

If multiple instances of the application are running and close simultaneously or in rapid succession, the instance that closes last shall determine the final saved state. No file locking or coordination is performed.

Acceptance Criterion: A test performs two sequential saves of different locations through two independent state-store instances pointing at the same XDG_STATE_HOME and verifies state.toml contains the second location.

---

### Location Classifier

**REQ-F-022: Classify Paths as Local, Network, or Removable**

The system shall determine the classification (Local, Network, or Removable) of any directory path via a replaceable LocationClassifier interface. Network detection is based on filesystem type (via QStorageInfo); Removable detection checks `/sys/block/<dev>/removable` and mount-point prefixes.

Acceptance Criterion: A test feeds the classifier injected mount information (filesystem type, device, mount root) and a fake sysfs root, and verifies one Local, one Network and one Removable result; a second test verifies the real implementation classifies the test's own temporary directory as Local.

---

**REQ-F-023: Network Filesystem Denylist**

The system shall classify a path as Network if its mount filesystem type matches one of: nfs, nfs4, cifs, smb3, smbfs, 9p, afs, ceph, glusterfs, davfs, fuse.sshfs, fuse.rclone, or any other known fuse network backend, or the path resides under `/run/user/<uid>/gvfs`.

Acceptance Criterion: A parameterised test feeds injected mount information for each listed filesystem type and verifies each is classified Network, and verifies a path under `/run/user/<uid>/gvfs` with filesystem type `ext4` is classified Network.

---

**REQ-F-024: Removable Device Detection**

The system shall classify a path as Removable if its backing block device has `/sys/block/<dev>/removable == 1`, OR the mount point is under `/run/media/` or `/media/`.

Acceptance Criterion: A test uses a fake sysfs root where `block/sdz/removable` contains `1` and injected mount information backed by `/dev/sdz1`, and verifies Removable; with `0` and a `/mnt/data` mount root it verifies Local; with `0` and mount roots `/run/media/u/usb` and `/media/usb` it verifies Removable.

---

**REQ-F-025: Everything Else Is Local**

The system shall classify paths as Local if they do not match Network or Removable criteria, including tmpfs, bind mounts, and fstab entries under `/mnt/` or other standard locations.

Acceptance Criterion: A test feeds injected mount information for `tmpfs`, `ext4` at `/mnt/data`, and `btrfs` at `/` (non-removable device) and verifies each is classified Local.

---

**REQ-F-026: Unclassifiable Paths Are Not Local**

If a path cannot be classified due to missing mount info or an error during classification, the system shall treat it as not Local (will not be restored, will not be stored).

Acceptance Criterion: A test feeds invalid (unresolvable) mount information and verifies the result is not Local and that a tracker receiving a successful load of that path does not store it.

---

**REQ-F-027: Classifier Seam for Testing**

The Location Classifier shall provide a test seam (injectable or replaceable implementation) so tests can mock Network and Removable classifications without requiring real network mounts or removable devices.

Acceptance Criterion: A test injects a classifier returning Network and verifies a valid stored location resolves to home with "last location is not local"; a second test injects a classifier returning Removable and verifies the same outcome.

---

### Configuration Architecture

**REQ-F-028: Single TOML Adapter Unit**

The TOML library (tomlplusplus) shall be included only by a single TOML-document adapter unit, never directly by consumers.

Acceptance Criterion: `grep -rn "toml++" apps/` returns includes from exactly one .cpp file and no headers.

---

**REQ-F-029: Typed Settings Declarations**

Settings shall be declared as typed declarations (key name, default value, human-readable description) grouped in one file per configuration section (e.g., `GeneralSettings.h` for `[general]` settings).

Acceptance Criterion: Code review confirms GeneralSettings (or similar) declares `restore_last_location` with its default, type, and description, in a dedicated header.

---

**REQ-F-030: Central Settings Registry**

A central registry shall perform validation, emit warnings, and record the source (default vs. config file) of each setting so a future settings UI can distinguish user-provided values from defaults.

Acceptance Criterion: Code review confirms a SettingsRegistry class performs all validation/warning logic and stores source metadata for each setting. A test retrieves a setting's source and verifies it is correct.

---

**REQ-F-031: Read-Only Settings Facade**

Consumers shall access settings via a read-only facade that does not permit modification. Settings are immutable after initialization.

Acceptance Criterion: Code review confirms the facade exposes only const accessors and consumers receive it by const reference; a test verifies values read through the facade match the loaded config and defaults.

---

**REQ-F-032: No Registry or Facade Changes When Adding Settings**

When adding a new setting to an existing section (e.g., a second setting to `[general]`), the registry and facades of other sections (e.g., `[server]`, `[ui]`) shall require no changes.

Acceptance Criterion: A test declares a test-only section with its own settings, registers it with a registry instance, and verifies it is validated (defaults, wrong-type and unknown-key warnings) with no change to registry source; code review confirms the registry contains no section- or key-specific names.

---

## Non-Functional Requirements

**REQ-NF-001: State File Survives Interrupted Write**

If the process terminates while state.toml is being written, the previous state.toml shall remain intact and readable.

Acceptance Criterion: Covered by the REQ-F-009 abort test (uncommitted QSaveFile leaves prior content byte-identical).

---

**REQ-NF-002: UI Responsiveness During Classification**

Location classification shall never block the GUI thread; a slow or hung mount may delay the first listing but shall not freeze input or rendering.

Acceptance Criterion: Covered by the REQ-F-016 blocking-classifier test (50 ms GUI timer keeps firing during a 2 s classification).

---

## Constraints

**REQ-C-001: XDG Base Directory Compliance**

The system shall use XDG_CONFIG_HOME, XDG_STATE_HOME, and their fallbacks per the XDG Base Directory specification. Paths are resolved at startup; no dynamic re-resolution on XDG_* variable changes during runtime.

Acceptance Criterion: A unit test resolves config and state paths with XDG_CONFIG_HOME/XDG_STATE_HOME set to absolute temp paths, set to empty, and set to a relative path, and verifies the expected XDG path or `$HOME` fallback in each case.

---

**REQ-C-002: TOML Format Only**

Configuration and state files shall be TOML format (RFC 5382/TOML 1.0.0) only. JSON, YAML, INI, or other formats are not supported.

Acceptance Criterion: Code review confirms only config.toml and state.toml are read; no other file names or formats are probed.

---

**REQ-C-003: Read-Only Configuration**

The application shall never create, modify, delete, or repair config.toml. All file I/O for config.toml is read-only.

Acceptance Criterion: A test loads a valid config.toml with warnings (unknown key) and records its bytes and mtime, runs load plus a state save, and verifies config.toml is byte-identical with unchanged mtime; with no config.toml, verifies none is created.

---

**REQ-C-004: Atomic State Write Only**

State.toml writes shall always be atomic (temp file + rename). No partial, in-place, or non-atomic write pattern is permitted.

Acceptance Criterion: Code review confirms the only write path for state.toml is QSaveFile with commit(); see REQ-F-009 abort test.

---

**REQ-C-005: Single Configuration Parse Per Startup**

Configuration shall be parsed exactly once per application startup, before any folder is opened. No re-parsing or re-loading on signal/timer/event is performed.

Acceptance Criterion: Code review confirms settings are loaded once in main() before startup resolution and passed by const reference; no file watcher, signal handler or timer reloads them.

---

**REQ-C-006: No Settings Exposure to QML**

Settings (configuration and state) shall not be exposed to QML in this cycle. All access is C++ only.

Acceptance Criterion: `grep -rnE "QML_ELEMENT|QML_SINGLETON|Q_INVOKABLE|Q_PROPERTY" apps/files/settings apps/files/state` returns no matches, and no settings object is passed to engine initial properties.

---

**REQ-C-007: Absolute Paths Only for State**

The `last_location` field in state.toml shall always be an absolute path. Relative paths are rejected and treated as "no stored location".

Acceptance Criterion: A test creates state.toml with a relative path, starts the app, and verifies it is rejected and home is opened.

---

**REQ-C-008: No Locking or Coordination**

Multiple instances of the application shall not coordinate or lock state.toml. Last-close-wins (REQ-F-021) is the only semantics; no atomic multi-instance coordination is performed.

Acceptance Criterion: Code review confirms no file locking or inter-process synchronization for state.toml; behaviour covered by the REQ-F-021 sequential-save test.

---

**REQ-C-009: tomlplusplus 3.4.0 Minimum**

The build shall require tomlplusplus 3.4 or later, found via `find_package(tomlplusplus 3.4 CONFIG REQUIRED)` and linked as the system shared library (`tomlplusplus::tomlplusplus`).

Acceptance Criterion: CMakeLists.txt contains `find_package(tomlplusplus 3.4 CONFIG REQUIRED)`; `readelf -d` on the built `hn-files` lists `libtomlplusplus.so.3` as NEEDED.

---

**REQ-C-010: Packaging and CI Carry the Dependency**

The CI image, the runtime-check image and the install check shall account for tomlplusplus.

Acceptance Criterion: `packaging/Dockerfile.ci` and `packaging/Dockerfile.runtime-check` install `tomlplusplus`; `task install-check` fails if the installed `hn-files` does not list `libtomlplusplus.so.3` as NEEDED, and `task check` passes end to end.

---

**REQ-C-011: Warning Output to stderr Only**

All warnings (parse errors, type mismatches, unknown keys) shall be emitted to stderr only. No warnings appear in stdout, logs, or the UI status bar (except fallback reasons, which are intentional).

Acceptance Criterion: A test triggers an unknown-key and a wrong-type warning, captures warning output, and verifies both are emitted through the stderr warning channel and the resolved fallback reason is unaffected.

---

## Verification & Testing

### Test Fixtures Required

1. **Redirected XDG Directories:**
   - Tests isolate configuration and state by setting XDG_CONFIG_HOME and XDG_STATE_HOME to temporary directories.
   - Ensures tests do not pollute the user's real `~/.config` and `~/.local/state`.

2. **Injected Location Classifier:**
   - Tests inject a mock LocationClassifier to simulate Network and Removable classifications without real mounts.
   - Allows testing restore logic without requiring loopback devices or actual USB mounts.

3. **Invalid TOML Files:**
   - Pre-built TOML files with syntax errors, type mismatches, unknown keys, for parsing error testing.

4. **Injected Mount Information and Fake sysfs Root:**
   - Classifier tests feed filesystem type, device and mount root directly, plus a temporary directory standing in for `/sys`; no real network or removable mounts are required.

5. **Read-Only Directory:**
   - For testing state write failures (chmod 0555 on state dir).

### Test Coverage Baseline

- Every requirement is covered by its acceptance criterion, as a test or a stated code-review/grep check.
- Acceptance criteria are the test pass condition.
- Multi-requirement coverage: config parsing with warnings, state save/restore cycles, startup resolution precedence, classifier seams.

### Integration with Existing Systems

- **apps/files/initial_directory.cpp:** Existing CLI argument handling remains unchanged; StartupResolver reads configuration and state, respects CLI arg precedence (REQ-F-011).
- **main.cpp / DirectoryController::shutdown:** main() loads settings and state before startup resolution; the shutdown path writes state (if restore_last_location is true).
- **DirectoryModel::load:** Loading a folder triggers classification on the worker thread; classification result affects whether the path is eligible for saving.
- **ModeStatusBar.qml:** Fallback reason is displayed here, same as existing "no folder given" messages.

---

## Non-Functional Details

### Future-Proofing

- **Settings Registry Pattern:** Adding future settings (e.g., `[places] pinned`) requires only a new section file and one registration, not changes to the central registry or other sections.
- **Layered Sources:** Per-value source tracking allows a future settings-UI layer (a separate app-owned file) to be merged under config.toml without rewriting the loader.
- **Location Classifier Interface:** Designed for replacement; MountService can be swapped in without caller changes.
- **State Format Versioning:** `version = 1` field allows future migrations (v2, v3, etc.) without breaking the file format.

---

## Traceability Table

| Requirement ID | Verification Method | Status |
|---|---|---|
| REQ-F-001 | Unit test: XDG config path resolution | Unverified |
| REQ-F-002 | Unit test: missing config uses defaults | Unverified |
| REQ-F-003 | Integration test: unparseable config stderr warning | Unverified |
| REQ-F-004 | Unit test: wrong type key fallback + warning | Unverified |
| REQ-F-005 | Unit test: unknown key warning ignored | Unverified |
| REQ-F-006 | Unit test: single parse error per file | Unverified |
| REQ-F-007 | Unit test: XDG state path resolution | Unverified |
| REQ-F-008 | Unit test: corrupt state ignored silently | Unverified |
| REQ-F-009 | Integration test: atomic state write with temp file | Unverified |
| REQ-F-010 | Integration test: state write failure does not block quit | Unverified |
| REQ-F-011 | Integration test: CLI arg precedence | Unverified |
| REQ-F-012 | Integration test: restore last location (success path) | Unverified |
| REQ-F-013 | Integration test: fallback to home on restore failure | Unverified |
| REQ-F-014 | Integration test: all five fallback reason messages | Unverified |
| REQ-F-015 | Unit test: restore disabled keeps "no folder given" | Unverified |
| REQ-F-016 | Instrumentation test: classification on worker thread | Unverified |
| REQ-F-017 | Integration test: state saved on clean close only | Unverified |
| REQ-F-018 | Integration test: state written only if restore enabled | Unverified |
| REQ-F-019 | Integration test: save most recent local folder | Unverified |
| REQ-F-020 | Integration test: no save if no local folder loaded | Unverified |
| REQ-F-021 | Unit test: sequential saves, last wins | Unverified |
| REQ-F-022 | Unit test: classifier returns Local/Network/Removable | Unverified |
| REQ-F-023 | Unit test: network filesystem denylist | Unverified |
| REQ-F-024 | Unit test: removable device detection | Unverified |
| REQ-F-025 | Unit test: default local classification | Unverified |
| REQ-F-026 | Unit test: unclassifiable treated as not local | Unverified |
| REQ-F-027 | Unit test: classifier seam (injectable) | Unverified |
| REQ-F-028 | Code review: single TOML adapter unit | Unverified |
| REQ-F-029 | Code review: typed settings declarations | Unverified |
| REQ-F-030 | Code review + test: central registry with source tracking | Unverified |
| REQ-F-031 | Code review: read-only facade | Unverified |
| REQ-F-032 | Unit test + code review: test-only section registers without registry changes | Unverified |
| REQ-NF-001 | Unit test: aborted save keeps prior state | Unverified |
| REQ-NF-002 | Integration test: blocking classifier keeps GUI responsive | Unverified |
| REQ-C-001 | Code review + test: XDG paths used, not re-resolved | Unverified |
| REQ-C-002 | Code review: TOML format only | Unverified |
| REQ-C-003 | Code review + test: config.toml read-only | Unverified |
| REQ-C-004 | Code review + test: atomic state writes | Unverified |
| REQ-C-005 | Code review + test: single parse per startup | Unverified |
| REQ-C-006 | Code review: no QML settings access | Unverified |
| REQ-C-007 | Unit test: relative paths rejected | Unverified |
| REQ-C-008 | Code review: no locking or coordination | Unverified |
| REQ-C-009 | Build verification: find_package + readelf NEEDED | Unverified |
| REQ-C-010 | Build/install check: packaging images and NEEDED entry | Unverified |
| REQ-C-011 | Unit test: warnings through stderr channel | Unverified |

---

## Related Documents

- **CLAUDE.md** — Project state and relationship to HoloNight umbrella.
- **docs/sdd/file-operations/SPEC.md** — Sibling stage specification (file operations, Stage 4).
- **apps/files/initial_directory.cpp** — Current CLI folder resolution.

---

## Acceptance Criteria Summary

Every REQ-F-XXX, REQ-NF-XXX, and REQ-C-XXX requirement above includes an inline, independently-verifiable acceptance criterion. Acceptance is contingent on:

1. **Code review:** Confirms all functional, non-functional, and constraint requirements are implemented and integrated correctly.
2. **Automated test suite:** Passes all tests corresponding to each requirement's acceptance criterion.
3. **Manual verification:** Startup and shutdown behavior match the spec; fallback reasons are displayed correctly; configuration is read from the correct XDG path; state is saved only on clean close.
4. **Integration checks:** Existing tests (build, format, tidy, qml-lint, REUSE, install checks) pass with the new tomlplusplus dependency added.

No requirement lacks an acceptance criterion. Implementation cannot proceed to code review until this spec is approved.

---

## Acceptance Scenarios Summary

The following scenarios cover the major acceptance paths:

1. **No config file:** app opens home, no files created, reason unchanged.
2. **restore enabled; navigate and close:** state.toml created with that path; next start opens it.
3. **restore enabled; stored path deleted:** home + reason shown.
4. **corrupt state.toml:** home opened with one stderr warning.
5. **corrupt config.toml:** defaults used + exactly one stderr warning.
6. **restore disabled; close:** state.toml not created/modified.
7. **classifier seam marks folder Network; visit local A then network B; close:** A stored.
8. **CLI arg + restore enabled:** CLI folder opened, not stored path.
9. **Existing checks (build, tests, format, tidy, qml-lint, REUSE, install):** pass with tomlplusplus dependency.
