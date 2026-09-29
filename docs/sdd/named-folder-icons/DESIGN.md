# Named-Folder Icons in the Main Directory Listing — Design

Status: Draft
Spec: `docs/sdd/named-folder-icons/SPEC.md`
Builds on: `docs/sdd/main-view-icons/` (IconNameResolver, IconImageProvider, IconFallbacks, `IconNameRole`)

## Spec conflicts and factual corrections found

Nothing blocks the design, but four spec statements do not match the code as read. The design follows the
code and calls out each one so the Stage 3/4 reader is not surprised.

1. **REQ-F-011 says the placeholder shows "the generic folder chain".** Today
   `DirectoryModel::insertPlaceholderRow()` sets `IconNameResolver::genericFallbackName(false)`, i.e.
   `application-x-generic`, and `tests/directory_model_test.cpp:438` pins that. main-view-icons REQ-F-011 is the
   origin of the behaviour. This design leaves the placeholder code **untouched** (it never consults the place
   map, which is what REQ-F-011's acceptance criteria actually require) and reads "generic" as "the existing
   generic chain, unchanged". If the author truly wants a folder glyph on the placeholder, that is a
   main-view-icons change, not a named-folder one. Flag for confirmation.
2. **REQ-F-003's Desktop example.** `UserDirsParser` maps Desktop to `user-desktop` (not `folder-desktop`),
   Downloads to `folder-download`, Projects to `folder-development`, Public to `folder-publicshare`. The listing
   therefore shows chains like `user-desktop/folder/inode-directory`. Correct by REQ-F-014 (same names as the
   sidebar); tests must not hard-code `folder-<key>` by pattern.
3. **REQ-F-008/REQ-C-002 speak of "the new `IconNameRole`".** It already exists (`DirectoryModel::IconNameRole`,
   role name `iconName`), so no role is added; REQ-C-003 is satisfied trivially.
4. **REQ-F-001 says Home comes from "the parser".** `UserDirsParser::parse()` deliberately drops any entry equal
   to `$HOME`; Home is not a parser key. Home's `user-home` therefore lives in the shared place list (§4.1),
   exactly as REQ-F-001's third acceptance bullet says.

## 1. Overview

A new tiny module, `PlaceList` (`apps/files/places/place_list.{h,cpp}`), builds **one** startup list of standard
places: Home (`user-home`) followed by the XDG entries from `UserDirsParser::parseFile()`, deduplicated by path.
`PlacesModel::buildPlaces()` is rewritten to consume that list instead of hard-coding Home and calling the parser
itself. `PlacesModel` also keeps an immutable, path-keyed icon map derived from the same list and exposes it
(`placeIcons()`); `DirectoryController`'s constructor hands that shared map to `DirectoryModel` through a
C++-only setter. `DirectoryModel` snapshots the `shared_ptr` into each walk (the same idiom as `classifier_`), and
the worker looks up each directory entry's cleaned absolute path with a single hash lookup. A hit is passed to
`IconNameResolver::candidateIconNames()` as a new optional third argument, which prepends it to the directory
chain. The `..` row (both `readParentEntry()` and `syntheticParentEntry()`) is resolved from the parent's own
path the same way. `PreviewPane.qml` stops inferring "folder" from the chain's first name and reads a new
`PreviewService::isDirectory` property instead.

No QML change is needed in `DirectoryListing.qml` (it already selects the fallback glyph with the model's `isDir`
and keys `IconFallbacks` on the whole chain string). No new thread, no new QObject, no per-entry syscall.

## 2. Component inventory

| File | Status | Responsibility |
|---|---|---|
| `apps/files/places/place_list.h` / `.cpp` | **New** | `PlaceList` namespace: `StandardPlace`, `IconMap`, `standardPlaces(home, userDirsFile)`, `iconMap(places)`. The single definition of Home's icon name and the single caller of `UserDirsParser::parseFile()` (REQ-F-001, REQ-C-001). Pure, QObject-free, no threads. |
| `apps/files/browsing/places_model.h` / `.cpp` | Modified | `buildPlaces()` consumes `PlaceList::standardPlaces()`; the `"user-home"` literal and the direct `UserDirsParser` calls are removed. New `placeIcons()` accessor and `place_icons_` member. Bookmark handling, availability checks, roles: unchanged. |
| `apps/files/browsing/directory_model.h` / `.cpp` | Modified | New C++-only `setPlaceIcons()`, private `place_icons_` (`shared_ptr<const IconMap>`). `readEntry()`, `readParentEntry()`, `syntheticParentEntry()`, `walkDirectory()` gain a map parameter. Placeholder row, roles, Q_PROPERTYs: untouched (REQ-C-003). |
| `apps/files/presentation/icon_name_resolver.h` / `.cpp` | Modified | `candidateIconNames()` gains a defaulted `namedIcon` parameter; only honoured for `S_ISDIR` modes (REQ-C-005, REQ-F-005). |
| `apps/files/application/directory_controller.cpp` | Modified | One line in the constructor body: `navigation_.model_.setPlaceIcons(places_.placeIcons());` (REQ-F-013, REQ-C-004). |
| `apps/files/preview/preview_service.h` / `.cpp` | Modified | New `Q_PROPERTY(bool isDirectory READ isDirectory NOTIFY changed)` backed by the existing `is_dir_` member (REQ-F-006). No new state, no signature change to `setTarget()`. |
| `apps/files/qml/inspection/PreviewPane.qml` | Modified | `isFolderIconName` (line ~264) replaced by `root.preview.isDirectory`; the `startsWith("folder/")` test is removed. |
| `apps/files/CMakeLists.txt` | Modified | Add `places/place_list.cpp` and `places/place_list.h` next to `places/user_dirs_parser.*` (list at ~line 51). |
| `tests/place_list_test.cpp` | **New** | Unit tests for `PlaceList` (Home first, XDG order, dedup, empty file). Added to the `files-smoke` source list in `tests/CMakeLists.txt`. |
| `tests/icon_name_resolver_test.cpp` | Modified | New cases for the `namedIcon` argument (REQ-F-003/005/016). |
| `tests/directory_model_test.cpp` | Modified | New cases listing a fixture with a place map (REQ-F-002/004/008-011/013/017). |
| `tests/places_model_test.cpp` | Modified | Existing expectations must keep passing unchanged (regression proof for the refactor); one new case for `placeIcons()` parity (REQ-F-014). |
| `tests/preview_service_test.cpp`, `tests/smoke.cpp` (or `preview_consumer_dpr_test.cpp`) | Modified | `isDirectory` assertions; folder-fallback glyph for a named chain on a theme lacking the named icon (REQ-F-006). |

Explicitly **not** modified: `DirectoryListing.qml`, `PlacesPanel.qml`, `QuickLookOverlay.qml`,
`DirectoryProxyModel`, `IconImageProvider`, `IconFallbacks`, `UserDirsParser`, `preview_selection.cpp`
(it already forwards `IconNameRole` verbatim into `PreviewService::setTarget()`), `main.cpp`,
`XdgPaths`.

No new `.qml` file is introduced, so the QML file-list chore in CMake/Taskfile does not apply. The new
`place_list.h` lives under `apps/files/places/`; per the known clang-tidy `HeaderFilterRegex` gap, headers there
are not checked by `task tidy`, so naming and style must be kept clean by hand and by `task format-check`.

## 3. Data flow

```
Startup (GUI thread, once)
  DirectoryController ctor
    members constructed in declaration order: places_ (PlacesModel) ... navigation_ (holds DirectoryModel)
    PlacesModel(homePath = QDir::homePath(), userDirsFile = XdgPaths::userDirsFilePath(), ...)
      buildPlaces():
        places   = PlaceList::standardPlaces(homePath, userDirsFile)      // Home + parseFile(), dedup
        for p in places -> append Place{name, path, icon_name, Home|XdgUserDirectory}   // sidebar rows
        place_icons_ = PlaceList::iconMap(places)                          // QHash<path, iconName>, const
        bookmarks appended as before (dedup against `places` paths)
    ctor body:  navigation_.model_.setPlaceIcons(places_.placeIcons());    // shared_ptr<const IconMap>

Per load()/refresh() (GUI thread, snapshot)
  DirectoryModel::startWalk()
    const auto placeIcons = place_icons_;              // shared_ptr copy, captured by value in the lambda
    load(): entries_[0] = syntheticParentEntry(path, place_icons_.get())   // GUI thread, const read

Worker thread (walkDirectory)
  readParentEntry(path, places):
      parentPath = cleanPath(absolutePath(path))
      entry = readEntry(parentPath, ".", nullptr)      // stat only; no map, "." would never match
      if stat failed -> syntheticParentEntry(path, places)
      named = lookup(places, parentPath)               // cleaned parent path, one hash probe
      entry.icon_name = candidateIconNames(entry.mode, "..", named).join('/')
  readEntry(dir, name, places):
      stat() ok:
         named = entry.is_dir ? lookup(places, entry.absolute_path) : QString()
         icon_name = candidateIconNames(mode, name, named).join('/')
      stat() failed (dangling link, EIO):
         icon_name = genericFallbackName(false)        // unchanged; never named
  -> batch -> queued to GUI thread -> DirectoryModel::data(IconNameRole)  (unchanged path)

GUI consumers (all unchanged except PreviewPane's fallback selector)
  DirectoryListing row : "image://icon/" + iconName ; fallback glyph by model isDir
  PreviewService       : iconName verbatim from IconNameRole (preview_selection.cpp) ; isDirectory from is_dir_
  PreviewPane          : fallback glyph = preview.isDirectory ? folder-fallback.svg : generic-file-fallback.svg
  IconImageProvider    : splits chain on '/', first non-null theme pixmap wins (folder-documents, else folder,
                         else inode-directory)
```

## 4. Interfaces

### 4.1 `PlaceList` (new, `apps/files/places/place_list.h`)

```cpp
#pragma once

#include <QHash>
#include <QString>

#include <memory>
#include <vector>

// The one startup list of standard places (Home + XDG user directories) that both the sidebar
// (PlacesModel) and the directory listing (DirectoryModel, via IconMap) are derived from
// (SPEC.md REQ-F-001, REQ-C-001). Pure and QObject-free; the only file I/O is UserDirsParser::parseFile().
namespace PlaceList {

struct StandardPlace {
  QString name;       // translated sidebar label (context "PlacesModel")
  QString path;       // QDir::cleanPath()'d absolute path
  QString icon_name;  // "user-home" for Home, UserDirsParser::iconName(key) otherwise
  bool is_home = false;
};

// cleaned absolute path -> named icon name. Built once, never mutated afterwards.
using IconMap = QHash<QString, QString>;

// Home first, then UserDirsParser::parseFile(userDirsFilePath, cleanedHome) in its Key order. An entry
// whose path was already seen is dropped silently (two XDG_*_DIR keys aliasing one path, the rule
// PlacesModel::buildPlaces() applies today). A missing user-dirs.dirs yields the Home-only list.
[[nodiscard]] std::vector<StandardPlace> standardPlaces(const QString& homePath, const QString& userDirsFilePath);

// Keyed by StandardPlace::path with no filtering or transformation (REQ-F-001). Never null.
[[nodiscard]] std::shared_ptr<const IconMap> iconMap(const std::vector<StandardPlace>& places);

}  // namespace PlaceList
```

Notes:
- The Home label is produced with `QCoreApplication::translate("PlacesModel", "Home")`, i.e. the same
  translation context `PlacesModel::tr("Home")` uses, so existing translations and the
  `PlaceTranslator` in `places_model_test.cpp` keep working.
- `standardPlaces()` cleans `homePath` itself (`QDir::cleanPath`), matching `buildPlaces()` today, and passes
  the cleaned value to the parser, which already drops any XDG entry equal to home.

### 4.2 `PlacesModel` (modified)

```cpp
// places_model.h, public:
[[nodiscard]] std::shared_ptr<const PlaceList::IconMap> placeIcons() const { return place_icons_; }
// private:
std::shared_ptr<const PlaceList::IconMap> place_icons_;
```

`buildPlaces()` becomes: build `standardPlaces`, append one `Place` per entry (`Origin::Home` when
`is_home`, else `Origin::XdgUserDirectory`; `iconName = icon_name`), fill the local `seen` set from those paths,
set `place_icons_ = PlaceList::iconMap(...)`, then run the unchanged bookmark loop. The constructor
signatures do not change, so every existing test seam (`homePath`, `userDirsFilePath`) keeps working.

The map is built from the startup list and is not pruned when a missing XDG directory is later removed from
the sidebar (`deliverResult`): a directory that does not exist can never appear in a listing, so the stale
entry is inert, and pruning would violate "immutable" (REQ-NF-001).

### 4.3 `IconNameResolver` (modified)

```cpp
// Before: candidateIconNames(quint32 mode, const QString& fileName)
// After:
// `namedIcon` is the place icon for this entry's exact path ("" for none). It is prepended only when
// S_ISDIR(mode); for every other mode it is ignored, so a file can never receive a folder icon
// (SPEC.md REQ-F-005). Result for a matched directory: {namedIcon, "folder", "inode-directory"}.
[[nodiscard]] QStringList candidateIconNames(quint32 mode, const QString& fileName, const QString& namedIcon = {});
```

Directory branch: `if (!namedIcon.isEmpty()) chain.append(namedIcon)` then `folder`, `inode-directory`, using the
existing `appendUnique` (so a hypothetical `namedIcon == "folder"` cannot duplicate). The defaulted argument
keeps every existing call site and test compiling; all new logic stays in this file (REQ-C-005). The resolver
still knows nothing about paths or the place list — it receives an already-looked-up name.

### 4.4 `DirectoryModel` (modified)

```cpp
// directory_model.h
#include "places/place_list.h"

// public (NOT Q_INVOKABLE, no Q_PROPERTY, no signal, no role — REQ-C-003):
// Installs the immutable place->icon map used by walks started after this call (load()/refresh()).
// Rows already in the model are not re-resolved; callers refresh() to apply it. GUI thread only.
// A null pointer restores generic-only behaviour.
void setPlaceIcons(std::shared_ptr<const PlaceList::IconMap> icons);

// The synthetic ".." row. `places` may be null (generic chain).
static DirectoryEntry syntheticParentEntry(const QString& path, const PlaceList::IconMap* places = nullptr);

// private:
std::shared_ptr<const PlaceList::IconMap> place_icons_;   // null until set
```

Free helpers in `directory_model.cpp` (anonymous namespace):

```cpp
QString namedIconFor(const PlaceList::IconMap* places, const QString& cleanedPath);  // "" if null/miss
DirectoryEntry readEntry(const QString& path, const QString& name, const PlaceList::IconMap* places);
DirectoryEntry readParentEntry(const QString& path, const PlaceList::IconMap* places);
void walkDirectory(const QString& path, const std::shared_ptr<std::atomic_bool>& cancel,
                   const std::function<void()>& beforeOpen, int readErrorAfter,
                   const std::shared_ptr<const PlaceList::IconMap>& places, const PublishBatch& publish);
```

`startWalk()` copies `place_icons_` into a local `const auto placeIcons` before `invokeMethod` and captures it by
value in the worker lambda, exactly as it does for `classifier_`/`beforeOpen`. The worker dereferences only
`placeIcons.get()` and calls `constFind()`/`value()`.

`syntheticParentEntry()` remains `static` and public so its (currently in-file only) callers keep compiling;
the new parameter is defaulted. `load()` passes `place_icons_.get()`; `readParentEntry()` passes its `places`.

### 4.5 `PreviewService` (modified)

```cpp
Q_PROPERTY(bool isDirectory READ isDirectory NOTIFY changed)
bool isDirectory() const { return has_entry_ && is_dir_; }
```

`is_dir_` is already set by `setTarget()` and reset by `clear()`, and the existing `changed()` emission already
covers it. `PreviewPane.qml` line ~264 becomes
`readonly property bool isFolderIconName: root.preview.isDirectory` (property renamed to
`isFolderEntry`; the `startsWith("folder/")` and `=== "folder"` comparisons are deleted).

### 4.6 Wiring (`DirectoryController` constructor)

```cpp
// after navigation_.proxy_.setSourceModel(&navigation_.model_);
navigation_.model_.setPlaceIcons(places_.placeIcons());
```

`places_` is declared before `navigation_` in `directory_controller.h` (line 157 vs 162), so it is constructed
first and its map exists. The controller's first `open()` is deferred by `main.cpp` until after construction, so
the setter always runs before the first `load()`.

## 5. Key decisions and rationale

### 5.1 Shared list lives in `PlaceList`, `PlacesModel` owns the built map (REQ-F-001, -014, REQ-C-001/004)

The spec requires Home's name to be defined once and both consumers to share one source. The smallest change is
a free function returning the list, with `PlacesModel::buildPlaces()` as its first consumer and the map handed
onward by the controller. `PlacesModel` already reads `user-dirs.dirs` once at construction, so hosting the
map there means the file is parsed exactly once per process (REQ-F-013, "no re-read"). `DirectoryController` is
the only production object that owns both models, so it is the natural, one-line wiring point; no global, no
singleton, no second `XdgPaths` lookup, no change to `main.cpp`.

### 5.2 Map handoff: setter with `shared_ptr<const IconMap>`, snapshotted per walk (REQ-F-012, REQ-NF-001, REQ-C-003)

`DirectoryModel` is a value member of `NavigationSession`, which is constructed from just the controller
reference; a constructor argument would have to be threaded through `NavigationSession` and every one of the many
tests that write `DirectoryModel model;`. A setter keeps the default constructor and all existing tests
unchanged. The `shared_ptr<const T>` snapshot copied into the worker lambda is the pattern this codebase already
uses for `classifier_`: the worker never touches a `DirectoryModel` member, an in-flight walk keeps its own
reference alive if the setter later replaces the pointer, and no lock is needed because nothing is ever mutated.
That satisfies REQ-NF-001's "const, no synchronization" literally, and replacing the map (only tests do) cannot
tear a running walk.

### 5.3 Must the map be set before the first `load()`? No, but production always does (REQ-F-013)

The default is a null pointer: walks started before `setPlaceIcons()` produce generic chains and never
dereference a missing map (`namedIconFor` returns `""` for null). `setPlaceIcons()` does **not** re-resolve
existing rows or trigger a refresh by itself, because that would hide a timing dependency and interfere with the
watcher-driven refresh machinery. It affects walks started afterwards; a test lists first, sets the map, calls
`refresh()`, and observes `dataChanged` for the changed rows (`DirectoryEntry::icon_name` already participates in
`operator==`, and the existing test `RefreshEmitsDataChangedWhenAnEntrysIconChanges` proves the diff path handles
icon changes). This is exactly the "list before set is generic, after set is named" test REQ-F-013 asks for.

### 5.4 Injection seam for tests: the existing `PlacesModel` seam plus `PlaceList` directly (REQ-F-013, -014, -016, -017)

No new seam is invented and no environment is mutated:
- **Home and XDG paths**: `PlaceList::standardPlaces(homePath, userDirsFilePath)` takes both as arguments, as does
  the existing `PlacesModel` test constructor. A `DirectoryModel` test writes a `user-dirs.dirs` into a
  `QTemporaryDir`, builds `PlaceList::iconMap(PlaceList::standardPlaces(home, file))`, and calls
  `model.setPlaceIcons(map)`. The "home" and place directories are real subdirectories of the temp dir, so the
  real worker, `stat()` and `readdir()` are exercised end to end.
- **Sidebar consistency (REQ-F-014)**: build a `PlacesModel` with the fixture's `(home, userDirs, places.toml,
  checker, warnings)`, call `dirModel.setPlaceIcons(places.placeIcons())`, and assert for each XDG row that
  `chain.split('/').first() == PlacesModel::IconNameRole` of the row with that `PathRole`. This proves both
  consume one list.
- **Whole-controller tests** use the controller's default constructor, which reads the developer's real
  `$HOME`/`XDG_CONFIG_HOME`. Tests that need named icons through the controller should set `XDG_CONFIG_HOME` and
  `HOME` before constructing it (the `QTemporaryDir` env pattern already used by settings tests); the
  model-level tests above are preferred and need none of that.

### 5.5 Exact-path hash lookup keyed by the listed path (REQ-F-002, -004, REQ-NF-002)

`entry.absolute_path` is `QDir(path).absoluteFilePath(name)`; `QDir::path()` is already cleaned, and `name` is a
single `readdir()` component, so the key equals what `QDir::cleanPath()` would produce. The parent row uses
`QDir::cleanPath(QFileInfo(path).absolutePath())`, the same value `readParentEntry()` already stores. Map keys are
cleaned by `UserDirsParser` and by `standardPlaces()`. Lookup is one `QHash::value()` per directory entry: no
`realpath()`, `readlink()`, `lstat()` or extra `stat()` (the one `lstat()` in `readEntry` for the dangling-link
error message pre-exists and is on the failure path only). A symlink is matched by its own listed path because
`readEntry` uses the listed path, and `stat()` (already there) only supplies `S_ISDIR`; `~/Docs -> ~/Documents`
therefore looks up `~/Docs`, misses, and gets `folder/inode-directory`. A symlink named `~/Documents` pointing
elsewhere is a hit if its target is a directory, and a dangling one is generic because the `stat()`-failed branch
never consults the map.

### 5.6 Directories only, enforced inside `IconNameResolver` (REQ-F-005, REQ-C-005)

The lookup in `readEntry` is additionally guarded by `entry.is_dir` (skipping a pointless hash probe for files),
but the authoritative guard is in `candidateIconNames()`, which ignores `namedIcon` unless `S_ISDIR(mode)`. One
enforcement point means a future caller cannot mis-prepend by forgetting the check, and the resolver's unit test
can prove REQ-F-005 with synthetic `mode_t` values, no filesystem needed. (A regular file cannot really sit at a
place path; the test uses `S_IFREG` with a non-empty `namedIcon`.)

### 5.7 PreviewPane: expose `isDirectory` from `PreviewService` instead of parsing the chain in QML (REQ-F-006)

Two options were considered (see §6). Chosen: a `bool isDirectory` property. `PreviewService::is_dir_` already
exists and is already fed from `DirectoryModel::IsDirRole`, so this is a one-line accessor plus one
`Q_PROPERTY`, with `NOTIFY changed` reusing the existing signal. The decision then rests on the actual fact
(the entry is a directory) rather than on a naming convention of a string built elsewhere, which is what made
the old `startsWith("folder/")` test fragile in the first place. It also mirrors `DirectoryListing.qml`, which
already uses the model's `isDir` for the same choice, so listing and preview now select the glyph by the same
signal. Spec REQ-F-006 explicitly permits either; REQ-C-003's ban on new QML API applies to `DirectoryModel`
only, and `PreviewService` already exposes many read-only properties for this pane.

### 5.8 Placeholder and stat-failed rows never consult the map (REQ-F-011)

`insertPlaceholderRow()` sets its chain from `genericFallbackName(false)` with no path and no map, so it cannot
inherit its anchor row's icon. The `stat()`-failed branch of `readEntry` likewise stays generic. No code is
added for either; the acceptance criterion "hard-coded to the generic chain, not computed from the anchor row's
path" is met by not touching them, and a test pins the chain in a place directory.

### 5.9 Log inflation (REQ-F-007)

No change to the provider or `IconFallbacks`. On a theme lacking `folder-documents`, the provider still returns
the pixmap of the next candidate (`folder`) and no failure occurs at all. Only a theme lacking every candidate
records a failure, once per distinct chain string (`IconFallbacks::markUnresolved(chain)`), and there are at
most one such chain per place plus the two existing ones (at most 11 directory chains total), never one per
row. The existing smoke assertion `isUnresolved("folder/inode-directory")` is unchanged and still valid.

## 6. Alternatives considered

| Alternative | Why rejected |
|---|---|
| **`DirectoryModel` takes the map in its constructor.** | `DirectoryModel` is embedded by value in `NavigationSession` and default-constructed in ~30 tests; threading a parameter through all of them is churn for no gain. A setter also lets tests demonstrate list-before-set (REQ-F-013). |
| **`DirectoryModel` builds its own map from `UserDirsParser`.** | Violates REQ-F-001 (second parse, second Home literal, two sources that can drift). |
| **Hold the map in a global/singleton (e.g., `PlaceList::instance()`).** | Hidden state; defeats test injection and makes REQ-NF-001's immutability a convention instead of a type. |
| **Pass `const IconMap&` to the worker instead of a `shared_ptr` snapshot.** | A reference to a `DirectoryModel` member would dangle if the setter replaced it during a walk, or if the model were destroyed while detached from a cancelled walk. The `shared_ptr` snapshot removes both hazards, matching `classifier_`. |
| **Mutex-guarded mutable map updated on `user-dirs.dirs` change.** | Explicitly out of scope (non-goal 5, REQ-NF-001); needs locks and a file watcher. |
| **Put the place lookup inside `IconNameResolver` (give it the map and the path).** | Makes a pure mode/name function path- and state-aware and harder to unit test; REQ-C-005 wants only "an optional named-icon argument". The lookup is a one-liner in `DirectoryModel`, next to the data it already owns. |
| **Compare `Place` rows by name (`Documents`) instead of full path.** | Non-goal 3, REQ-F-002. |
| **Canonicalize (`realpath`) entries or map keys to catch symlinked homes.** | Non-goal 4 and REQ-NF-002 forbid per-entry syscalls; see risk R1. |
| **PreviewPane: detect folders by splitting the chain in QML** (`iconName.split("/").indexOf("folder") >= 0`). | Works today (every directory chain contains a literal `folder`; no file chain does), needs no C++ change, and is the smaller diff. Rejected because it still infers a fact from a naming convention of a string assembled in another module, so it can silently break again if the directory chain ever changes; it also puts string parsing in a QML binding that re-evaluates on every `iconName` change. |
| **PreviewPane: pass an extra `isDir` argument through `setTarget()`/`syncPreviewTarget()`.** | Unnecessary: `setTarget()` already receives `isDir` and stores `is_dir_`. |
| **Replace the parent-row lookup with a second lookup site outside the worker (GUI-thread post-processing).** | Violates REQ-F-012 (matching on the worker). `load()`'s synthetic parent is the only GUI-thread call and is a const hash read of the immutable map, which REQ-F-009 requires ("including `syntheticParentEntry`"). |
| **Trigger `refresh()` automatically inside `setPlaceIcons()`.** | Surprising side effect, would race with the initial `load()` in production, and breaks a listing that was intentionally suspended. |

## 7. Known risks

- **R1: symlinked home.** If `$HOME` or a parent is a symlink (e.g. `/home -> /var/home`), a user who reaches the
  same directory through its canonical path sees generic icons. This is exactly non-goal 4 and is consistent
  with how the sidebar navigates (it opens the listed path). Accepted; documented, not mitigated.
- **R2: sidebar-only pruning.** `PlacesModel` removes non-existent XDG rows, but `place_icons_` keeps them. Inert
  (a nonexistent path is never listed); noted so the difference is not mistaken for a parity bug.
- **R3: existing DirectoryController tests use the real `$HOME`.** A developer whose `~/Documents` is a place will
  now see `folder-documents/...` chains in any controller-level test that lists `$HOME` and asserts a bare
  `folder/inode-directory`. Existing assertions found in `tests/directory_model_test.cpp` use temporary
  directories, which never match. Stage 4 must grep for `folder/inode-directory` assertions in
  controller/window tests and give any that list a real home an isolated `HOME`/`XDG_CONFIG_HOME`, not weaken the
  assertion.
- **R4: placeholder wording conflict** (spec-conflict item 1). If the spec author intends a folder glyph on the
  placeholder, that contradicts main-view-icons and requires a separate decision.
- **R5: theme lacks `folder-documents` but has `folder`.** Handled by the chain; the provider returns the
  `folder` pixmap and reports success, so the named row simply looks generic. No fallback SVG is involved.
- **R6: clang-tidy header gap.** `HeaderFilterRegex` does not cover `apps/files/**/*.h`, so problems in the new
  `place_list.h` would not be reported by `task tidy`; review manually. `task tidy` also runs in no other task.
- **R7: `DirectoryEntry::icon_name` now depends on the map for equality.** A refresh after `setPlaceIcons()`
  reports `dataChanged` for rows whose chain changed; this is intended (§5.3) and the existing diff logic needs no
  change.
- **R8: `QCoreApplication::translate` in `PlaceList` before a translator is installed.** `standardPlaces()` runs
  in the `PlacesModel` constructor, as `tr("Home")` did, so the timing and result are unchanged.

## 8. Test plan

- `icon_name_resolver_test.cpp`: directory with `namedIcon` -> `{folder-documents, folder, inode-directory}`;
  directory without -> `{folder, inode-directory}`; `S_IFREG`/`S_IFIFO` with a non-empty `namedIcon` -> the file
  chain, unchanged (REQ-F-005); existing tests untouched.
- `place_list_test.cpp` (new): Home first with `user-home`; XDG entries in `Key` order with parser icon names;
  duplicate path dropped; missing file -> Home only; map keys equal place paths; map is non-null when the list is
  empty of XDG entries.
- `directory_model_test.cpp` (temp-dir fixture with an XDG `user-dirs.dirs`, real worker):
  1. matched `Documents` row chain `folder-documents/folder/inode-directory`; same-named `work/Documents`
     stays `folder/inode-directory` (REQ-F-002).
  2. `Docs -> Documents` symlink generic; symlink literally named as a place path pointing at a directory is
     named; dangling link generic (REQ-F-004).
  3. regular file next to places generic (REQ-F-005/008).
  4. home directory row (list its parent) chain begins `user-home` (REQ-F-010).
  5. list `Documents/Projects`: `..` row (both the synthetic row right after `load()` and the stat'ed row after
     settling) chain begins `folder-documents` (REQ-F-009); a parent that is not a place stays generic.
  6. placeholder in a place directory keeps the existing generic chain (REQ-F-011).
  7. list before `setPlaceIcons()` is generic; after `setPlaceIcons()` + `refresh()` it is named and `dataChanged`
     fired (REQ-F-013).
  8. sidebar parity with a `PlacesModel` built from the same fixture (REQ-F-014).
  9. `roleNames()` unchanged, no new Q_PROPERTY (REQ-C-003; asserted by existing role-name test).
- `places_model_test.cpp`: all existing tests pass unmodified (REQ-C-001 refactor safety).
- `preview_service_test.cpp`: `isDirectory` true for a directory target, false for file and after `clear()`.
- Window/QML test with a theme lacking `folder-documents` (e.g., `bareEngine` in `smoke.cpp`): preview of a
  Documents directory shows `folder-fallback.svg`, a regular file shows `generic-file-fallback.svg` (REQ-F-006);
  `IconFallbacks` records one unresolved chain for 100 same-chain rows (REQ-F-007).
- Native visual check at 1.5x fractional scale on the real display (Hyprland), per the project's known
  offscreen-screenshot gap; icon sizes are unchanged, only names differ.

## 9. Requirement traceability

| REQ | Satisfied by |
|---|---|
| REQ-F-001 | `PlaceList::standardPlaces()`/`iconMap()` (§4.1): sole caller of `parseFile()`, sole `user-home` definition; `PlacesModel` consumes it (§4.2); `DirectoryController` passes the map (§4.6). Tests: `place_list_test.cpp`, `places_model_test.cpp`. |
| REQ-F-002 | Exact-key `QHash` lookup on the cleaned listed path (§5.5); `DirectoryModel` tests 1. |
| REQ-F-003 | `IconNameResolver::candidateIconNames(..., namedIcon)` (§4.3); `kChainSeparator` join unchanged; resolver and model tests. |
| REQ-F-004 | Lookup uses the entry's own listed path; no `realpath`/`readlink` (§5.5); model test 2. |
| REQ-F-005 | `S_ISDIR` guard inside `candidateIconNames()` (§5.6); resolver test + model test 3. |
| REQ-F-006 | `PreviewService::isDirectory` + `PreviewPane.qml` selector (§4.5, §5.7); window test with bare theme. |
| REQ-F-007 | No provider/`IconFallbacks` change; failures bounded per distinct chain (§5.9); window test. |
| REQ-F-008 | `readEntry()` / `namedIconFor()` on the worker (§3, §4.4); model tests 1, 3. |
| REQ-F-009 | `readParentEntry()` and `syntheticParentEntry(path, places)` resolve from the parent's own path (§3, §4.4); model test 5. |
| REQ-F-010 | Home is in the shared list with `user-home` and matched like any other place (§4.1); model test 4. |
| REQ-F-011 | Placeholder code and `stat()`-failed branch untouched, never consult the map (§5.8); model test 6; see spec-conflict item 1. |
| REQ-F-012 | Map read only inside `walkDirectory`/`readEntry` via a captured `shared_ptr<const IconMap>`; no new thread (§4.4, §5.2). |
| REQ-F-013 | Map built once in `PlacesModel::buildPlaces()`, set in the controller ctor; null map is generic; list-before-set test (§4.6, §5.3). |
| REQ-F-014 | `PlacesModel` and `DirectoryModel` derive from the same `standardPlaces()` output; parity test (§5.1, §5.4). |
| REQ-F-015 | `preview_selection.cpp` already forwards `IconNameRole` verbatim; `DirectoryListing` reads the same role; no re-resolution added anywhere (§2, §3). |
| REQ-F-016 | Resolver and `PlaceList` unit tests (§8). |
| REQ-F-017 | `DirectoryModel` listing tests with a place map, including `..` (§8, items 1-8). |
| REQ-NF-001 | `shared_ptr<const QHash>`; setter replaces the pointer on the GUI thread, workers hold snapshots; no locks (§5.2). |
| REQ-NF-002 | One hash lookup per directory entry, no added syscalls (§5.5). |
| REQ-C-001 | `UserDirsParser::iconName()` is the only XDG name source; `PlaceList` is the only Home name source; `"user-home"` removed from `PlacesModel` (§4.1, §4.2). |
| REQ-C-002 | Existing `IconNameRole` returns the prepended chain (§3); no new role. |
| REQ-C-003 | Only a C++ setter is added to `DirectoryModel`; no `Q_INVOKABLE`, `Q_PROPERTY`, signal or role (§4.4). |
| REQ-C-004 | One small new header/source pair (no QObject, no thread), one setter, one wiring line, one lookup (§2). |
| REQ-C-005 | All chain construction stays in `IconNameResolver`, extended by one defaulted argument (§4.3). |
