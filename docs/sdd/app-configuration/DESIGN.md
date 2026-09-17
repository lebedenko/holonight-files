# App Configuration & State Management — Design

**Document ID:** SDD-APP-CONFIG-DESIGN
**Version:** 1.0
**Date:** 2026-09-17
**Status:** Draft
**Traces to:** `docs/sdd/app-configuration/SPEC.md` (REQ-F-001..032, REQ-NF-001..002, REQ-C-001..011)

---

## 1. Overview

This feature adds read-only configuration (`config.toml`), write-on-clean-shutdown state
(`state.toml`), and a `LocationClassifier` (Local/Network/Removable) to HoloNight Files, and wires
them into the existing startup/shutdown paths in `main.cpp` and `DirectoryController`.

Two new source groups are added: `apps/files/settings/` (config loading, validation, read-only
facade) and `apps/files/state/` (state file I/O, last-successful-local-folder tracking), plus two
small shared, single-purpose files at `apps/files/` top level (`location_classifier.{h,cpp}`,
`restore_outcome.{h,cpp}`, `warning_sink.{h,cpp}`) that both groups depend on and that don't belong
to either.

The one integration point requiring real design work is **restore-time classification off the GUI
thread** (REQ-F-016): today `main()` resolves the initial directory synchronously and defers only
the `controller.open()` call via `QTimer::singleShot(0, ...)`. When `restore_last_location = true`
and a stored candidate path exists, validating and classifying that path (which may be a hung
network mount) can no longer happen inline in `main()`. §4.1.2 shows the mechanism — reusing
`DirectoryModel`'s existing persistent worker thread (already running by the time `main()` reaches
this point) — §10 item 1 gives the rationale, and §11 lists the rejected alternatives.

Load-time classification (REQ-F-019: "most recent folder that loaded successfully and was
classified Local") hooks into `DirectoryModel`'s existing walk-completion path: the classifier runs
on the worker thread immediately after a `load()` (not `refresh()`) finishes without error, and the
result is carried out on the existing `changed()`-adjacent signal path as a new `loadSucceeded`
signal — no new thread, no new signal-delivery mechanism.

---

## 2. Components

### 2.1 New files

| File | Responsibility | REQs |
|---|---|---|
| `apps/files/settings/toml_document.h` / `.cpp` | **Only** TU including `<toml++/toml.h>`. Parses a file into a Qt/std-shaped `TomlDocument` (sections/keys/typed values) plus a diagnostics list. Never exposes a toml++ type. | REQ-F-003/006/028 |
| `apps/files/settings/setting.h` | `Setting<T>{key, defaultValue, description}` — a typed declaration, no logic. | REQ-F-029 |
| `apps/files/settings/general_settings.h` / `.cpp` | `[general]` section: declares `restore_last_location`, registers itself with a `SettingsRegistry`, exposes the resolved bool. | REQ-F-018 (config side), REQ-F-029 |
| `apps/files/settings/settings_registry.h` / `.cpp` | Generic validation engine: type-checks declared keys against a `TomlDocument`, defaults on mismatch, warns on unknown sections/keys, records `Default`/`ConfigFile` source per key. Contains no section- or key-specific names. | REQ-F-004/005/030/032 |
| `apps/files/settings/app_settings.h` / `.cpp` | Read-only facade: `AppSettings::load(path, WarningSink&)` orchestrates `TomlDocument::parseFile` → registers all sections → `SettingsRegistry::apply()` → returns an immutable `AppSettings`. `settings.general().restoreLastLocation()`. | REQ-F-001/002/031, REQ-C-003/005 |
| `apps/files/settings/xdg_paths.h` / `.cpp` | `XdgPaths::configFilePath()`, `stateFilePath()`, `stateDirPath()` — XDG resolution with the unset/empty/non-absolute fallback rule. | REQ-F-001/007, REQ-C-001 |
| `apps/files/warning_sink.h` / `.cpp` | `WarningSink` (abstract) + `StderrWarningSink`. Used by both `settings/` and `state/`, so it lives at top level rather than inside either (deviation from the proposed layout — rationale in §10 item 5). | REQ-C-011 |
| `apps/files/location_classifier.h` / `.cpp` | `LocationClassifier` interface + `RealLocationClassifier` (QStorageInfo + `/sys/block/<dev>/removable`, injectable sysfs root) + a pure `classifyMount(MountInfo, sysfsRoot)` free function for direct unit testing. | REQ-F-022..027 |
| `apps/files/restore_outcome.h` / `.cpp` | `RestoreOutcome` enum (`Ok`/`DoesNotExist`/`NotDirectory`/`NotReadable`/`NotLocal`) + `restoreOutcomeReason()` mapping to the four REQ-F-014 strings. Shared by `initial_directory.cpp` (consumer) and `directory_model.cpp` (producer); a third small file rather than forcing either to include the other's heavier header (deviation — rationale in §10 item 4). | REQ-F-013/014/016 |
| `apps/files/state/state_store.h` / `.cpp` | `StateStore::load()` (parse, version/absolute-path validation, silent-missing) and `StateStore::save()` (mkdir 0700, `QSaveFile` + `commit()`). | REQ-F-007/008/009/010/017/018/019/020/021, REQ-NF-001, REQ-C-004/007/008 |
| `apps/files/state/last_location_tracker.h` / `.cpp` | Plain value class (no `QObject`, mirrors `ClipboardRegister`): the single most-recent Local successful-load path for the session. | REQ-F-019/020/026 |

### 2.2 Modified files

| File | Change | REQs |
|---|---|---|
| `apps/files/main.cpp` | Loads `AppSettings` and `StateStore` before startup resolution; builds a `StartupPlan`; passes the restore flag to the controller; the deferred `QTimer::singleShot` branches on whether the plan is already resolved or needs async validation. | REQ-F-001/007/011, REQ-C-005 |
| `apps/files/initial_directory.h` / `.cpp` | Adds `StartupPlan planStartup(...)`; `resolveInitialDirectory()` itself is unchanged (REQ-F-011's "existing reasons unchanged") and is called from inside `planStartup()` for the CLI-arg case. | REQ-F-011..015 |
| `apps/files/directory_model.h` / `.cpp` | Adds an injected `std::shared_ptr<LocationClassifier>` (default: `RealLocationClassifier`), `validateForRestore(path)`, and two new signals: `restoreValidated`, `loadSucceeded`. Both run the classifier on the existing worker thread. | REQ-F-016/019/022, REQ-NF-002 |
| `apps/files/directory_controller.h` / `.cpp` | Owns `LastLocationTracker`; `configureRestore(bool)`; connects to `loadSucceeded`/`restoreValidated`; `shutdown()` triggers the three worker shutdowns; `handleWorkerShutdown()` saves state synchronously after their completion and before `shutdownFinished`. | REQ-F-017/018/019/020, REQ-F-016 |
| `apps/files/CMakeLists.txt` | Adds all new files to `files-ui`'s `SOURCES`; links `tomlplusplus::tomlplusplus`. | REQ-C-009 |
| `CMakeLists.txt` (top level) | `find_package(tomlplusplus 3.4 CONFIG REQUIRED)`. | REQ-C-009 |
| `tests/CMakeLists.txt` | New test files added to `files-smoke`'s sources (§8). | — |
| `packaging/Dockerfile.ci`, `packaging/Dockerfile.runtime-check` | Already list `tomlplusplus` in the CI image's package set (confirmed during grounding — no change needed there); `scripts/check-install.sh` gets one new `readelf` assertion. | REQ-C-010 |

### 2.3 Unmodified but load-bearing

- `apps/files/directory_model.cpp`'s `walkDirectory`/batching/`generation_` machinery — the new classification hook rides the existing "final batch, no error, not a diff" condition; no new batching logic.
- `tests/directory_fixtures.h`'s `ScopedXdgDataHome` — the pattern this design generalizes for config/state isolation (§8, not modifying the file here, but described for the implementation stage).
- `apps/files/trash_service.cpp`'s `xdgDataHome()` — precedent for resolving one XDG variable with a local fallback; `XdgPaths` extends it with the unset/empty/**non-absolute** check REQ-C-001 requires that this existing helper doesn't have.

---

## 3. Public interfaces (sketched)

```cpp
// apps/files/settings/xdg_paths.h
namespace XdgPaths {
QString configFilePath();  // $XDG_CONFIG_HOME/holonight-files/config.toml, or $HOME/.config/...
QString stateFilePath();   // $XDG_STATE_HOME/holonight-files/state.toml, or $HOME/.local/state/...
QString stateDirPath();    // parent of stateFilePath(), for mkdir 0700
}

// apps/files/settings/toml_document.h
struct TomlDiagnostic {
  enum class Kind { ParseError, WrongType, UnknownEntry };
  Kind kind;
  QString path;     // dotted key path ("general.restore_last_location"), empty for ParseError
  QString message;  // ready to prepend to a stderr warning
  int line = 0;
};
struct TomlValue {
  // Other = any TOML type the registry does not declare (array, table, float, date) — always a
  // wrong-type diagnostic when a declared key has it.
  enum class Type { Missing, Bool, String, Integer, Other };
  Type type = Type::Missing;
  bool boolValue = false;
  QString stringValue;
  qint64 intValue = 0;
};
class TomlDocument {
 public:
  // fileExists=false + no diagnostics => REQ-F-002 (silent defaults). fileExists=true + parse
  // failure => exactly one ParseError diagnostic, document behaves empty thereafter (REQ-F-006).
  static TomlDocument parseFile(const QString& path, bool& fileExists, std::vector<TomlDiagnostic>& diagnostics);
  TomlValue value(const QString& section, const QString& key) const;
  std::vector<QString> sections() const;                    // top-level tables
  std::vector<QString> keys(const QString& section) const;  // keys inside a table
  std::vector<QString> rootKeys() const;                    // non-table top-level keys (always unknown, REQ-F-005)
 private:
  struct Impl;                 // toml::table lives only in toml_document.cpp
  std::shared_ptr<Impl> impl_;
};

// apps/files/settings/setting.h
template <typename T>
struct Setting {
  const char* key;
  T defaultValue;
  const char* description;
};

// apps/files/settings/settings_registry.h
enum class SettingSource { Default, ConfigFile };
struct SettingInfo {          // retained after load: the metadata a future settings UI renders
  QString section, key, description;
  TomlValue::Type type;
  SettingSource source;
};
// Pull model, no raw out-pointers: sections declare Setting<T> objects, apply() resolves every
// declared value into the registry's own storage, then sections read back typed values. The
// registry never stores pointers into section objects, so AppSettings stays safely copyable/movable.
class SettingsRegistry {
 public:
  void declare(const QString& section, const Setting<bool>& setting);
  // Future types add one declare()/value() overload pair here — generic, never section-specific.
  std::vector<TomlDiagnostic> apply(const TomlDocument& doc);   // REQ-F-004/005, called once
  bool value(const QString& section, const Setting<bool>& setting) const;
  std::vector<SettingInfo> settings() const;                     // REQ-F-030 (source per key)
};

// apps/files/settings/general_settings.h
class GeneralSettings {
 public:
  static constexpr const char* kSection = "general";
  static inline const Setting<bool> kRestoreLastLocation{
      "restore_last_location", false, "Reopen the last local folder when started without a folder argument."};
  static void declare(SettingsRegistry& registry);               // one declare() line per setting
  static GeneralSettings read(const SettingsRegistry& registry); // one value() line per setting
  bool restoreLastLocation() const { return restore_last_location_; }
 private:
  bool restore_last_location_ = kRestoreLastLocation.defaultValue;
};

// apps/files/settings/app_settings.h
class AppSettings {
 public:
  static AppSettings load(const QString& configPath, WarningSink& warnings);  // REQ-C-005: called once
  static AppSettings defaults();                                              // tests / no-config path
  const GeneralSettings& general() const { return general_; }
  const std::vector<SettingInfo>& settingInfo() const { return info_; }        // REQ-F-030
 private:
  AppSettings() = default;
  GeneralSettings general_;
  std::vector<SettingInfo> info_;
};
// Adding a section = new <name>_settings.{h,cpp} with declare()/read(), plus one member,
// one accessor and one declare()/read() line in app_settings — the facade is the single
// registration point; the registry itself never changes (REQ-F-032).

// apps/files/warning_sink.h
class WarningSink {
 public:
  virtual ~WarningSink() = default;
  virtual void warn(const QString& message) = 0;
};
class StderrWarningSink : public WarningSink {
 public:
  void warn(const QString& message) override;  // QTextStream(stderr) << message << '\n' (§7.1)
};

// apps/files/location_classifier.h
struct MountInfo {
  QString fsType;
  QString device;      // "/dev/sdz1"; empty if pseudo/unknown
  QString mountRoot;
  bool valid = false;  // false => unresolvable (REQ-F-026)
};
class LocationClassifier {
 public:
  enum class Classification { Local, Network, Removable };
  virtual ~LocationClassifier() = default;
  virtual Classification classify(const QString& path) const = 0;
};
class RealLocationClassifier : public LocationClassifier {
 public:
  explicit RealLocationClassifier(QString sysfsRoot = QStringLiteral("/sys"));
  Classification classify(const QString& path) const override;  // QStorageInfo + sysfs lookup
};
// REQ-F-022..026 acceptance criteria feed this directly with injected MountInfo + a fake sysfs
// root — no QObject, no filesystem beyond what's passed in.
LocationClassifier::Classification classifyMount(const MountInfo& mount, const QString& sysfsRoot);

// apps/files/restore_outcome.h
enum class RestoreOutcome { Ok, DoesNotExist, NotDirectory, NotReadable, NotLocal };
QString restoreOutcomeReason(RestoreOutcome outcome);  // the exact REQ-F-014 strings, tr()'d

// apps/files/state/state_store.h
struct StateLoadResult {
  std::optional<QString> lastLocation;  // empty => "no stored location" (REQ-F-008)
};
class StateStore {
 public:
  StateStore(QString stateFilePath, QString stateDirPath);
  StateLoadResult load(WarningSink& warnings) const;
  bool save(const QString& lastLocationPath, WarningSink& warnings) const;
};

// apps/files/state/last_location_tracker.h
class LastLocationTracker {
 public:
  void recordLoad(const QString& path, LocationClassifier::Classification classification);
  bool hasCandidate() const;
  QString candidate() const;
};

// apps/files/initial_directory.h (additions; resolveInitialDirectory() unchanged)
struct StartupPlan {
  std::optional<ResolvedDirectory> resolved;  // set => no async step needed
  QString pendingCandidatePath;               // set (resolved empty) => needs off-thread validation
};
StartupPlan planStartup(const QStringList& arguments, bool restoreEnabled,
                        const std::optional<QString>& storedLocation);

// apps/files/directory_model.h (additions)
void validateForRestore(const QString& path);   // worker-thread stat + classify, REQ-F-016
signals:
  void loadSucceeded(const QString& path, LocationClassifier::Classification classification);
  void restoreValidated(const QString& path, RestoreOutcome outcome);

// apps/files/directory_controller.h (additions)
void configureRestore(bool enabled);            // called once from main(), not Q_INVOKABLE (REQ-C-006)
void openRestoreCandidate(const QString& path); // posts validateForRestore(); result handled internally
```

---

## 4. Data flow

### 4.1 Startup (config → state → resolution → open)

| Step | Thread | Detail |
|---|---|---|
| 1. Parse CLI args | GUI (main) | Unchanged. |
| 2. `AppSettings::load(XdgPaths::configFilePath(), warnings)` | GUI (main) | Local-disk-only tiny-file read; never touches a mount that could hang (REQ-C-005). |
| 3. `StateStore(...).load(warnings)` | GUI (main) | Also local-disk-only (XDG_STATE_HOME is never a stored *target* path); returns `optional<QString>` or empty. |
| 4. `planStartup(args, restoreEnabled, stateResult.lastLocation)` | GUI (main) | Pure, synchronous, no filesystem access beyond what's already in hand. CLI-arg case and "restore disabled"/"no stored location" cases produce a fully `resolved` plan here — see §4.1.1. |
| 5. Construct `DirectoryController`; `configureRestore(restoreEnabled)` | GUI | `DirectoryModel`'s worker thread starts here (existing behavior, unchanged). |
| 6. `QTimer::singleShot(0, ...)` fires | GUI | Same deferral reason as today (QML bindings must be connected first). Branches on the plan: |
| 6a. Plan resolved | GUI | `controller.open(path, reason)` — identical to today. |
| 6b. Plan has a pending candidate | GUI → worker | `controller` forwards to `model_.validateForRestore(path)`, which posts to the worker thread (§4.1.2). |
| 7. (6b only) worker validates + classifies | **worker** | `::stat`/`access()` existence/dir/readable checks, then `classifier_->classify(path)` — REQ-F-016. Result posted back via `QMetaObject::invokeMethod(this, ..., Qt::QueuedConnection)`. |
| 8. (6b only) `restoreValidated` handled by controller | GUI | Controller maps `RestoreOutcome` to path+reason (`Ok` → stored path, empty reason; anything else → home + `restoreOutcomeReason()`) and calls `open()` itself — same code path as 6a from this point on. **Stale guard:** if any `open()` already happened while validation was pending (e.g. the user activated a Place during a slow mount check), the result is discarded so a late restore never yanks the user away. |
| 9. `open()` → `model_.load()` | GUI kicks off; walk runs on worker | Unchanged existing flow. |
| 10. Walk finishes without error, `!diff` | worker | Classifier runs again — this time for **load-time** tracking, not restore validation (§4.1.3) — result posted separately after the final `Batch`. |
| 11. Classification delivery emits `loadSucceeded` for an accepted load | GUI | `DirectoryController` updates `last_location_tracker_` only if `classification == Local` (REQ-F-019/020/026). |

#### 4.1.1 `planStartup()` decision table

```
if (!arguments.isEmpty())              → resolved = resolveInitialDirectory(arguments)      // REQ-F-011, unchanged
else if (!restoreEnabled)              → resolved = {home, "no folder given"}                // REQ-F-015
else if (!storedLocation.has_value())  → resolved = {home, "no stored location"}             // REQ-F-013/014
else                                    → pendingCandidatePath = *storedLocation              // REQ-F-016
```

`storedLocation` is already `nullopt` for every REQ-F-008 case (missing file, corrupt TOML, wrong
version, non-absolute path) — `StateStore::load()` does that filtering synchronously since none of
those checks touch the *candidate path itself*, only the state file.

#### 4.1.2 Restore-time validation (worker thread, `directory_model.cpp`)

```cpp
void DirectoryModel::validateForRestore(const QString& path) {
  if (stopping_) return;
  const auto classifier = classifier_;  // shared_ptr copy, safe to hand to the worker
  QMetaObject::invokeMethod(worker_, [this, path, classifier] {
    RestoreOutcome outcome = RestoreOutcome::Ok;
    struct stat info{};
    if (::stat(QFile::encodeName(path).constData(), &info) != 0) {
      outcome = RestoreOutcome::DoesNotExist;
    } else if (!S_ISDIR(info.st_mode)) {
      outcome = RestoreOutcome::NotDirectory;
    } else if (::access(QFile::encodeName(path).constData(), R_OK | X_OK) != 0) {
      outcome = RestoreOutcome::NotReadable;
    } else if (classifier->classify(path) != LocationClassifier::Classification::Local) {
      outcome = RestoreOutcome::NotLocal;
    }
    QMetaObject::invokeMethod(this, [this, path, outcome] { emit restoreValidated(path, outcome); },
                              Qt::QueuedConnection);
  }, Qt::QueuedConnection);
}
```

No `generation_`/cancellation interaction: this runs once, before any `load()` call exists for this
process, on the same worker `QObject` that `startWalk()` already uses.

#### 4.1.3 Load-time classification (worker thread, inside the existing walk)

`walkDirectory()` reports failures through its batch callback. For a fresh, uncancelled,
error-free load, the worker posts the final batch before running classification. The GUI delivery
marks a per-walk `acceptedLoad` flag before `applyBatch()` emits any signals, only if that final
batch is current and updates are neither suspended nor stopping. Both reads and writes of this
flag happen on the GUI thread.

The subsequent classification delivery emits `loadSucceeded` only for an accepted load. Once
accepted, a load remains eligible across refresh, navigation and shutdown; these operations must
not discard a folder that already loaded successfully. Loads superseded before acceptance remain
excluded. Refreshes never initiate classification. The worker processes loads serially, preserving
tracking order when navigating onward while classification is pending.

### 4.2 Shutdown (worker completion → state save → shutdownFinished)

| Step | Thread | Detail |
|---|---|---|
| 1. `lastWindowClosed` → `DirectoryController::shutdown()` | GUI | Unchanged trigger. |
| 2. `model_.shutdown()`, `preview_.shutdown()`, `tasks_.shutdown()` | GUI → workers | Cancel unfinished work and begin the existing three-worker handshake. |
| 3. Pending accepted-load classification is delivered | worker → GUI | Its queued delivery is posted before the directory thread finishes, so it precedes that worker's shutdown notification. Tracking is allowed during shutdown. |
| 4. `handleWorkerShutdown()` receives the third notification | GUI | Save the latest Local candidate, if restore is enabled, then emit `shutdownFinished`. Saving still uses one synchronous `QSaveFile` write. |
| 5. `shutdownFinished` → `QCoreApplication::quit()` | GUI | Unchanged. |

---

## 5. Config/state file formats

**`config.toml`** (read-only; app never writes this file):

```toml
[general]
restore_last_location = true
```

**`state.toml`** (written only via `QSaveFile::commit()`, directory mode 0700):

```toml
version = 1

[navigation]
last_location = "/home/andrii/Projects"
```

---

## 6. Error handling table

| Condition | Behaviour | REQ |
|---|---|---|
| config.toml missing | Defaults, no output, no file created | REQ-F-002 |
| config.toml unparseable | Whole file ignored, defaults, exactly one stderr warning (path, line, message) | REQ-F-003/006 |
| Key wrong type | That key defaults, one warning naming key + expected type, other keys still applied | REQ-F-004 |
| Unknown key/section | One warning per unknown entry, entry ignored, known keys still applied | REQ-F-005 |
| state.toml missing | Silent "no stored location" | REQ-F-008 |
| state.toml corrupt / wrong version / relative `last_location` | "No stored location" + one stderr warning | REQ-F-008, REQ-C-007 |
| Stored path doesn't exist | Home + "last location does not exist" | REQ-F-013/014 |
| Stored path not a directory | Home + "last location is not a directory" | REQ-F-013/014 |
| Stored path not readable | Home + "last location is not readable" | REQ-F-013/014 |
| Stored path not Local | Home + "last location is not local" | REQ-F-013/014, REQ-F-016 |
| State write fails (I/O, perms, disk full) | One stderr warning, quits normally, no prompt/delay | REQ-F-010 |
| No local folder loaded all session | state.toml untouched | REQ-F-020 |
| restore disabled at close | state.toml never created/modified/deleted | REQ-F-018 |
| Unclassifiable path (no mount info) | Treated as not-Local: not restored, not stored | REQ-F-026 |

All warnings funnel through the single `WarningSink::warn()` channel → stderr (REQ-C-011).

---

## 7. Threading model

- **GUI thread**: all of `main()`'s synchronous config/state reads (§4.1 steps 1-4), `planStartup()`,
  all `DirectoryController` state (including `last_location_tracker_`), the state-save write on
  shutdown, and every `Q_PROPERTY`/`Q_INVOKABLE`.
- **`DirectoryModel`'s existing worker thread** (`worker_`, moved to `thread_`, started in the
  constructor): both `validateForRestore()`'s stat+classify work and the load-time classifier call
  inside `startWalk()`. This is the only thread that ever calls into `LocationClassifier::classify()`
  or touches the stored candidate path's filesystem metadata — REQ-F-016/NF-002 by construction, not
  by convention.
- No new `QThread` is introduced. `TaskManager`'s and `PreviewService`'s worker threads are untouched
  and uninvolved.
- Race safety: `validateForRestore()` is called at most once per process (before the first `load()`);
  the final load batch must pass the existing generation and lifecycle guards before becoming a
  tracking candidate. Later refresh, navigation or shutdown does not invalidate an accepted load.

### 7.1 Warning channel choice

`StderrWarningSink::warn()` uses `QTextStream(stderr) << message << '\n'` — the same mechanism
`main.cpp` already uses for `"Open at most one folder."` (its only existing stderr message). The
codebase has no existing `qWarning()`/`QLoggingCategory` usage in `apps/files/` (confirmed by grep),
so `QTextStream(stderr)` is the established, not merely a new, convention. `WarningSink` is made
abstract specifically so tests can substitute a recording sink instead of parsing captured stderr
text, while production code always gets `StderrWarningSink`.

---

## 8. Testing strategy

All new tests run in **`files-smoke`** (not `files-fsops-smoke`, whose `unshare()`-granted
`CAP_DAC_OVERRIDE` would defeat this feature's chmod-0000/0555 permission-denied fixtures — the
project's existing rule for `files-smoke`'s permission tests applies unchanged here) and not
`files-fsops-smoke`/`files-fsops-window-smoke` at all, since nothing here needs distinct real
filesystems.

Extends `tests/directory_fixtures.h`'s `ScopedXdgDataHome` pattern with two siblings,
`ScopedXdgConfigHome`/`ScopedXdgStateHome` (or a single templated `ScopedEnvVar`), constructed the
same way: save/restore whatever was set, `qputenv` for the guard's lifetime. Because `XdgPaths`
resolution happens once per `DirectoryController`/`AppSettings::load()` call (REQ-C-001/REQ-C-005),
each test sets the env var(s) *before* constructing the object under test — no live-reload seam
needed.

| Test file | Covers | REQs |
|---|---|---|
| `tests/xdg_paths_test.cpp` (new) | unset/empty/relative XDG_CONFIG_HOME/XDG_STATE_HOME fallback | REQ-C-001 |
| `tests/toml_document_test.cpp` (new) | parse success/failure, single diagnostic per bad file, type introspection | REQ-F-003/006/028 |
| `tests/settings_registry_test.cpp` (new) | wrong-type/unknown-key/unknown-section warnings, source tracking, test-only section registers without touching existing sections | REQ-F-004/005/030/032 |
| `tests/app_settings_test.cpp` (new) | missing file, XDG path selection, config.toml never written/mtime unchanged | REQ-F-001/002, REQ-C-003 |
| `tests/location_classifier_test.cpp` (new) | `classifyMount()` with injected `MountInfo` + fake sysfs root: network denylist, removable via sysfs/`/media`/`/run/media`, default-Local (tmpfs/bind/fstab), unresolvable-is-not-Local, real `RealLocationClassifier` against the test's own `QTemporaryDir` | REQ-F-022..027 |
| `tests/state_store_test.cpp` (new) | round-trip save/load, missing/corrupt/wrong-version/relative-path handling, mkdir 0700, `QSaveFile::cancelWriting` leaves prior content intact, read-only-directory write failure (chmod 0555, `runningAsRoot()`-skipped like the existing permission fixture), last-close-wins via two sequential `StateStore` instances | REQ-F-007/008/009/010/017/018/019/020/021, REQ-NF-001, REQ-C-004/007/008 |
| `tests/initial_directory_test.cpp` (extended) | `planStartup()` precedence table (§4.1.1), `restoreOutcomeReason()` exact strings | REQ-F-011..015 |
| `tests/directory_model_test.cpp` (extended) | injected classifier records `QThread::currentThread() != GUI` for both `validateForRestore()` and load-time classification; a 2s-blocking injected classifier plus a 50ms `QTimer` proves the GUI loop stays responsive; `loadSucceeded` fires only on `load()` not `refresh()`; unaccepted superseded loads are excluded; accepted loads survive refresh and navigation | REQ-F-016/019/022, REQ-NF-002 |
| `tests/directory_controller_test.cpp` (extended) | tracker updates only on Local loads; Local A → Network B → close saves A; no local load ever → state untouched; `configureRestore(false)` never touches state.toml; pending accepted-load classification is tracked before saving at completion of the 3-worker handshake | REQ-F-017/018/019/020/021 |

`DirectoryModelTestAccess` (existing `tests/directory_model_test_access.h`) gains one more setter,
`setLocationClassifier(model, classifier)`, matching `beforeOpen`/`failReadAfter`'s existing shape.
A **new** `tests/directory_controller_test_access.h` (`DirectoryControllerTestAccess`, same friend
pattern — none exists today) provides `setWarningSink(controller, sink)` and
`setLocationClassifier(controller, classifier)` (forwarding to the owned model).

---

## 9. Build / packaging changes

- Top-level `CMakeLists.txt`: `find_package(tomlplusplus 3.4 CONFIG REQUIRED)` alongside the
  existing `find_package(Qt6 ...)`/`find_package(HolonightQt ...)` block.
- `apps/files/CMakeLists.txt`: new files added to `files-ui`'s `SOURCES`; `target_link_libraries(files-ui PUBLIC ... tomlplusplus::tomlplusplus)`.
- `packaging/Dockerfile.ci` **already** lists `tomlplusplus` in its `pacman -S` package set (confirmed
  during grounding, before this design was written) — no change needed. `packaging/Dockerfile.runtime-check`
  is `FROM files-ci`, so it inherits the same runtime lib automatically — no change needed either.
- `scripts/check-install.sh`: add one assertion after the existing staged-install checks:
  `readelf -d "$stage/usr/bin/hn-files" | rg 'libtomlplusplus\.so\.3'` (mirrors REQ-C-009's
  `readelf -d` acceptance criterion, run here against the *installed* binary rather than the build tree).

---

## 10. Key decisions with rationale

1. **Restore-time validation reuses `DirectoryModel`'s worker thread rather than spawning a new one
   or using `QtConcurrent`.** See §11 for the full comparison; the short version: the SPEC
   names "the directory worker thread" specifically, that thread already exists and is idle at the
   moment `main()` needs it, and every other async component in this codebase (`DirectoryModel`,
   `PreviewService`, `TaskManager`) already uses the identical "persistent worker `QObject` +
   `QMetaObject::invokeMethod`" idiom — a fourth bespoke thread class for one one-shot startup call
   would be the odd one out.
2. **Load-time classification is a plain queued signal, not folded into `Batch`.** `Batch` is a
   per-flush structure that already carries entries/finished/error; adding a field used exactly once
   per walk (only on the final, non-diff, error-free flush) would mean every other batch carries a
   meaningless default. A second `QMetaObject::invokeMethod` delivers classification after the final batch. A GUI-owned
   acceptance flag connects these two deliveries without delaying listing updates.
3. **State save is synchronous on the GUI thread, not a 4th worker.** It's a single small local-disk
   `QSaveFile` write of a value already computed (the tracker's candidate never needs stat/classify
   at save time — that already happened at load time). Adding a 4th entry to `workers_finished_`'s
   count-to-3 handshake is unnecessary. Save after that handshake so classification of the last
   displayed folder has reached the tracker. The state directory is the XDG state path, independent
   of the saved navigation path; existing filesystem I/O limitations still apply.
4. **`RestoreOutcome`/`restoreOutcomeReason()` live in their own tiny top-level file, not inside
   `initial_directory.h` or `directory_model.h`.** Both files need the enum; putting it in either
   would make the other depend on a heavier header it doesn't otherwise need (`directory_model.h`
   pulls `QAbstractListModel`/`QThread`; `initial_directory.h` is deliberately minimal).
5. **`WarningSink` lives at `apps/files/` top level, not inside `settings/`.** Both `settings/` and
   `state/` need it; nesting it under one implies false ownership by that one.
6. **`AppSettings`/`StateStore` are not injected into `DirectoryController`'s constructor.** Changing
   `DirectoryController`'s default-constructible signature would ripple into every existing test that
   does `DirectoryController controller;` (directory_controller_test.cpp, directory_controller_file_ops_test.cpp,
   history_navigation_window_test.cpp, places_window_test.cpp, window_cross_filesystem_test.cpp, …).
   Instead `DirectoryController` builds its own `StateStore` member from `XdgPaths` (mirroring how
   `TrashService::xdgDataHome()` already resolves its own XDG variable internally rather than being
   handed one), and `main()` passes only the resolved boolean via `configureRestore()`. Tests that
   need state-file isolation set `XDG_STATE_HOME` before constructing their own controller instance.

---

## 11. Alternatives considered

| Alternative | Rejected because |
|---|---|
| Store `last_location` inside `config.toml` instead of a separate `state.toml` | SPEC explicitly separates config (user intent, read-only) from state (app-observed, machine-written) specifically so a network path visited once doesn't get silently re-opened after reboot if the user later hand-edits config.toml; conflating the two files would also violate REQ-C-003 (config.toml never written). |
| `QSettings` (INI/registry-style) for either file | SPEC requires TOML 1.0.0 specifically (REQ-C-002), typed validation with per-key warnings (REQ-F-004/005), and source tracking (REQ-F-030) — `QSettings` gives none of these and has no schema/diagnostics concept. |
| One `Q_PROPERTY`-per-setting "god object" (e.g. `AppSettings : public QObject` with `Q_PROPERTY(bool restoreLastLocation ...)`) | Violates REQ-C-006 (no QML exposure) by construction once `QML_ELEMENT`ed, and violates REQ-F-032 (adding a setting to one section touching a shared object) the moment a second section is added — every new property is a diff to the same class. |
| toml++ used directly wherever a setting is read (header-only, no adapter) | Violates REQ-F-028 directly; also means every consumer TU pays toml++'s header-parsing cost and any future replacement of the underlying TOML library touches every call site instead of one `.cpp`. |
| Synchronous restore-time classification on the GUI thread (today's `main()` shape, extended in place) | Directly violates REQ-F-016/REQ-NF-002 — a hung network mount named in a stale `state.toml` would freeze the entire application before its first window paints. |
| `QtConcurrent::run()` + `QFutureWatcher` for restore-time validation | Would introduce a new linked module (`Qt6::Concurrent`) not currently used anywhere in this codebase; a `QFutureWatcher` also doesn't participate in `DirectoryController`'s existing `workers_finished_`-counted shutdown handshake, so cancellation-on-quick-quit would need its own bespoke handling. Rejected in favor of reusing infrastructure that already has both properties. |
| A brand-new dedicated `QThread`-owning class (e.g. `RestoreValidator`) for restore-time validation only | Would be a 4th instance of the exact "worker `QObject` + `QThread` + `QMetaObject::invokeMethod`" pattern in the same process for a task that runs exactly once per process lifetime; `DirectoryModel`'s worker is already running and idle at the moment it's needed. |

---

## 12. Known risks

| Risk | Mitigation |
|---|---|
| `.clang-tidy`'s `HeaderFilterRegex: 'src/.*\.h$'` doesn't match `apps/files/*.h` (project memory: pre-existing gap) — new headers under `apps/files/settings/` and `apps/files/state/` inherit the same exemption from header-only naming/other checks. | No action needed for this feature specifically; documented so it isn't mistaken for new-code oversight during review, same as `navigation-history`'s DESIGN.md noted for `jump_list.h`. |
| A future settings UI (explicitly out of scope, but designed for) needs to merge an app-owned settings file *beneath* `config.toml` without rewriting `AppSettings::load()`. | `SettingsRegistry`'s per-value `SettingSource` tracking (REQ-F-030) is the seam: a future loader would apply the app-owned file into the registry first, then `config.toml` over it, and `SettingInfo::source` would need a third `AppOwnedFile` value — additive to the enum, no other change. Not built now (spec is explicit this cycle has no such file). |
| `RealLocationClassifier`'s `/sys/block/<dev>/removable` lookup requires mapping a mount's device path to its sysfs block name (e.g. `/dev/sdz1` → `sdz`), which for partitions vs. whole disks vs. device-mapper/LUKS/loop devices is genuinely fiddly. | Scoped narrowly: only the exact `/sys/block/<dev>/removable` check plus the `/run/media/`+`/media/` mount-root shortcut are required by REQ-F-024; anything more exotic (LVM, loop, bind mounts of removable media) falls through to "Local" or "Network" by the ordinary rules, which REQ-F-025/026 already accept as correct default behavior. |
| Two instances of the app both classify+save around the same moment (REQ-F-021's "last-close-wins", REQ-C-008's "no locking"). | Explicitly accepted by the SPEC as-is; `QSaveFile::commit()`'s rename is atomic per-write, so the worst case is exactly what REQ-F-021 describes (final commit wins), never a torn/corrupt file. |
| A restore candidate or loaded folder on a genuinely hung mount blocks the worker inside `classify()`/`stat()`. The GUI stays responsive (REQ-NF-002), but `DirectoryModel`'s shutdown ends in `thread_.wait()`, so closing the app while that call is stuck would hang exit. | Pre-existing for walks of hung mounts (the walk's own `stat`/`readdir` block identically); this feature does not make it worse. Documented, not solved here — a future MountService can pre-filter known-offline network mounts before any syscall. |
| `readEntry()`/`walkDirectory()` already run at a fairly high call frequency on `refresh()` (every filesystem-watcher tick); classification is deliberately excluded from `refresh()` (`!diff` guard) to avoid re-running `QStorageInfo`/sysfs lookups on every watcher-driven refresh of the same directory. | If a future requirement needs classification to react to a directory being remounted mid-session, this guard would need revisiting — out of scope today (REQ scope: classification is a load-time-and-restore-time concept only). |

---

## 13. Traceability

| REQ | Component(s) |
|---|---|
| REQ-F-001 | `XdgPaths::configFilePath`, `AppSettings::load` |
| REQ-F-002 | `TomlDocument::parseFile`, `AppSettings::load` |
| REQ-F-003 | `TomlDocument::parseFile` |
| REQ-F-004 | `SettingsRegistry::apply` |
| REQ-F-005 | `SettingsRegistry::apply` |
| REQ-F-006 | `TomlDocument::parseFile` |
| REQ-F-007 | `XdgPaths::stateFilePath`, `StateStore::load` |
| REQ-F-008 | `StateStore::load` |
| REQ-F-009 | `StateStore::save` |
| REQ-F-010 | `StateStore::save`, `WarningSink` |
| REQ-F-011 | `resolveInitialDirectory` (unchanged), `planStartup` |
| REQ-F-012 | `planStartup`, `DirectoryModel::validateForRestore` |
| REQ-F-013 | `planStartup`, `DirectoryModel::validateForRestore`, `restoreOutcomeReason` |
| REQ-F-014 | `restoreOutcomeReason`, `planStartup` ("no stored location" case) |
| REQ-F-015 | `planStartup` |
| REQ-F-016 | `DirectoryModel::validateForRestore`, load-time classification hook in `startWalk` |
| REQ-F-017 | `DirectoryController::shutdown` |
| REQ-F-018 | `DirectoryController::configureRestore`, `shutdown` |
| REQ-F-019 | `DirectoryModel::loadSucceeded`, `LastLocationTracker::recordLoad` |
| REQ-F-020 | `LastLocationTracker`, `DirectoryController::shutdown` |
| REQ-F-021 | `StateStore::save` (atomic commit, no locking) |
| REQ-F-022 | `LocationClassifier`, `classifyMount` |
| REQ-F-023 | `classifyMount` |
| REQ-F-024 | `classifyMount`, `RealLocationClassifier` |
| REQ-F-025 | `classifyMount` |
| REQ-F-026 | `classifyMount`, `LastLocationTracker::recordLoad` |
| REQ-F-027 | `DirectoryModelTestAccess::setLocationClassifier` |
| REQ-F-028 | `toml_document.cpp` (sole toml++ include) |
| REQ-F-029 | `Setting<T>`, `GeneralSettings::kRestoreLastLocation` |
| REQ-F-030 | `SettingsRegistry::settings()`, `AppSettings::settingInfo()` |
| REQ-F-031 | `AppSettings` (const accessors only) |
| REQ-F-032 | `SettingsRegistry` (no section-specific code); sections register via `AppSettings` only |
| REQ-NF-001 | `StateStore::save` (`QSaveFile` semantics) |
| REQ-NF-002 | `DirectoryModel` worker-thread classification |
| REQ-C-001 | `XdgPaths` |
| REQ-C-002 | `TomlDocument` (TOML-only) |
| REQ-C-003 | `AppSettings::load` (never writes) |
| REQ-C-004 | `StateStore::save` (`QSaveFile` only) |
| REQ-C-005 | `main()` (single load, before resolution) |
| REQ-C-006 | No `QML_ELEMENT`/`Q_PROPERTY`/`Q_INVOKABLE` in `settings/`/`state/` |
| REQ-C-007 | `StateStore::load` (absolute-path check) |
| REQ-C-008 | `StateStore` (no locking) |
| REQ-C-009 | `CMakeLists.txt` (`find_package(tomlplusplus 3.4 CONFIG REQUIRED)`) |
| REQ-C-010 | `packaging/Dockerfile.ci` (already present), `scripts/check-install.sh` |
| REQ-C-011 | `WarningSink`/`StderrWarningSink` |

---

**End of Design**
