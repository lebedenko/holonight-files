# Places Sidebar Sources — Architecture Design

## Status

**Implemented; review remediation verified automatically.** Implements
`docs/sdd/places-sources/SPEC.md`; verification and remaining acceptance are tracked
in [VERIFICATION.md](VERIFICATION.md). Earlier approval history is not recorded here.

## 1. Overview & Component Diagram

Today `PlacesModel` (`apps/files/places_model.{h,cpp}`) is a synchronous, `QStandardPaths`-driven
snapshot built entirely inside its constructor: six standard locations plus a hardcoded
`~/Projects` check, no async work, no bookmarks, three roles (`name`, `path`, `iconName`).
`DirectoryController` owns it as a plain value member (`PlacesModel places_;`, `directory_controller.h:147`)
and exposes it read-only as `Q_PROPERTY(PlacesModel* places READ places CONSTANT)`. `PlacesPanel.qml`
binds `ListView.model: root.controller.places` and its `PlaceRow` delegate calls
`root.controller.open(row.path)` directly on activation (`PlacesPanel.qml:69`).

This redesign keeps that shape (model still owned as a value member, still constructed by the
controller, still consumed the same way from QML) but changes what `PlacesModel` is built from and
adds one narrow, model-owned async subsystem for availability checking. Three new pure/adapter
units do the parsing; `PlacesModel` composes them, applies dedup, and owns the async check
lifecycle; `DirectoryController` adds one new activation entry point and two lines of signal wiring
for the one behavior that needs controller cooperation (the bookmark-unavailable message and
navigation staleness).

```
                              ┌─────────────────────────┐
                              │     DirectoryController  │
                              │  (places_ member; ctor   │
                              │   builds it, as it does  │
                              │   state_store_ today)    │
                              └─────────────┬────────────┘
                                            │ open(path) on Home/XDG activate (unchanged)
                                            │ activateBookmark(row) on Bookmark activate (new)
                                            │ handleBookmarkRecheckResolved() slot (new)
                                            ▼
                              ┌─────────────────────────┐
                              │        PlacesModel        │◄── QML_ELEMENT, uncreatable, roles:
                              │  (QAbstractListModel)     │    name/path/iconName/origin/status/
                              │  owns: Place rows,        │    startsBookmarks
                              │  checker_, next_id_       │
                              └───┬──────────┬───────────┘
                 constructs/calls │          │ dispatches (detached std::thread per check)
             ┌────────────────────┘          └─────────────────────────┐
             ▼                                                         ▼
  ┌───────────────────┐   ┌────────────────────┐         ┌─────────────────────────────┐
  │  UserDirsParser    │   │   BookmarkStore     │         │  PlaceAvailabilityChecker    │
  │  (pure functions)  │   │  (via TomlDocument)  │         │  (injectable interface;      │
  │  apps/files/places/ │   │  apps/files/places/   │         │   Stat... = real impl)       │
  └─────────┬──────────┘   └──────────┬──────────┘         └───────────────┬───────────────┘
            │ reads                   │ reads                              │ off-GUI-thread stat()
            ▼                         ▼                                    │
  ${XDG_CONFIG_HOME}/user-dirs.dirs   $XDG_DATA_HOME/holonight/holonight-files/       ▼
  (XdgPaths::userDirsFilePath())      places.toml (XdgPaths::placesFilePath())
                                       via settings/toml_document.{h,cpp}
                                       (only unit that includes toml++;
                                       gains [[array-of-tables]] support)
```

Ownership: `PlacesModel` owns its `Place` rows, the injected `checker_` (`shared_ptr`), and the
dispatch bookkeeping (`next_id_`, per-row `recheck_generation`). `UserDirsParser` and `BookmarkStore`
are stateless — called once from `PlacesModel`'s constructor, own nothing after returning. Detached
check threads own nothing of the model; they hold a path string, the `shared_ptr<Checker>` and a
`shared_ptr<DeliveryGuard>` (a mutex plus a `PlacesModel*` that the model's destructor nulls). A
thread posts its result only while holding that mutex and only if the pointer is still non-null, so
it can never touch a destroyed model (see §3.1).

## 2. Components and Interfaces

### 2.1 `UserDirsParser` (new, pure) — `apps/files/places/user_dirs_parser.h`

```cpp
#pragma once

#include <QString>

#include <vector>

// Pure parser for ${XDG_CONFIG_HOME:-~/.config}/user-dirs.dirs (SPEC.md REQ-F-002..006, REQ-F-010).
// No QObject, no file I/O in the parse() overload — file reading is a one-line wrapper so tests can
// feed literal strings without touching disk.
namespace UserDirsParser {

// One of the 9 known XDG_*_DIR keys, in SPEC.md's required display order (REQ-F-005).
enum class Key { Desktop, Documents, Downloads, Pictures, Music, Videos, Projects, Templates, Public };

struct Entry {
  Key key = Key::Desktop;
  QString path;  // already unescaped, $HOME-substituted, QDir::cleanPath()'d
};

// Parses user-dirs.dirs text. home is QDir::homePath() (injected so tests don't depend on the
// real user's home). Malformed lines, unknown keys, and entries whose resolved path equals home
// are silently dropped (REQ-C-007). Result order follows Key's declaration order, not file order
// (REQ-F-005), keeping only keys actually present in the file.
std::vector<Entry> parse(const QString& text, const QString& home);

// Returns {} (parse() of empty text) if the file does not exist; this is not an error (REQ-F-010).
std::vector<Entry> parseFile(const QString& path, const QString& home);

// Translated label + theme icon name for a key (REQ-F-006), e.g. Key::Documents ->
// ("Documents", "folder-documents"). Translation context is "PlacesModel", matching the existing
// places_model_test.cpp PlaceTranslator convention.
QString label(Key key);
QString iconName(Key key);

}  // namespace UserDirsParser
```

`label()`/`iconName()` are looked up from a small `static constexpr` table keyed by `Key`, matching
the fixed order/label/icon lists in REQ-F-005/006 exactly (Desktop→`user-desktop`,
Documents→`folder-documents`, Downloads→`folder-download`, Pictures→`folder-pictures`,
Music→`folder-music`, Videos→`folder-videos`, Projects→`folder-development`,
Templates→`folder-templates`, Public→`folder-publicshare`). `label()` uses
`QCoreApplication::translate("PlacesModel", ...)` (a free function, not a `Q_OBJECT` member) so the
existing `PlaceTranslator` test fixture (`tests/places_model_test.cpp:11-17`, matches on context
`"PlacesModel"`) keeps working unmodified.

### 2.2 `BookmarkStore` (new) — `apps/files/places/bookmark_store.h`

```cpp
#pragma once

#include "warning_sink.h"

#include <QString>

#include <vector>

// Reads $XDG_DATA_HOME/holonight/holonight-files/places.toml through the TomlDocument adapter only
// (SPEC.md REQ-C-003). One warning per distinct problem, all via WarningSink (REQ-C-007/008).
namespace BookmarkStore {

struct Bookmark {
  QString name;  // resolved: explicit "name" or the path's directory base name (REQ-F-008)
  QString path;  // absolute, cleaned with QDir::cleanPath (REQ-F-009)
};

// Missing file -> {} silently (REQ-F-011). Unparseable TOML, missing/mismatched version -> {} plus
// exactly one warning (REQ-F-012/013). Otherwise returns the valid entries, in file order, having
// already skipped invalid entries and warned once per skip/unknown-key (REQ-F-014/015). Does not
// deduplicate against Home/XDG/earlier bookmarks — that is PlacesModel's job (§5).
std::vector<Bookmark> read(const QString& path, WarningSink& warnings);

}  // namespace BookmarkStore
```

`read()` is implemented entirely in terms of `TomlDocument` (§2.2.1) — it never includes
`<toml++/toml.h>` itself, preserving REQ-C-003 and the `TomlLibraryIsIncludedOnlyByTheAdapter` test.

#### 2.2.1 `TomlDocument` addition: `[[array-of-tables]]` support

`TomlDocument` today (`apps/files/settings/toml_document.h`) has no way to read a TOML array of
tables — `value()`/`keys()`/`sections()` only see single tables. `places.toml`'s `[[bookmarks]]`
entries need this, and `TomlDocument` is the *only* unit allowed to include `toml++`, so the
adapter's surface grows by four small read-only accessors, mirroring the existing
`value()`/`keys()`/`sectionLine()` shape:

```cpp
// Added to class TomlDocument (toml_document.h):

// Size of the top-level [[key]] array of tables; 0 if key is absent or not an array of tables.
int arrayOfTablesSize(const QString& key) const;
// field's value inside the array-of-tables key's index'th entry (0-based); Missing if absent.
TomlValue arrayOfTablesValue(const QString& key, int index, const QString& field) const;
// That entry's own keys, in document order (for unknown-key detection, REQ-F-015).
std::vector<QString> arrayOfTablesKeys(const QString& key, int index) const;
// 1-based line where that entry's table begins.
int arrayOfTablesLine(const QString& key, int index) const;
```

Implementation sketch (`toml_document.cpp`), following the existing `value()`/`keys()` pattern of
walking `impl_->root`:

```cpp
int TomlDocument::arrayOfTablesSize(const QString& key) const {
  if (!impl_) return 0;
  const auto* array = impl_->root[utf8View(key.toUtf8())].as_array();
  return (array != nullptr && array->is_array_of_tables()) ? static_cast<int>(array->size()) : 0;
}
```

`arrayOfTablesValue`/`arrayOfTablesKeys`/`arrayOfTablesLine` index `array->get(index)->as_table()`
and reuse the existing private `toValue()`/`keysInSourceOrder()` helpers unchanged. No other file
gains a `toml++` include; `apps/files/CMakeLists.txt`'s `target_link_libraries(files-ui PRIVATE
tomlplusplus::tomlplusplus)` line is unchanged (still linked, not header-only, per the existing
comment on that line).

### 2.3 `XdgPaths` additions — `apps/files/settings/xdg_paths.h`

```cpp
namespace XdgPaths {
QString configFilePath();  // existing: $XDG_CONFIG_HOME/holonight-files/config.toml
QString stateDirPath();    // existing
QString stateFilePath();   // existing
QString userDirsFilePath();  // new: $XDG_CONFIG_HOME/user-dirs.dirs (NOT under holonight-files/ —
                              // this is the shared xdg-user-dirs file other apps also read)
QString dataDirPath();       // new: $XDG_DATA_HOME/holonight/holonight-files
QString placesFilePath();    // new: dataDirPath()/places.toml
}  // namespace XdgPaths
```

Implementation reuses the existing private `baseDirectory(variable, homeRelativeFallback)` helper
in `xdg_paths.cpp` unchanged: `userDirsFilePath()` = `baseDirectory("XDG_CONFIG_HOME", ".config") +
"/user-dirs.dirs"` (no `holonight-files` segment — `user-dirs.dirs` is a shared, non-app-specific
file written by `xdg-user-dirs-update`); `dataDirPath()` = `baseDirectory("XDG_DATA_HOME",
".local/share") + "/holonight/holonight-files"` (an extra `holonight/` segment, unlike
`configFilePath()`/`stateDirPath()`, reserving a shared data-home namespace for future
cross-app data sharing — amended post-cycle, see T-015); `placesFilePath()` =
`dataDirPath() + "/places.toml"`. Same unset/empty/non-absolute → `$HOME` fallback rule as the
existing functions (REQ-C-001).

**Note on `trash_service.cpp`'s own `xdgDataHome()`** (`apps/files/trash_service.cpp:23-26`): it
duplicates `XDG_DATA_HOME` resolution with a slightly different fallback rule (checks only
`isEmpty()`, not `QDir::isAbsolutePath()`). This design deliberately does **not** touch
`trash_service.cpp` — consolidating it onto `XdgPaths::dataDirPath()` is a real cleanup opportunity
but changes trash-directory selection behavior for the edge case of a *relative* `XDG_DATA_HOME`,
which is out of scope for a Places-sidebar change and risks the extensive `trash_service_test.cpp`/
`cross_filesystem_test.cpp` suite for no Places benefit. Left as a follow-up, noted under §8 Risks.

### 2.4 `PlaceAvailabilityChecker` (new, injectable) — `apps/files/places/place_availability_checker.h`

```cpp
#pragma once

#include <QString>

// Injectable directory-availability check (SPEC.md REQ-F-018/020/021, REQ-NF-001/002), mirroring
// the existing LocationClassifier seam (location_classifier.h) used by DirectoryModel::
// validateForRestore. Always called off the GUI thread by PlacesModel (§3).
class PlaceAvailabilityChecker {
 public:
  virtual ~PlaceAvailabilityChecker() = default;
  PlaceAvailabilityChecker() = default;
  PlaceAvailabilityChecker(const PlaceAvailabilityChecker&) = delete;
  PlaceAvailabilityChecker& operator=(const PlaceAvailabilityChecker&) = delete;
  PlaceAvailabilityChecker(PlaceAvailabilityChecker&&) = delete;
  PlaceAvailabilityChecker& operator=(PlaceAvailabilityChecker&&) = delete;

  // True iff path exists, is a directory, and is readable (stat() + access(), or QFileInfo
  // equivalent). May block indefinitely (dead NFS/sshfs) — callers must never invoke this on the
  // GUI thread (REQ-NF-001/002).
  [[nodiscard]] virtual bool isAvailable(const QString& path) const = 0;
};

class StatPlaceAvailabilityChecker final : public PlaceAvailabilityChecker {
 public:
  [[nodiscard]] bool isAvailable(const QString& path) const override;
};
```

Test fixture `FakePlaceAvailabilityChecker` is added to `tests/directory_fixtures.h`, structurally
identical to the existing `FakeLocationClassifier` there (`tests/directory_fixtures.h:42-70`):
records `QThread::currentThread()` and the queried path per call (for REQ-NF-001's "ran on a thread
other than the GUI thread" assertion), takes per-path result overrides, and optionally blocks on a
`QSemaphore` the test releases explicitly (for REQ-NF-002/REQ-F-021's hung-check tests — the same
shape as `GatedLocationClassifier`).

### 2.5 `PlacesModel` — `apps/files/places_model.h` (redesigned)

```cpp
#pragma once

#include "place_availability_checker.h"
#include "warning_sink.h"

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

#include <memory>

// Home + XDG user directories + user bookmarks, deduplicated, with async availability status
// (SPEC.md places-sources). Constructed once by DirectoryController, exactly like today.
class PlacesModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the application")
 public:
  enum class Origin { Home, XdgUserDirectory, Bookmark };
  Q_ENUM(Origin)
  enum class Status { Checking, Available, Unavailable };
  Q_ENUM(Status)
  enum Role {
    NameRole = Qt::UserRole + 1,
    PathRole,
    IconNameRole,
    OriginRole,
    StatusRole,
    // True only on the first Bookmark row, when at least one non-bookmark row precedes it
    // (REQ-F-028); consumed by PlacesPanel.qml's delegate to add one extra spacing gap.
    StartsBookmarksRole,
  };

  explicit PlacesModel(QObject* parent = nullptr);
  // Test seam: overrides real XDG env resolution and the real stat()-based checker
  // (REQ-NF-001/002 tests inject a recording/blocking checker here).
  PlacesModel(QString homePath, QString userDirsFilePath, QString placesFilePath,
              std::shared_ptr<PlaceAvailabilityChecker> checker, std::shared_ptr<WarningSink> warnings,
              QObject* parent = nullptr);
  ~PlacesModel() override;

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  // Re-checks row's path off the GUI thread, regardless of its current status (REQ-F-022). Returns
  // the row's stable place id, or 0 if row is out of range or not a Bookmark row. Row indices are
  // NOT stable while startup checks are still removing XDG rows, so everything after dispatch is
  // keyed by id.
  quint64 recheckBookmark(int row);

 signals:
  // Fired once the freshest in-flight recheck for that place resolves; its StatusRole is already
  // updated (one dataChanged, REQ-NF-003) before this signal is emitted. A recheck superseded by a
  // newer recheckBookmark() call on the same place never reaches here.
  void bookmarkRecheckResolved(quint64 placeId, QString path, bool available);

 private:
  friend struct PlacesModelTestAccess;
  struct Place {
    quint64 id = 0;              // stable across the model's lifetime; rows are never reordered
    QString name;
    QString path;
    QString iconName;
    Origin origin = Origin::Home;
    Status status = Status::Checking;
    bool starts_bookmarks = false;
    quint64 recheck_generation = 0;  // guards stale recheckBookmark() deliveries (REQ-NF-003)
  };
  void buildPlaces(const QString& homePath, const QString& userDirsFilePath, const QString& placesFilePath,
                    WarningSink& warnings);
  void dispatchStartupChecks();
  void deliverStartupResult(quint64 id, bool available);   // XDG: remove row if false; Home: always Available
  void deliverRecheckResult(quint64 id, quint64 generation, bool available);
  int rowForId(quint64 id) const;
  // Shared with every detached check thread; ~PlacesModel() locks it and nulls `model` (§3.1).
  struct DeliveryGuard {
    std::mutex mutex;
    PlacesModel* model = nullptr;
  };
  void dispatchCheck(quint64 id, quint64 generation, const QString& path);
  QList<Place> places_;
  std::shared_ptr<DeliveryGuard> guard_;
  std::shared_ptr<PlaceAvailabilityChecker> checker_;
  quint64 next_id_ = 1;
};
```

`data()`/`roleNames()` extend today's implementation with the three new roles; `OriginRole`/
`StatusRole` return the enum as `QVariant::fromValue`, consumed from QML as an int comparable to
`PlacesModel.Bookmark`/`PlacesModel.Unavailable` (identical to how `PlacesPanel.qml` already
compares `root.controller.vim.currentMode === VimModeController.Normal`).

### 2.6 `DirectoryController` changes

```cpp
// directory_controller.h additions
 public:
  // Bookmark activation entry point (SPEC.md REQ-F-022): PlaceRow calls this instead of open()
  // for origin === Bookmark rows. Home/XDG rows keep calling open(path) directly (unchanged —
  // both are guaranteed available whenever shown, REQ-C-005/REQ-C-006).
  Q_INVOKABLE void activateBookmark(int placesRow);

 private:
  void handleBookmarkRecheckResolved(quint64 placeId, const QString& path, bool available);
  // navigation_serial_ snapshot at the moment each place's currently-outstanding recheck was
  // dispatched; consulted on resolution to drop a stale navigate/message side effect if a real
  // navigation happened in between (see DESIGN.md §3). Keyed by place id, not row: a bookmark's
  // row index shifts when an earlier XDG row is removed by a late startup check.
  QHash<quint64, quint64> bookmark_dispatch_navigation_serial_;
```

```cpp
// directory_controller.cpp — constructor wiring, alongside the existing model_/preview_/tasks_ connects
connect(&places_, &PlacesModel::bookmarkRecheckResolved, this, &DirectoryController::handleBookmarkRecheckResolved);

void DirectoryController::activateBookmark(int placesRow) {
  if (vim_.currentMode() != VimModeController::Mode::Normal || tasks_.hasPrompt() || quick_look_open_) {
    return;
  }
  if (const auto id = places_.recheckBookmark(placesRow); id != 0) {
    bookmark_dispatch_navigation_serial_[id] = navigation_serial_;
  }
}
void DirectoryController::handleBookmarkRecheckResolved(quint64 placeId, const QString& path, bool available) {
  const auto dispatchSerial = bookmark_dispatch_navigation_serial_.take(placeId);
  if (dispatchSerial != navigation_serial_ || vim_.currentMode() != VimModeController::Mode::Normal ||
      tasks_.hasPrompt() || quick_look_open_) {
    return;  // Navigation or an interaction guard intervened while the check was pending.
  }
  if (available) {
    open(path);
  } else {
    status_message_ = tr("Location is currently unavailable");
    emit changed();
  }
}
```

This reuses the *exact* staleness idiom already in this file for an analogous problem —
`openRestoreCandidate()` / `handleRestoreValidated()` / `restore_serial_ != navigation_serial_`
(`directory_controller.cpp:803-817`) — rather than inventing a new one. `status_message_` is the
same field `ModeStatusBar.qml:26` already renders (`root.controller.statusMessage`), i.e. exactly
"the controller's existing error/status path" the spec asks for; no new QML-facing property. The
`available` branch calling `open(path)` unwrapped satisfies REQ-C-010 ("preserving the controller's
error handling... without wrapping") — the indirection through a resolved-signal only changes *when*
`open()` fires, not what it does.

`DirectoryController`'s constructor keeps building `places_` as a plain value member with no
arguments (`PlacesModel places_;` — unchanged from today), exactly as `state_store_` is built from
`XdgPaths` inside the constructor today (`directory_controller.cpp:29`). `DirectoryController
controller;` in every existing test therefore keeps compiling and running unchanged: it uses the
real `StatPlaceAvailabilityChecker` and real `XdgPaths` resolution. See §8 for why this doesn't hang
or spam warnings against a real `$HOME`.

### 2.7 `PlacesPanel.qml` changes

- **`ListView.model`** stays `root.controller.places` — no change to the binding itself, since
  `PlacesModel` is still the direct model (not proxied; see §6 for why no `QSortFilterProxyModel`).
- **`PlaceRow.activate()`** branches on origin:
  ```qml
  function activate(): void {
      if (!root.activationEnabled)
          return;
      if (row.origin === PlacesModel.Bookmark)
          root.controller.activateBookmark(row.index);
      else
          root.controller.open(row.path);
  }
  ```
  (`row.index` is the `ListView` delegate's model row index, already a `required property int` on
  `PlaceRow` today — `PlacesPanel.qml:60`.)
- **Status rendering**: the two-icon leading slot keeps its existing `themeIcon`/fallback-icon
  pattern (`PlacesPanel.qml:85-99`) untouched for the *place* icon. A new small trailing badge is
  added via `HnListDelegate`'s existing (currently unused) `trailingContent` slot, visible only when
  `row.status === PlacesModel.Unavailable`, using the identical two-icon theme/fallback technique:
  theme candidate `"dialog-warning"` with a new bundled `qrc:/qt/qml/HolonightFiles/icons/warning-fallback.svg`
  fallback (mirrors `icons/folder-fallback.svg`, added to `apps/files/CMakeLists.txt`'s `RESOURCES`
  list next to it).
- **Muted text (REQ-F-023)**: `HnListDelegate`'s title label color is internal to the
  `holonight-qt` component (`root.enabled ? textPrimary : textDisabled`, see
  `holonight-qt/qml/controls/HnListDelegate.qml:50`) and not independently overridable, and setting
  `enabled: false` would also disable focus/activation — forbidden by REQ-F-023 ("remain focusable
  ... and activatable"). Instead the whole delegate's `opacity` is reduced:
  `button.opacity: row.status === PlacesModel.Unavailable ? 0.5 : 1.0` — the same 0.5 value
  holonight-qt's own controls use for their disabled look (`Button.qml`, `TextField.qml`). This mutes
  icon and text together without touching focus/enabled state, and needs no change to the
  `holonight-qt` package. SPEC.md REQ-F-023's acceptance criterion is worded accordingly.
- **Accessible description**: `HnListDelegate` binds `Accessible.description: root.subtitle`
  internally (`holonight-qt/qml/controls/HnListDelegate.qml:30`); `PlacesPanel.qml` never sets
  `subtitle` (keeping delegates single-line/compact per REQ-F-031, since a non-empty `subtitle`
  would render a visible second line and change row height). Instead the `button:` instantiation in
  `PlaceRow` overrides the attached property directly at the instantiation site (an outer binding on
  a component's own default-bound property takes precedence over the component's internal one — the
  same mechanism QML uses for any overridden default binding):
  ```qml
  HnListDelegate {
      id: button
      // ...
      Accessible.description: row.status === PlacesModel.Unavailable ? qsTr("unavailable") : ""
  }
  ```
- **Spacing gap (REQ-F-028)**: `PlaceRow`'s `height` grows by one compact spacing token when
  `row.startsBookmarks` is true, with the visible delegate content anchored to the row's bottom:
  ```qml
  component PlaceRow: Item {
      // ...
      readonly property real extraGap: row.startsBookmarks ? HnMetrics.internalSpacing(HnControlSize.Compact) : 0
      height: button.implicitHeight + extraGap
      HnListDelegate {
          id: button
          anchors.left: parent.left
          anchors.right: parent.right
          anchors.bottom: parent.bottom
          // ...
      }
  }
  ```
  Combined with `ListView.spacing: HnMetrics.internalSpacing(HnControlSize.Compact)` (unchanged),
  this makes the gap before the first bookmark exactly one compact token larger than the gap between
  two ordinary rows, matching REQ-F-028's acceptance criterion precisely. No `Separator`, no
  `section.property`/section header (REQ-F-029) — `startsBookmarks` is a per-row model role, not a
  `ListView` section mechanism, so it produces spacing only, no heading.

No new `.qml` files are introduced, so `scripts/check-qml-format.sh`'s explicit file list needs no
addition — only the existing `PlacesPanel.qml` entry is reformatted, already covered.

## 3. Data Flow

**Startup** (inside `PlacesModel`'s constructor / `buildPlaces()`):
1. Read `userDirsFilePath()` and `placesFilePath()` synchronously (two small local files,
   REQ-NF-001 — this is the one GUI-thread filesystem access Places performs). Missing files are
   silent (`TomlDocument::parseFile`'s `file_exists=false` / empty `QFile::exists()` check).
2. `UserDirsParser::parseFile()` → ordered `Entry` list; `BookmarkStore::read()` → ordered
   `Bookmark` list (each already warns internally via the injected `WarningSink` for its own
   parse-level problems, REQ-F-012..015).
3. Build the candidate list in priority order — Home, then XDG entries in display order, then
   bookmarks in file order — and run dedup (§5) against it, producing the final `places_` list, each
   row's `status` initialized to `Checking` and `id` assigned from `next_id_++` (REQ-F-019).
4. `rowCount()`/`data()` are now callable and correct — the constructor returns with all rows
   present and `Checking` before any check has run (REQ-NF-002: "model construction returns while
   the check is still blocked").
5. `dispatchStartupChecks()` fires one check per row (including Home, which is checked only to
   satisfy "checked off the GUI thread" symmetrically — its result never removes or un-avails it,
   REQ-C-005) — see §3.1 for the dispatch mechanism.

**Check resolution** (`deliverStartupResult`, called via a queued `QMetaObject::invokeMethod` back
onto the GUI thread):
- Home: status → `Available` unconditionally, one `dataChanged`. Never removed (REQ-C-005).
- XDG: available → status `Available`, one `dataChanged`; unavailable → row removed, one
  `rowsAboutToBeRemoved`/`rowsRemoved` pair (REQ-F-020/REQ-NF-003 — never *both* a status flip and a
  removal for the same result).
- Bookmark: available → `Available`; unavailable → `Unavailable`. Never removed (REQ-C-006). One
  `dataChanged` either way.

Each dispatched check carries the row's `id` (not its index — indices shift as XDG rows are removed
mid-resolution); delivery looks the id back up via `rowForId()` and is a silent no-op if the id no
longer exists (the row was already removed — cannot happen for a startup check on the *same* id
since each id is checked exactly once at startup, but keeps the delivery path defensively correct
if this logic is ever reused).

**Activation re-check** (REQ-F-022): `PlaceRow.activate()` (Bookmark rows only) calls
`DirectoryController.activateBookmark(row.index)` → `places_.recheckBookmark(row)` (returns the place id) →
`Place::recheck_generation` is bumped and captured as `expected`; a new check is dispatched exactly
like a startup check, except delivery (`deliverRecheckResult`) checks
`place.recheck_generation == expected` before applying anything — a same-row recheck superseded by
a newer `recheckBookmark()` call on that row is silently dropped in its entirety (no status flip, no
signal). The same generation guard applies to startup (generation zero): once a recheck
has been dispatched, its older startup result cannot change the row or emit signals.
Thus double-activating one bookmark before its check returns is safe by construction
(REQ-NF-003's "no duplicate/intermediate" extends naturally to this case). The row's `status` is
still applied and `bookmarkRecheckResolved(placeId, path, available)` is emitted for the winning
(freshest) delivery even if the model has, in the meantime, been asked to recheck a *different* row
— only same-row supersession is guarded, deliberately (see below).

**Discard rule when navigation/other activation intervenes** (the two components of the design
task's open question):
- *A real navigation happens before the recheck resolves* (e.g. the user activated a bookmark, then
  clicked a different Home/XDG place, or pressed `h`): `DirectoryController` drops the *controller-level*
  side effect (neither navigates nor shows the message) — see §2.6's
  `bookmark_dispatch_navigation_serial_` check. The row's status is still updated by the model
  regardless (that part is purely informational about the bookmark itself and never disturbs the
  current navigation).
- *A different bookmark is activated before the first one resolves* (no real navigation yet): **not**
  treated as staleness for the first bookmark — its eventual result is still applied and, if
  available, still navigates. Rationale for keeping this minimal: `navigation_serial_` only advances
  on an actual `openInternal()` call, so "activated a different bookmark" alone doesn't imply the
  user has moved on to a different folder; a second discard axis (e.g. a global
  `bookmark_activation_serial_`) would need careful per-row bookkeeping to avoid one bookmark's
  activation silently cancelling another's, and no REQ-F-022 acceptance scenario exercises
  concurrent multi-bookmark activation. If product feedback later wants "only the most recently
  activated bookmark may navigate", it is a small, localized addition to
  `bookmark_dispatch_navigation_serial_`'s bookkeeping, not a redesign.
- *Activation is disabled* (VISUAL/INSERT/SEARCH mode, task prompt, Quick Look — REQ-F-037): the
  `root.activationEnabled` guard in `PlaceRow.activate()` means `activateBookmark()` is never called
  in the first place. The controller repeats this guard at dispatch and completion.
  If a guard becomes active while a check is pending, the model still updates status,
  but the controller discards navigation and error-message side effects.

**Shutdown**: `DirectoryController::shutdown()` (`directory_controller.cpp:827`) is unchanged — it
calls `model_.shutdown()` / `preview_.shutdown()` / `tasks_.shutdown()`, none of which involve
`places_`. `PlacesModel` needs no `shutdown()` method and nothing to join: every check runs on a
detached `std::thread` (§3.1/§6) whose delivery is gated by the shared `DeliveryGuard` —
`~PlacesModel()` locks the guard mutex and nulls the model pointer (a bounded wait: the lock is only
ever held for one `invokeMethod` post, never across a check), so any in-flight check's eventual
delivery becomes a no-op. Events already posted but not yet processed are discarded by Qt when the
receiver is destroyed (`QObject::~QObject` removes its posted events). `DirectoryController::shutdown()` therefore never blocks
on a hung Places check, satisfying "must not hang on quit" trivially (there is nothing to wait for).

### 3.1 Dispatch mechanism

```cpp
PlacesModel::~PlacesModel() {
  const std::scoped_lock lock(guard_->mutex);
  guard_->model = nullptr;
}

void PlacesModel::dispatchCheck(quint64 id, quint64 generation, const QString& path) {
  std::thread([guard = guard_, checker = checker_, id, generation, path] {
    const bool available = checker->isAvailable(path);  // may block indefinitely; no lock held
    const std::scoped_lock lock(guard->mutex);
    if (guard->model == nullptr) {
      return;  // model destroyed while checking
    }
    QMetaObject::invokeMethod(
        guard->model, [model = guard->model, id, generation, available] { model->deliverResult(id, generation, available); },
        Qt::QueuedConnection);
  }).detach();
}
```

The thread must **not** capture `this` or call `QMetaObject::invokeMethod(this, ...)` unguarded: the
context-object overload reads the receiver's thread affinity at post time, so posting to an
already-destroyed `PlacesModel` is a use-after-free, not a no-op. The guard closes that window;
posted-but-unprocessed events are removed by Qt when the receiver is destroyed on the GUI thread.
`deliverResult()` dispatches to startup (`generation == 0`) or recheck handling (it replaces the
separate `deliverStartupResult`/`deliverRecheckResult` names used in the §2.5 sketch).

## 4. Parsing Details

### 4.1 `user-dirs.dirs` grammar

Accepted line: optional leading whitespace, then `XDG_<KEY>_DIR="<value>"`, where `<KEY>` is one of
the 9 recognized keys (`DESKTOP`, `DOCUMENTS`, `DOWNLOAD`, `PICTURES`, `MUSIC`, `VIDEOS`,
`PROJECTS`, `TEMPLATES`, `PUBLICSHARE`) and `<value>` is the quoted content up to the closing `"`.
Anything else on the line (comments starting `#`, blank lines, an unrecognized `XDG_*_DIR` key, a
line missing the surrounding quotes, trailing garbage after the closing quote) → line silently
ignored, no warning (REQ-C-007).

**Unescaping** the quoted value (REQ-F-003): scan left to right; `\"` → `"`, `\\` → `\`, `\$` → `$`,
`` \` `` → `` ` ``; any other character (escaped or not) is copied through unchanged. This exactly
matches the 4 documented escapes in REQ-F-003's acceptance criterion (`\"`, `\\`, `\$`, `` \` ``);
no other backslash sequence is specified by the spec, so others are left as-is (backslash retained)
rather than guessed at.

**`$HOME` prefix handling**: after unescaping, if the value starts with the literal 5-character
token `$HOME` followed by either end-of-string or `/`, substitute `QDir::homePath()` for that
prefix; otherwise, if the value starts with `/`, treat it as already absolute; any other form
(bare relative path, `$XDG_*`, any other `$VAR`) is malformed → silently dropped (REQ-C-007, "This
is intentional" per SPEC.md Known Risks #5). The substituted/absolute result is then run through
`QDir::cleanPath()`, which also collapses a trailing slash (REQ-F-003's "trailing slashes" note) —
so `$HOME/Videos/` and `$HOME/Videos` parse identically.

**`$HOME`-equality detection** (REQ-F-004): compare `QDir::cleanPath(resolved) ==
QDir::cleanPath(home)` — i.e. *after* cleaning both sides, not a raw string compare of the
unescaped `$HOME` token — so `XDG_DOCUMENTS_DIR="$HOME/"` (trailing slash) is also correctly
recognized as disabled. Matching entries are omitted from the result silently (no warning).

### 4.2 `places.toml` validation order and warning formats

Validation proceeds in this order (each step's failure short-circuits the ones below it for the
whole file, except per-entry/per-key checks which continue past a single bad entry/key):

| # | Condition | Result | Warning (to stderr via `WarningSink`) |
|---|---|---|---|
| 1 | File missing | No bookmarks, silent | *(none)* |
| 2 | `TomlDocument::parse` yields a diagnostic (syntax error) | Whole file ignored | `"%1: line %2: %3"` — file path, `TomlDiagnostic::line`, `TomlDiagnostic::message` (matches `state_store.cpp`'s `ignore()` format exactly) |
| 3 | Root `version` missing or not `1` | Whole file ignored | `"%1: unsupported places version"` |
| 4 | (per top-level key outside `version`/`bookmarks`) unknown key | Key ignored, file otherwise still processed | `"%1: line %2: unknown key \"%3\""` — one per key |
| 5 | (per `[[bookmarks]]` entry) `path` missing or not a string | Entry skipped | `"%1: line %2: bookmark missing a valid \"path\""` |
| 6 | (per entry) `path` is relative, a bare `$VAR` reference, or otherwise neither `~/...` nor `/...` | Entry skipped | `"%1: line %2: bookmark path \"%3\" is not absolute or ~/-relative"` |
| 7 | (per entry) unknown key inside the entry (e.g. `custom_icon`) | Key ignored, entry still loads if otherwise valid | `"%1: line %2: unknown key \"%3\" in bookmark"` |
| 8 | (per entry, after all of the above) duplicates Home/XDG/an earlier bookmark (dedup, §5) | Entry dropped | `"%1: bookmark \"%2\" duplicates %3"` — bookmark's own path, then either `"Home"`, the duplicated XDG place's name, or `"an earlier bookmark"` |

All messages are prefixed with the resolved `places.toml` path (matching `state_store.cpp`'s
`"%1: %2"` convention) and emitted through the same `WarningSink` interface already used by
`StateStore`/`TaskManager`/etc. (`apps/files/warning_sink.h`) — production wiring passes the
existing `StderrWarningSink` (REQ-C-008: stderr only, never stdout or the UI). Steps 2/3 emit
*exactly one* warning for the whole file (REQ-F-012/013); steps 4/5/6/7/8 emit one warning per
occurrence (REQ-C-007's "exactly one warning per parse error... unknown key, invalid entry...
duplicate").

`path` resolution (step 6, once accepted): `~/...` → `QDir::homePath() + rest`, then
`QDir::cleanPath`; `/...` → `QDir::cleanPath` directly. `name` resolution (REQ-F-008): explicit
`name` string if present and non-missing-type, else `QFileInfo(cleanedPath).fileName()`.

## 5. Deduplication Algorithm

Runs once, synchronously, inside `PlacesModel::buildPlaces()`, after `UserDirsParser`/
`BookmarkStore` have independently produced their candidate lists and before any row is appended to
`places_` (so dedup never sees or affects `status`/async state — it operates purely on the
`{name, path, iconName, origin}` candidate tuples).

```
seen: QSet<QString>  // cleaned path strings, via QDir::cleanPath (REQ-C-004: no symlink resolution)
append Home candidate; seen.insert(homePath)
for each XDG candidate in display order:
    if seen.contains(candidate.path): drop silently (REQ-F-016)       // e.g. two XDG_*_DIR keys aliasing
    else: append; seen.insert(candidate.path)
for each Bookmark candidate in file order:
    if seen.contains(candidate.path):
        warn(duplicates <Home | that XDG place's name | "an earlier bookmark">)   // REQ-F-017
        drop
    else: append; seen.insert(candidate.path)
```

Priority is structural (Home always first, then XDG, then Bookmarks) rather than needing an
explicit priority comparison — because each source is fully appended (subject to intra-source dedup)
before the next source is considered, "first source wins" falls out of processing order alone.
Names are **not** deduplicated (REQ-F-016's "Names are never deduplicated" — dedup is keyed purely
on the cleaned path string, never on `name`).

## 6. Key Decisions and Alternatives

| Decision | Chosen | Rejected alternative(s) | Rationale |
|---|---|---|---|
| Source of standard directories | Direct parse of `user-dirs.dirs` via `UserDirsParser` | `QStandardPaths::writableLocation()` (today's approach) | SPEC.md's Behaviour Changes #1/#2/#3/#4 explicitly require Desktop/Templates/Public/Projects support and "configured-but-nonexistent dirs now hidden" — `QStandardPaths` doesn't expose Desktop/Templates/Public on Linux and has no notion of "configured but not yet created". |
| Availability-check concurrency | One detached `std::thread` per check (§3.1) | (a) single persistent worker `QThread` (the `DirectoryModel`/`PreviewService` shape); (b) `QThreadPool` sized with headroom | (a) is rejected outright: REQ-F-021 requires a hung check not to block *other* checks, and a single worker thread serializes every check behind whichever is currently running — a dead-NFS bookmark would freeze all Places checks forever, by construction. (b) shrinks the failure window but doesn't eliminate it: a `QThreadPool` has a hard `maxThreadCount`, and enough concurrently-hung checks (e.g. several dead-mount bookmarks reactivated over a session) would still queue later checks behind them, silently reintroducing the exact bug REQ-F-021 forbids. A detached `std::thread` has no ceiling: every check is truly independent. The accepted cost — an OS thread blocked on a dead stat() leaks until process exit — is explicitly named in SPEC.md's own Known Risks #1 and is bounded in practice (checks fire only at startup, bounded by place count, and once per bookmark activation, a user-paced action, not a hot loop). |
| Row removal vs. filtering for unavailable XDG dirs | `PlacesModel` removes the row directly (`beginRemoveRows`/`endRemoveRows`) | `QSortFilterProxyModel` wrapping a always-populated source | REQ-F-018 requires XDG dirs that fail their check to be *absent*, not merely visually hidden, and REQ-F-020 requires the removal to be a real model signal (`rowsRemoved`, checked by REQ-NF-003's tests). A proxy would need custom `filterAcceptsRow()` logic keyed on the same status role anyway, adding an indirection layer with no behavior a direct model doesn't already give; `PlacesPanel.qml`'s `ListView.model` binding stays a single object either way. |
| Where sources/checks/dedup live | Inside `PlacesModel` itself, built in its own constructor (mirrors `state_store_` being built inside `DirectoryController`'s constructor from `XdgPaths` today) | `DirectoryController` owns/orchestrates parsing and hands `PlacesModel` a finished list | Keeping `PlacesModel` self-building preserves `DirectoryController controller;` default-construction unchanged (matches the project's established "controller constructs its own dependency from `XdgPaths` in its ctor" pattern) and keeps `PlacesModel` unit-testable in total isolation via its second (injectable) constructor, exactly like today's `PlacesModel(const LocationProvider&, QObject*)` test seam. |
| Role types | Enum roles (`Origin`, `Status`) via `Q_ENUM` on `PlacesModel`, `QML_ELEMENT` already present | Plain `QString` roles (`"home"`/`"xdg"`/`"bookmark"`, `"checking"`/`"available"`/`"unavailable"`) | Matches the existing in-repo convention exactly — `PreviewService::PreviewErrorKind` (`preview_service.h:36-37`) is a nested `Q_ENUM` on a `QML_ELEMENT` class, consumed from QML the same way `PlacesPanel.qml` would consume `PlacesModel.Bookmark`/`PlacesModel.Unavailable`. Type-safe, no typo risk, no new pattern introduced. |
| Muted-unavailable visual treatment | `opacity` on the whole delegate | Overriding `HnListDelegate`'s internal title-color binding; setting `enabled: false` | `HnListDelegate`'s title/icon colors are internal to the `holonight-qt` package (out of this repo), not independently exposed; `enabled: false` would also strip focus/activation, forbidden by REQ-F-023. `opacity` mutes icon+text together with zero changes outside this repo. |
| New subdirectory `apps/files/places/` | Yes, for the 3 new pure/adapter units | Add the new `.h`/`.cpp` files flat under `apps/files/` | Mirrors the two existing precedents (`settings/`, `state/`) exactly — both were "added explicitly" to the non-recursive CMake `SOURCES` list and the non-recursive `clang-format`/`format-check` globs in `Taskfile.yml`. Three new units (parser, bookmark store, checker) plus their headers is enough surface area to justify the same treatment; flagged explicitly under §8 as a wiring risk (easy to forget one of the three places it must be added). |

## 7. Test Strategy

| REQ IDs | Covered by | How |
|---|---|---|
| REQ-F-001..006, REQ-F-010 | `tests/user_dirs_parser_test.cpp` (new) | Pure `UserDirsParser::parse(text, home)` calls with literal strings — no filesystem, no model. Covers valid/malformed lines, all 4 escapes, `$HOME`-equals-disabled, fixed display order with a subset of keys present, label/icon table. |
| REQ-F-007..009, REQ-F-011..015 | `tests/bookmark_store_test.cpp` (new) | `BookmarkStore::read(path, RecordingWarningSink&)` against real temp files (`QTemporaryDir`, `files_test::fixturePattern`/`writeFile` from `tests/directory_fixtures.h`) — missing file, valid entries, each of the 8 validation-order conditions from §4.2's table, asserting exact `RecordingWarningSink::messages` counts/content. |
| REQ-F-016, REQ-F-017 | New tests in `tests/places_model_test.cpp` | Construct `PlacesModel` with the injectable ctor over fixture `user-dirs.dirs`/`places.toml` files producing intentional collisions; assert final row set and `RecordingWarningSink` messages. |
| REQ-F-018..021, REQ-C-005, REQ-C-006 | `tests/places_model_test.cpp`, using `FakePlaceAvailabilityChecker` (new, added to `tests/directory_fixtures.h` alongside `FakeLocationClassifier`) | Per-path override results (`overrides[path] = false`) for "not a directory" cases; a `QSemaphore`-gated fake (mirrors `GatedLocationClassifier`) for REQ-F-021's "one resolves in 10ms, one hangs" scenario — assert the fast row updates via `QTest::qWaitFor` while the hung row's status stays `Checking` and no `QThreadPool`/global state is starved. |
| REQ-F-022, REQ-C-006's recheck transitions | `tests/places_window_test.cpp` (extended) or a new `places_model_test.cpp` case | Real `DirectoryController` + real filesystem: create the bookmark's target directory *after* it resolved `Unavailable`, call `activateBookmark()`, `QTest::qWaitFor` on `currentPath()`/`places()->data(..., StatusRole)`; and the inverse (remove an `Available` bookmark's directory, activate, assert `directoryError`/`statusMessage` unchanged current path). |
| REQ-F-023 | `tests/places_window_test.cpp` | Render window, set a bookmark's fake status to `Unavailable` (via the injectable checker), assert the warning-icon child is visible, `button.opacity` equals the muted value, `Accessible.description` contains `"unavailable"`, and Up/Down still reaches the row. |
| REQ-F-024..027 | `tests/places_model_test.cpp` | Direct `data(index, role)` calls per origin/status, `roleNames()` contents, `rowCount()` before/after a simulated removal and a simulated status-only update (assert `rowCount()` unchanged for the latter). |
| REQ-F-028..038 | `tests/places_window_test.cpp` (existing tests preserved + extended) | The existing `PlacesWindow` fixture (`tests/places_window_test.cpp:14-50`) is reused essentially unmodified for Home/XDG-origin behavior (its `KeyboardMouseHistorySelectionAndFocus`/`GuardsBlockActivation`/`FallbackIconsAndShortWindow`/`MissingPlaceUsesDirectoryError` tests all exercise row 0, which is still `Home` with `origin === Home`, so they still take the direct `open()` path and need no changes). New cases are added for the spacing gap (REQ-F-028, measuring delegate `y` positions with 1 XDG + 2 bookmark fixture rows), no-separator/no-heading scan (REQ-F-029, code review + a light structural QML test), and bookmark-specific activation/status behavior (REQ-F-023/037/038). |
| REQ-NF-001, REQ-NF-002 | `tests/places_model_test.cpp` | `FakePlaceAvailabilityChecker` records `QThread::currentThread()` per call, asserted `!= QThread::currentThread()` (the test's own GUI thread) for every recorded call (REQ-NF-001); a `QSemaphore`-gated variant blocks all checks, and the test starts a 50ms `QTimer` on the GUI thread, asserts it still fires (`QTest::qWaitFor` on a fired-count), and asserts `PlacesModel`'s constructor already returned (implicitly true — the test reaches the `QTimer` assertions at all) while checks remain blocked, matching REQ-NF-002's acceptance criterion structure precisely. |
| REQ-NF-003 | `tests/places_model_test.cpp` | Connect to `dataChanged`/`rowsRemoved`/`rowsInserted` with counting slots; drive one fast-resolving XDG-removal case and one fast-resolving bookmark-status case; assert exactly one relevant signal each, no `modelReset`. |
| REQ-NF-004 | `tests/places_model_test.cpp` | One duplicate bookmark (parse-time warning) + one bookmark that resolves `Unavailable`; capture `RecordingWarningSink` count after construction, then call `activateBookmark()` twice via a real `DirectoryController`; assert the warning count is unchanged. |
| REQ-C-001 | `tests/xdg_paths_test.cpp` (extended) | Same `ScopedXdgConfigHome`/`ScopedXdgStateHome`-style fixture, adding `ScopedXdgDataHome` (already exists, `tests/directory_fixtures.h:121-124`) coverage for `userDirsFilePath()`/`dataDirPath()`/`placesFilePath()` across unset/empty/relative/absolute. |
| REQ-C-002, REQ-C-003 | Code review + `tests/toml_document_test.cpp` (extended) for the new array-of-tables accessors; `BookmarkStore`'s own file has no `#include <toml++/...>` — enforced by the existing `TomlDocument.TomlLibraryIsIncludedOnlyByTheAdapter` scan of `apps/` (unchanged test, now also scanning the new `places/` files). |
| REQ-C-004 | `tests/places_model_test.cpp` | Real `QFile::link()` to create a symlink alongside its target inside a `QTemporaryDir`, both added as bookmarks, assert both rows are retained (mirrors the existing `buildPermissionFixture`-style real-filesystem approach used throughout `tests/`, never mocked paths). |
| REQ-C-007, REQ-C-008 | `tests/bookmark_store_test.cpp`, `tests/places_model_test.cpp` | Exhaustive warning-count assertions per §4.2's table; stdout/stderr separated via `RecordingWarningSink` (already inherently stderr-only by construction — production wiring is the only place `StderrWarningSink::warn()`'s actual `stderr` write needs checking, covered once via the existing `warning_sink_test.cpp`). |
| REQ-C-009 | `tests/bookmark_store_test.cpp`, `tests/user_dirs_parser_test.cpp` | `QFileInfo` mtime/permission snapshots before/after parsing, matching `StateStore`'s existing precedent (`state_store_test.cpp`'s general fixture style) — assert unchanged, and assert no new file was created when none existed before. |
| REQ-C-010 | Code review of `DirectoryController::handleBookmarkRecheckResolved` (calls `open(path)` verbatim, no wrapper) + the existing `places_window_test.cpp` history/preview assertions extended to a bookmark-origin activation. |

**Keeping existing tests deterministic**: `DirectoryController controller;` (default-constructed,
used pervasively across `directory_controller_test.cpp` et al.) now transitively constructs a real
`PlacesModel` against the real `$HOME`/`XDG_*` environment and dispatches real availability checks
on detached threads. These are pure `stat()` calls against real, normally-fast local paths; they
never touch `warnings_` in a way visible to those tests (no bookmarks/XDG dedup collisions against a
real, uncontrived `$HOME` are expected to warn), and no existing test reads `places()`'s status role
or waits on it, so their pass/fail is unaffected. Tests that *do* care about Places behavior always
use the second, injectable constructor (or, for window-level tests, wait for `!controller.scanning()`
exactly as before, which is unrelated to Places' own async state).

## 8. Known Risks and Mitigations

1. **Detached check threads leak past shutdown on a hung mount** (SPEC.md Known Risks #1). Mitigated
   by design, not avoided: each leaked thread holds only a path string, a `shared_ptr<Checker>` and
   the `shared_ptr<DeliveryGuard>` (never the model itself)
   (a few hundred KB of stack, no GUI-thread resources), and the process is exiting anyway once
   `hn-files` tears down — the OS reclaims the thread at process exit. Explicitly the accepted
   trade-off for satisfying REQ-F-021 without a `QThreadPool` ceiling (§6).
2. **Existing tests constructing `DirectoryController controller;` now touch the real user's
   `$HOME`/`XDG_CONFIG_HOME`/`XDG_DATA_HOME`.** Mitigated: this is already true today for
   `state_store_`'s construction (`XdgPaths::stateFilePath()`/`stateDirPath()` against the real
   environment) — Places just adds two more real-environment reads. No test asserts on Places'
   parsed content unless it uses the injectable constructor.
3. **`toml++` literal-string test** (`TomlDocument.TomlLibraryIsIncludedOnlyByTheAdapter`, which
   scans `apps/` for the literal `toml++`, even in comments). This design document itself must never
   write that literal outside this one paragraph's necessarily-quoted reference to the test's own
   name and purpose — every other mention above says "the TOML library" or "toml_document.cpp";
   implementers must apply the same discipline in code comments in the new `places/` files.
4. **Wiring gaps for the new `places/` subdirectory** (mirrors the project's known "clang-tidy header
   filter gap" and "QML format-check gap" pattern of easy-to-forget additive wiring): adding
   `apps/files/places/*.{h,cpp}` requires *all three* of: (a) `apps/files/CMakeLists.txt`'s `SOURCES`
   list (explicit, non-recursive — the project has no `GLOB`), (b) `tests/CMakeLists.txt`'s
   `files-smoke` source list for the two new test files, (c) `Taskfile.yml`'s `format`/`format-check`
   tasks' `clang-format` glob arguments (currently `apps/files/*.cpp apps/files/*.h
   apps/files/settings/* apps/files/state/*` — needs ` apps/files/places/*` appended in **both** the
   `format` and `format-check` task bodies, matching how `settings/`/`state/` were added).
5. **New bundled warning-icon SVG asset**: needs adding to `apps/files/CMakeLists.txt`'s `RESOURCES`
   list (currently `size_format.h ... icons/folder-fallback.svg icons/generic-file-fallback.svg
   icons/go-back.svg icons/go-forward.svg`) alongside the existing fallback icons, or the fallback
   path silently 404s and `HnIcon.hasError` never clears for the warning badge.
6. **Fractional-scale (Hyprland @1.5) visual verification is manual** — the extra spacing gap, the
   muted-opacity treatment, and the new trailing warning badge are all new visual elements that
   `task visual-check`'s offscreen capture cannot fully validate (per the project's established
   fractional-scale gotcha); a native run is required before considering REQ-F-023/028 visually
   done, per SPEC.md's own Acceptance Scenario #13.
7. **`trash_service.cpp`'s duplicate `XDG_DATA_HOME` resolution is left unconsolidated** (§2.3) —
   intentionally out of scope; flagged so it isn't mistaken for an oversight.

## 9. File Change List

**New:**
- `apps/files/places/user_dirs_parser.h`, `apps/files/places/user_dirs_parser.cpp`
- `apps/files/places/bookmark_store.h`, `apps/files/places/bookmark_store.cpp`
- `apps/files/places/place_availability_checker.h`, `apps/files/places/place_availability_checker.cpp`
- `apps/files/icons/warning-fallback.svg` (bundled fallback for the Unavailable-bookmark badge)
- `tests/user_dirs_parser_test.cpp`
- `tests/bookmark_store_test.cpp`

**Modified:**
- `apps/files/places_model.h`, `apps/files/places_model.cpp` — full rewrite per §2.5
- `apps/files/directory_controller.h`, `apps/files/directory_controller.cpp` — `activateBookmark()`,
  `handleBookmarkRecheckResolved()`, one new connect, `bookmark_dispatch_navigation_serial_` (§2.6)
- `apps/files/PlacesPanel.qml` — activation branch, status badge, opacity, `Accessible.description`
  override, spacing gap (§2.7)
- `apps/files/settings/xdg_paths.h`, `apps/files/settings/xdg_paths.cpp` — `userDirsFilePath()`,
  `dataDirPath()`, `placesFilePath()` (§2.3)
- `apps/files/settings/toml_document.h`, `apps/files/settings/toml_document.cpp` —
  array-of-tables accessors (§2.2.1)
- `apps/files/CMakeLists.txt` — new `SOURCES`/`RESOURCES` entries (§8 risk 4/5)
- `tests/CMakeLists.txt` — new test-file entries
- `Taskfile.yml` — `format`/`format-check` clang-format glob additions (§8 risk 4)
- `tests/directory_fixtures.h` — `FakePlaceAvailabilityChecker` (mirrors `FakeLocationClassifier`)
- `tests/places_model_test.cpp` — substantially rewritten (the current `LocationProvider`-based
  tests no longer apply to the redesigned sources)
- `tests/places_window_test.cpp` — extended with bookmark/status/spacing cases; existing Home/XDG
  cases preserved (§7)
- `tests/xdg_paths_test.cpp`, `tests/toml_document_test.cpp` — extended for the new functions/accessors

## Related Documents

- `docs/sdd/places-sources/SPEC.md` — the approved requirements this design implements.
- `docs/sdd/places/SPEC.md` — prior Places spec (P-003..P-007 behaviours this design preserves).
- `docs/sdd/app-configuration/SPEC.md` / its DESIGN.md — the TOML-adapter and XDG-path-resolution
  conventions this design reuses.
