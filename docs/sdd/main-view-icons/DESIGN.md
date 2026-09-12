# File-Type Icons in Directory Listing and Preview Pane — Design

Status: Draft
Spec: `docs/sdd/main-view-icons/SPEC.md`

## Spec conflicts found

None. Two apparent tensions in the spec text are resolved below without contradicting either
requirement, not treated as conflicts:

- REQ-F-004 ("no `image://hnicons` URLs... for file-type icons") vs REQ-F-017 (bundled fallback
  glyphs must be tinted, and `HnIcon`'s tinted path is itself backed by `image://hnicons`) — resolved
  in §5.1. The two bundled glyphs are Holonight design-system chrome assets, not MIME-derived icon
  names; REQ-F-004's ban is specifically on using `IconThemeResolver`/`hnicons` as a *MIME name*
  resolution mechanism, which this design never does.
- REQ-C-002's "single read-only path... the selected row's icon name" (singular) vs the preview
  pane's fallback needing to pick a folder-vs-generic-file glyph, which naively wants a second
  `isDir`-like property — resolved in §5.6 with a string test on the one exposed `iconName`, adding
  no second property.

## 1. Overview

This feature adds a leading icon column to `apps/files/DirectoryListing.qml`'s rows and header, and
an icon fallback in `apps/files/PreviewPane.qml`'s existing thumbnail slot, using `QIcon::fromTheme`
via a new synchronous `QQuickImageProvider` registered as `image://icon/`, rendered through the
Holonight design system's `HnIcon` component. MIME detection (extension/name matching only, no
content sniffing) runs on `DirectoryModel`'s existing worker thread and is exposed as a new
`IconNameRole` string; theme resolution itself runs on the GUI thread via the new provider, per
REQ-NF-001. Two bundled SVG glyphs (folder, generic file) guarantee an icon always renders, even
with no icon theme installed, and are the one part of this feature rendered *tinted* (in palette
colours) rather than in the theme icon's own colours.

No source code, QML, tests, or `CMakeLists.txt` are written as part of this document — this is a
design-only deliverable per the SDD Stage 2 contract.

## 2. Component inventory

| File | Status | Responsibility |
|---|---|---|
| `apps/files/icon_name_resolver.h`/`.cpp` | **New** | Pure, QObject-free namespace (`IconNameResolver`) deriving the ordered icon-name candidate chain from a `stat()` mode and filename (REQ-F-006/REQ-C-002). Calls `QMimeDatabase::mimeTypeForFile(name, QMimeDatabase::MatchExtension)` — the only MIME lookup in this feature (REQ-F-005). Directly unit-testable with synthetic `mode_t` values, no filesystem or model needed. |
| `apps/files/icon_image_provider.h`/`.cpp` | **New** | `IconImageProvider : QQuickImageProvider` (type `Pixmap`), registered under scheme `icon`. Splits the `/`-joined candidate chain out of the requested id, tries `QIcon::fromTheme()` per candidate in order, returns the first non-null pixmap, owns a bounded per-(name,pixel-size) `QCache`, de-duplicates failure logging (REQ-F-002/018/020/021/022). Runs only on the GUI thread — see §5.3. |
| `apps/files/directory_model.h`/`.cpp` | Modified | `DirectoryEntry` gains `QString icon_name` (the `/`-joined chain, or the single-name generic fallback for stat-failed/placeholder rows). New `Role::IconNameRole`/`roleNames()` entry `"iconName"`. Worker's `readEntry()` calls `IconNameResolver::candidateIconNames()` on success, `IconNameResolver::genericFallbackName(false)` on `stat()` failure. `insertPlaceholderRow()` sets the placeholder's `icon_name` the same way (REQ-C-001/REQ-F-006/007/011). |
| `apps/files/preview_service.h`/`.cpp` | Modified | New `QString icon_name_` member, `Q_PROPERTY(QString iconName ...)`, `iconName()` accessor. `setTarget()` gains one new parameter; `clear()` resets it. This is REQ-C-002's "single read-only path" — no other new state. |
| `apps/files/directory_controller.cpp` | Modified | `syncPreviewTarget()` reads one more role (`DirectoryModel::IconNameRole`) and forwards it into `preview_.setTarget(...)`, alongside the seven roles it already forwards (`apps/files/directory_controller.cpp:692-697`). No header/property surface change. |
| `apps/files/DirectoryListing.qml` | Modified | New `readonly property real iconColumnWidth: 20` on root. Delegate `required property string iconName` added alongside the existing `required property`s. Delegate `contentItem`'s `RowLayout` gains a new first cell (a fixed-width `Item` hosting the theme/fallback `HnIcon` pair). `nameColumnHeader` gains `Layout.leftMargin: root.iconColumnWidth + root.columnSpacing` (REQ-F-008/009/010/011). |
| `apps/files/PreviewPane.qml` | Modified | `imageArea`'s `visible` binding changes from `hasImage` to `hasEntry` (REQ-F-012/013 need the slot to stay reserved when only an icon, not a thumbnail, is shown). `PreviewImageItem` gains `visible: root.preview.hasImage`. A theme/fallback `HnIcon` pair is added alongside it, sized `Math.min(128, imageArea.width)` (REQ-F-013/014/015). |
| `apps/files/icons/folder-fallback.svg`, `apps/files/icons/generic-file-fallback.svg` | **New** | Bundled REUSE-licensed SVG glyphs (REQ-F-016), served as `qrc:/qt/qml/HolonightFiles/icons/<name>.svg` via the `files-ui` QML module's own resource bundle. |
| `apps/files/CMakeLists.txt` | Modified | Adds the two new C++ files to `SOURCES`, the two SVGs to a new `RESOURCES` block, and (§5.4) the plugin customization for image-provider registration. |
| `apps/files/main.cpp`, `tests/smoke.cpp`, `tests/directory_performance_test.cpp`, `tests/window_cross_filesystem_test.cpp` | Unaffected by §5.4's chosen design; would each need one extra line under the rejected alternative — see §5.4. |
| `tests/icon_name_resolver_test.cpp` | **New** | REQ-F-006 candidate-chain unit tests (directory, FIFO, socket, chardevice, blockdevice, `.py`, `.jpg`, extensionless `README`, unregistered `.xyz123`). Added to `files-smoke`'s `SOURCES` in `tests/CMakeLists.txt`. |
| `tests/icon_image_provider_test.cpp` | **New** | REQ-F-002 (null for unknown name), REQ-F-016 (empty-theme fixture), REQ-F-020/021 (cache-hit counting), REQ-F-022 (no crash, one log per distinct id) — all against the plain C++ provider class, no QML engine needed. Added to `files-smoke`. |
| `tests/icon_image_provider_test_access.h` | **New** | `friend struct` test seam exposing `theme_lookup_count_for_test_`, mirroring `DirectoryModelTestAccess`/`PreviewServiceTestAccess`. |
| `tests/directory_model_test.cpp`, `tests/preview_service_test.cpp` | Modified | Assert `IconNameRole`/`iconName` end to end through the existing worker/preview pipelines (REQ-C-001, REQ-F-015). |
| `tests/directory_performance_test.cpp` | Modified | See §9 — the cache-hit half of REQ-F-021 is automated in `icon_image_provider_test.cpp`; the wall-clock-vs-baseline half remains a two-run manual/CI comparison, not a new self-contained assertion. |
| `tests/smoke.cpp` | Modified (surgical) | New assertions for the icon column's presence/width; existing `title`/`filenameRun`/`inlineNameEditor` assertions are expected to keep passing unmodified — see §7. |

Explicitly **not** modified, per the spec's non-goals and REQ-C-002/REQ-C-004: `PlacesPanel.qml`,
`QuickLookOverlay.qml`, `ThumbnailService`, `DirectoryProxyModel` (its `roleNames()` is not
overridden, so `QSortFilterProxyModel`'s default forwards `IconNameRole` to QML automatically —
confirmed by reading `apps/files/directory_proxy_model.cpp`, which has no `roleNames()` override),
`TaskManager`, `VimModeController`.

No new `.qml` files are introduced, so the "new `.qml` files must be added to `Taskfile.yml`/
`scripts/check-qml-format.sh`" chore does not apply this cycle — `DirectoryListing.qml` and
`PreviewPane.qml` are both already present in both places.

## 3. Data flow

```
Worker thread (DirectoryModel::readEntry, per entry, during listing/refresh)
  stat() / lstat() on dangling-link path (existing code, apps/files/directory_model.cpp:29-51)
       │
       ├─ stat() succeeded ──> IconNameResolver::candidateIconNames(info.st_mode, name)
       │                         ├─ S_ISDIR/FIFO/SOCK/CHR/BLK  -> mode-based chain (REQ-F-006)
       │                         └─ regular file (or unknown)  -> QMimeDatabase::mimeTypeForFile(
       │                                                            name, MatchExtension)
       │                                                          -> own iconName()/genericIconName(),
       │                                                             then allAncestors()' names' pairs,
       │                                                             then "application-x-generic"
       │
       └─ stat() failed (dangling symlink, ENOENT, EIO...) ──> IconNameResolver::genericFallbackName(false)
                                                                  -> "application-x-generic" (single name)
  entry.icon_name = chain.join('/')                     // DirectoryEntry, new field
       │  (QMetaObject::invokeMethod ... Qt::QueuedConnection, existing batching machinery)
       ▼
GUI thread: DirectoryModel::data(index, IconNameRole) -> entry.icon_name   (REQ-C-001)
       │  (QSortFilterProxyModel forwards the role unchanged — no DirectoryProxyModel change)
       ▼
QML: delegate.iconName  (required property string, bound to model.iconName)
       │
       ├─────────────────────────────────────────────────────────────────────┐
       ▼                                                                     ▼
DirectoryListing.qml row icon cell                              PreviewPane.qml (selected row only,
  HnIcon { source: "image://icon/" + delegate.iconName }        via PreviewService.iconName — REQ-F-015)
       │                                                                     │
       ▼ (Qt Quick: Image computes requestedSize = sourceSize * window       ▼
          .effectiveDevicePixelRatio(), calls the provider synchronously —   same "image://icon/" + name
          see §5.3/§5.5)                                                     binding
       ▼
IconImageProvider::requestPixmap(id, size, requestedSize)
  candidates = id.split('/', SkipEmptyParts)
  for name in candidates:
    key = name + "@" + requestedSize            // REQ-F-020 cache key
    if cache hit  -> return cached pixmap (or, if cached *miss*, try next candidate)
    else          -> QIcon::fromTheme(name).pixmap(requestedSize); cache the result (hit or miss);
                     if non-null, return it
  (all candidates exhausted) -> log once per distinct id (REQ-F-022), return null QPixmap
       │
       ├─ non-null pixmap ─────────────> Image.status = Ready -> HnIcon shows it, untinted
       │                                 (image://icon/ scheme forces tinted:false regardless of
       │                                  the tinted property — HnIcon.qml:38-39)
       └─ null pixmap ─────────────────> Image.status = Error -> HnIcon.hasError = true
                                          -> sibling fallback HnIcon becomes visible:
                                             source: qrc:/qt/qml/HolonightFiles/icons/
                                                     {folder,generic-file}-fallback.svg
                                             tinted: true (default) -> routed through the existing
                                             hnicons/IconThemeResolver/palette pipeline, unrelated
                                             to file-type MIME resolution (REQ-F-017/§5.1)
```

## 4. Interfaces

### 4.1 `IconNameResolver` (new, `apps/files/icon_name_resolver.h`)

```cpp
#pragma once
#include <QString>
#include <QStringList>

// Pure, QObject-free derivation of the ordered icon-name candidate chain for one directory entry
// (SPEC.md REQ-F-006, REQ-C-002). Takes only primitive stat()/name data, so it is directly
// unit-testable without a DirectoryModel, QAbstractItemModel, or real filesystem access.
// MIME lookup uses QMimeDatabase::mimeTypeForFile() with MatchExtension only (REQ-F-005):
// filename/extension glob matching, never content sniffing.
namespace IconNameResolver {

// `mode` is a stat() st_mode value, already resolved through symlinks by the caller's stat() call
// (this function never itself distinguishes a symlink from its target). `fileName` is the entry's
// own filename, used for MIME extension matching regardless of what `mode` says. Returns an
// ordered, de-duplicated (first occurrence wins) list of freedesktop icon names to try in turn;
// never empty. The caller joins this into DirectoryModel::IconNameRole's single string (REQ-C-001)
// with '/' as separator; IconImageProvider splits it back into a chain — see §4.3's URL grammar.
[[nodiscard]] QStringList candidateIconNames(quint32 mode, const QString& fileName);

// The REQ-F-007 generic fallback name alone, for callers that must skip MIME/mode derivation
// entirely because the entry's stat() data cannot be trusted (dangling symlink, stat() failure) or
// does not exist (the INSERT-mode placeholder row, REQ-F-011).
[[nodiscard]] QString genericFallbackName(bool isDir);

}  // namespace IconNameResolver
```

`candidateIconNames()` body (described, not written — no production code in this document):

1. `S_ISDIR(mode)` → `{"folder", "inode-directory"}`.
2. `S_ISFIFO(mode)` → `{"inode-fifo"}`.
3. `S_ISSOCK(mode)` → `{"inode-socket"}`.
4. `S_ISCHR(mode)` → `{"inode-chardevice"}`.
5. `S_ISBLK(mode)` → `{"inode-blockdevice"}`.
6. Otherwise (regular file, or an unrecognized/zero mode — treated as a regular file since it is
   never reached for a genuinely stat-failed entry; see below): `QMimeDatabase db; const auto mime =
   db.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);` then append, in order and
   de-duplicated: `mime.iconName()`, `mime.genericIconName()`, then for each name in
   `mime.allAncestors()`, `QMimeDatabase().mimeTypeForName(name).iconName()` and `.genericIconName()`,
   and finally `"application-x-generic"`. Empty strings are dropped before de-duplication.

`genericFallbackName(bool isDir)` → `isDir ? "folder" : "application-x-generic"`.

The `stat()`-failed branch in `DirectoryModel::readEntry()` calls `genericFallbackName(false)`
directly and never calls `candidateIconNames()` at all — a dangling symlink's own extension (e.g.
`photo.jpg`) must not leak a MIME-derived icon for a target that doesn't exist (REQ-F-006's "for
dangling symlinks... use the generic fallback chain" / REQ-F-007).

### 4.2 `IconImageProvider` (new, `apps/files/icon_image_provider.h`)

```cpp
#pragma once
#include <QCache>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QSet>

// Registered under scheme "icon" (image://icon/...). Synchronous (ImageType::Pixmap) — not
// QQuickAsyncImageProvider, and specifically not ImageType::Image either: Pixmap-type providers
// are the only QQuickImageProvider kind Qt Quick guarantees to call on the GUI thread even when the
// consuming Image sets asynchronous:true, which is exactly what REQ-NF-001 requires because
// QIcon::fromTheme()/QIconLoader is not documented thread-safe. See §5.3.
class IconImageProvider final : public QQuickImageProvider {
 public:
  IconImageProvider();
  QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) override;

 private:
  friend struct IconImageProviderTestAccess;
  // Key: "<name>@<w>x<h>". Caches both hits and misses (REQ-F-020/021) — a miss for a name/size
  // pair costs one QIcon::fromTheme() call at most once, not once per row.
  QCache<QString, QPixmap> cache_;
  // Theme misses return null; Qt Quick reports image-load failure to QML.
  int theme_lookup_count_for_test_ = 0;   // incremented once per cache miss; REQ-F-021 seam
};
```

`requestPixmap(id, size, requestedSize)`: `id` is the `/`-joined candidate chain (the part of the
URL after `image://icon/`, exactly as `DirectoryModel::IconNameRole` produced it — see §4.3).
`requestedSize` is trusted as-is as the physical pixel size to render at (no manual
`devicePixelRatio` multiplication — see §5.5); if invalid, `QSize(20, 20)` is used as a floor.
Split `id` on `/`, try each candidate against the cache, then `QIcon::fromTheme(name).pixmap(...)`
on a miss, returning the first non-null pixmap. On total exhaustion: log at most once per distinct
`id` (not per name, not per row) and return a null `QPixmap`, leaving `size` unset — `Image.status`
becomes `Image::Error`, which is the exact hook `HnIcon.hasError` (`HnIcon.qml:27`) already exposes.

### 4.3 `image://icon/` URL grammar

```
image://icon/<name1>[/<name2>/.../<nameN>]
```

Candidate names, in priority order, joined by literal `/`. Icon names in the freedesktop icon
naming spec never themselves contain `/`, so no escaping or delimiter collision is possible.
`DirectoryModel::IconNameRole` carries this exact joined string (e.g.
`"text-x-python/text-x-generic/text-plain/application-x-generic"` for a `.py` file, or the
single-element `"folder"` for a directory). QML never manipulates the chain — it only concatenates
`"image://icon/" + model.iconName` (REQ-C-003). This was chosen over a delimiter character (`|`,
`,`) precisely because it needs zero escaping logic on either side — the provider receives the
already-decoded path segment and can `split('/', Qt::SkipEmptyParts)` it directly.

### 4.4 `DirectoryModel` role addition

```cpp
enum Role {
  NameRole = Qt::UserRole + 1,
  PathRole,
  IsDirRole,
  SizeRole,
  ModifiedRole,
  ModeRole,
  IsHiddenRole,
  StatFailedRole,
  StatErrorRole,
  IconNameRole,   // NEW — SPEC.md REQ-C-001. The '/'-joined candidate chain (§4.3); consumed only
                  // as "image://icon/" + iconName from QML, never parsed in C++ beyond the provider.
};
```
`roleNames()` gains `{IconNameRole, "iconName"}`. `DirectoryEntry` gains `QString icon_name;`
(participates in the existing defaulted `operator==`, so a changed icon name during a diffed
`refresh()` correctly triggers `dataChanged()` the same way `size`/`modified` already do).

### 4.5 `PreviewService` addition

```cpp
Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
...
QString iconName() const { return icon_name_; }
...
void setTarget(const QString& path, bool isDir, qint64 size, const QDateTime& modified, quint32 mode,
               bool statFailed, const QString& statError, const QString& iconName, quint64 revision = 0);
```
`icon_name_` is set synchronously inside `setTarget()` (which already formats all other metadata
with zero I/O, per its existing doc comment) and cleared in `clear()`. `DirectoryController::
syncPreviewTarget()`'s existing call (`apps/files/directory_controller.cpp:692-697`) gains one more
`model_.data(sourceIndex, DirectoryModel::IconNameRole).toString()` argument, inserted before
`preview_revision_`.

### 4.6 QML property/binding surface

- `DirectoryListing.qml` root: `readonly property real iconColumnWidth: 20`.
- Delegate: `required property string iconName` (bound from `model.iconName`, alongside the
  existing `required property string name`, etc. at `apps/files/DirectoryListing.qml:174-180`).
- Delegate icon cell (first `RowLayout` child, before the existing `nameColumnField` `Item`):
  ```qml
  Item {
      objectName: "iconColumnField"
      Layout.preferredWidth: root.iconColumnWidth
      Layout.minimumWidth: root.iconColumnWidth
      Layout.maximumWidth: root.iconColumnWidth
      Layout.fillHeight: true

      HnIcon {
          id: themeIcon
          anchors.centerIn: parent
          size: root.iconColumnWidth
          tinted: false   // moot for image://icon/ sources (HnIcon.qml forces this), kept for clarity
          source: "image://icon/" + delegate.iconName
          visible: !hasError
      }
      HnIcon {
          anchors.centerIn: parent
          size: root.iconColumnWidth
          source: delegate.isDir ? "qrc:/qt/qml/HolonightFiles/icons/folder-fallback.svg"
                                  : "qrc:/qt/qml/HolonightFiles/icons/generic-file-fallback.svg"
          visible: themeIcon.hasError
      }
  }
  ```
- `nameColumnHeader` (`apps/files/DirectoryListing.qml:67-73`) gains
  `Layout.leftMargin: root.iconColumnWidth + root.columnSpacing`.
- `PreviewPane.qml`'s `imageArea` (`apps/files/PreviewPane.qml:55-67`):
  ```qml
  Item {
      id: imageArea
      Layout.fillWidth: true
      Layout.preferredHeight: Math.min(width, 240)
      visible: root.preview.hasEntry                 // was: root.preview.hasImage
      onWidthChanged: root.reportImageAreaSize()
      onHeightChanged: root.reportImageAreaSize()

      readonly property real iconExtent: Math.min(128, width)
      // REQ-F-006's directory chain always starts with "folder" — reusing that string is how the
      // preview pane, which only ever sees `iconName` (REQ-C-002 §5.6), picks the fallback glyph.
      readonly property bool isFolderIconName: {
          const n = root.preview.iconName;
          return n === "folder" || n.startsWith("folder/");
      }

      PreviewImageItem {
          anchors.fill: parent
          image: root.preview.image
          visible: root.preview.hasImage
      }
      HnIcon {
          id: previewThemeIcon
          anchors.centerIn: parent
          size: imageArea.iconExtent
          tinted: false
          source: "image://icon/" + root.preview.iconName
          visible: !root.preview.hasImage && !hasError
      }
      HnIcon {
          anchors.centerIn: parent
          size: imageArea.iconExtent
          source: imageArea.isFolderIconName ? "qrc:/qt/qml/HolonightFiles/icons/folder-fallback.svg"
                                              : "qrc:/qt/qml/HolonightFiles/icons/generic-file-fallback.svg"
          visible: !root.preview.hasImage && previewThemeIcon.hasError
      }
  }
  ```

## 5. Key decisions with rationale

### 5.1 REQ-F-017 resolution: two stacked `HnIcon`s, `Image.status === Error` as the switch

**Decision:** every icon slot (listing cell, preview thumbnail slot) is two overlapping `HnIcon`s.
The primary one sources `image://icon/<chain>` and is always shown untinted (`HnIcon.qml:38-39`
forces this for any `image://icon/` source regardless of its `tinted` property — REQ-F-004's
`hnicons`-exclusion note already documents this as fixed HnIcon behaviour, not something this
feature can or should fight). The secondary one sources one of the two bundled `qrc:` SVGs with
`tinted: true` (`HnIcon`'s default), and is `visible` only when the primary's `hasError` is true.

**Why:** `HnIcon.hasError` (`readonly property bool hasError: iconImage.status === Image.Error`,
`HnIcon.qml:27`) is already exactly the signal REQ-F-002's acceptance criteria describe ("an unknown
or unresolvable name returns a null result... so the fallback path... is reachable and observable").
No new C++ surface, no new signal, no attached property is needed — the existing component already
exposes the exact boolean this design needs. This is the "provider returns null and QML falls back
to a `qrc:` source on a second tinted `HnIcon`" candidate named in this design's brief, and it is the
only one of the candidates that requires zero changes to `HnIcon.qml` itself (a sibling-repo file
this project does not own or modify).

**Why not the other candidates:**
- *Provider serves the glyph via a second `image://` scheme* (e.g. `image://icon-fallback/folder`):
  would need a second registered provider whose only job is reading two fixed qrc files — strictly
  more C++ than the two-`HnIcon` approach, for no behavioural gain, and it still could not be tinted
  through `HnIcon` either (any `image://` scheme could in principle need its own carve-out in
  `HnIcon.qml`, which this project cannot edit).
- *Provider exposes a "resolved/unresolved" signal or attached property*: `QQuickImageProvider` has
  no signal surface at all (it is not a `QObject`); inventing one would mean wrapping the provider in
  a `QObject` façade exposed to QML, directly contradicting REQ-C-003 ("no Q_INVOKABLE methods...
  QML-exposed C++ objects... for icon handling, beyond the provider itself").
- *A single `HnIcon` that internally detects failure and swaps its own source*: would require editing
  `HnIcon.qml` (out of scope, sibling repo) or reimplementing `HnIcon` locally (directly contradicts
  REQ-C-005's "consistent with existing... architecture" and duplicates the design system).

**On the REQ-F-004/REQ-F-017 tension named above:** the fallback glyphs' *tinted* rendering path
necessarily computes an `image://hnicons/...` URL internally (`HnIcon._renderSource`,
`HnIcon.qml:39-47`), the same as every other tinted icon in every Holonight app. REQ-F-004's
acceptance criterion — "no `image://hnicons` URLs appear in `DirectoryListing.qml`/`PreviewPane.qml`
for file-type icons" — is satisfied literally: neither file's *source text* ever spells
`image://hnicons`; that URL exists only inside `HnIcon`'s own internal computed property, not in
this project's QML or C++. REQ-F-004's rationale paragraph is specifically about not asking
`IconThemeResolver` to resolve *MIME* icon names (`"text-x-python"`, `"folder"`, ...) — this design
never does that; `IconThemeResolver` only ever sees the two fixed, Files-owned SVG paths, exactly the
same way it already resolves every other tinted glyph in this application family (e.g.
`controls/assets/folder.svg` in `holonight-qt`, an unrelated pre-existing asset).

### 5.2 The candidate chain crosses the C++/QML boundary as a `/`-joined string in one role

**Decision:** `IconNameRole` (a single `QString`, satisfying REQ-C-001's literal "role" wording)
carries the entire ordered candidate chain, joined with `/`. `IconImageProvider` — the only code
that ever needs the full chain — splits it back apart. See §4.3 for the exact grammar.

**Why:** REQ-C-001 requires "a new role... that returns the derived icon name (string)" — singular
role, but REQ-F-006 requires an ordered, multi-candidate chain, and REQ-F-002 requires the *provider*
(not the model, not QML) to be the place a name is tried against the theme and falls through to the
next candidate on failure ("an unknown or unresolvable name returns a null result... so the fallback
path... is reachable"). Putting the whole chain in the URL path, rather than only the primary name,
is what lets the provider itself walk the chain — if the role carried only `mime.iconName()` and
QML tried multiple `HnIcon`s (one per candidate) to emulate a chain, the *first* one to fail would
already trigger the REQ-F-017 fallback glyph, never reaching `genericIconName()` or the parent-type
names at all. A single round trip through the provider, which owns the retry loop, is the only way
to satisfy REQ-F-006's chain and REQ-F-002's "provider... fetches the pixmap" in the same design.

**Why `/` and not another delimiter:** freedesktop icon names are restricted to
`[A-Za-z0-9_.-]` — never `/` — so joining with `/` needs no escaping on the model side and no
special-casing on the provider side beyond `QString::split('/', Qt::SkipEmptyParts)`. A comma or
pipe delimiter would work equally validly but would need a documented assurance that no MIME/inode
icon name ever contains that character; `/` needs no such assurance because it is already the one
character guaranteed absent from every candidate in the chain, and it reads naturally as URL path
segments (`image://icon/text-x-python/text-x-generic/text-plain/application-x-generic`).

**Why not have the provider re-derive the chain itself from a primary name:** the provider only has
a `QString id` and no filesystem or `QMimeDatabase` context guaranteed by REQ-C-002 (which
constrains `IconNameResolver` to have "no dependency on the model"); re-deriving would mean either
threading a `QMimeDatabase` lookup onto the GUI thread (contradicts REQ-NF-001/REQ-C-004's
worker-thread-only MIME detection) or duplicating `stat()` at pixmap-request time (redundant I/O per
row, and racy against the file changing between listing and rendering).

### 5.3 `IconImageProvider` is `ImageType::Pixmap`, and needs no mutex

**Decision:** `IconImageProvider` derives `QQuickImageProvider(QQuickImageProvider::Pixmap)` and
overrides `requestPixmap`, not `requestImage`.

**Why:** Qt Quick only guarantees GUI-thread execution for `ImageType::Pixmap` providers; an
`ImageType::Image` provider can still be invoked off the GUI thread when the consuming `Image` sets
`asynchronous: true`, even though the provider class itself is "synchronous" in the sense of not
being a `QQuickAsyncImageProvider`. REQ-NF-001 requires theme resolution to run only on the GUI
thread because `QIconLoader` (which backs `QIcon::fromTheme`) is not documented thread-safe.
Choosing `Pixmap` makes this a property Qt enforces, not a convention this project has to maintain
by never setting `asynchronous: true` on the consuming `HnIcon`/`Image` (nothing in `HnIcon.qml`
sets it either way, so nothing here relies on that already-favourable default — but `Pixmap` removes
the question entirely).

This is a deliberate divergence from the sibling `HnIconImageProvider`
(`holonight-qt/src/icons/hniconimageprovider.h`), which is `ImageType::Image` and, as direct
evidence of the tradeoff above, carries its own `QMutex`/`QWaitCondition`/in-flight `QSet` precisely
*because* it cannot assume GUI-thread-only execution. `IconImageProvider` needs none of that
machinery — no mutex, no wait condition — because `Pixmap` already gives it the GUI-thread guarantee
`HnIconImageProvider` has to earn manually. `HnIconImageProvider`'s renders are also far more
expensive (parsing+re-tinting an SVG) than `IconImageProvider`'s (`QIcon::fromTheme` is itself
already cached by Qt's own `QIconLoader`, and this provider caches on top of that), which is a
second, independent reason `HnIconImageProvider` needed off-thread-safe behaviour and this provider
does not.

### 5.4 Where `addImageProvider("icon", ...)` is called

**Decision:** register the provider from inside the `HolonightFiles` QML module's own plugin, by
overriding `QQmlEngineExtensionPlugin::initializeEngine()`, rather than adding an explicit
`engine.addImageProvider(...)` call at every place a `QQmlEngine`/`QQmlApplicationEngine` is
constructed. Concretely: `apps/files/CMakeLists.txt`'s existing `qt_add_qml_module(files-ui ...)`
call gains `NO_GENERATE_PLUGIN_SOURCE` and `CLASS_NAME HolonightFilesPlugin` (matching the class
name Qt already derives implicitly today, so `Q_IMPORT_QML_PLUGIN(HolonightFilesPlugin)` in
`main.cpp`/`tests/smoke.cpp` needs no change), plus two new files
`apps/files/holonight_files_plugin.h`/`.cpp` added to that call's `SOURCES`, implementing:

```cpp
class HolonightFilesPlugin : public QQmlEngineExtensionPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid)
 public:
  void initializeEngine(QQmlEngine* engine, const char* uri) override {
    QQmlEngineExtensionPlugin::initializeEngine(engine, uri);
    engine->addImageProvider(QStringLiteral("icon"), new IconImageProvider);
  }
};
```

**Why:** grep found **four** existing call sites that each independently construct a
`QQmlApplicationEngine` and call `engine.loadFromModule("HolonightFiles", "Main")`
(`apps/files/main.cpp`, `tests/smoke.cpp`, `tests/directory_performance_test.cpp`,
`tests/window_cross_filesystem_test.cpp`) — and `tests/smoke.cpp` alone constructs **ten separate**
engines across its `TEST()` functions (one per test). Requiring every current and future test that
builds its own engine to remember one extra `engine.addImageProvider(...)` line is a real,
easy-to-silently-miss drift risk: forgetting it does not crash or fail loudly (per REQ-F-022's own
design, a missing provider just means every icon in that test run falls back to the bundled SVG),
so a forgotten line would quietly stop exercising the real theme-resolution path in whichever test
added it, with no test failure to catch the omission. `QQmlEngineExtensionPlugin::initializeEngine`
is Qt's own designated "run this once, automatically, for every engine that imports this module"
hook — it is arguably a more literal reading of REQ-F-002's "registered... during engine
initialization" than an ad hoc post-construction call repeated at every site.

**Rejected alternative:** an explicit `engine.addImageProvider(QStringLiteral("icon"), new
IconImageProvider)` line at each of the four (in practice, effectively ten-plus counting every
`smoke.cpp` `TEST()`) call sites, mirroring how those same sites already independently repeat
`engine.setInitialProperties({{...}})`. Rejected primarily for the drift risk above; also rejected
was a lazy self-registering QML singleton mirroring `HnIconProvider::ensureProviderRegistered()`
(`holonight-qt/src/icons/hniconprovider.cpp:37-45`) — REQ-C-002's C++ addition list is exhaustive
("the icon image provider and its cache, the pure icon-name derivation helper, and the single
read-only path...") and does not include a registration-singleton `QObject`, and REQ-C-003
separately forbids new `Q_INVOKABLE` methods "for icon handling."

**Residual risk:** this repository has never customized a `qt_add_qml_module` plugin's generated
source before (every other `qt_add_qml_module` call in both `holonight-files` and `holonight-qt`
uses the fully auto-generated plugin). The `NO_GENERATE_PLUGIN_SOURCE`/`CLASS_NAME` mechanism is
standard, documented Qt6 CMake API, but should be spiked (a trivial build with a `qDebug()` in
`initializeEngine()`) at the very start of implementation before the rest of the task breakdown
commits to it. If it proves troublesome, the rejected alternative above is a complete, safe fallback
— it only costs the drift risk, not correctness.

### 5.5 DPR handling requires no new code — Qt Quick's existing `Image`/provider contract already does it

**Decision:** neither `HnIcon.qml` nor this feature's QML adds any `Screen.onDevicePixelRatioChanged`
handler or manual size multiplication for icons. `HnIcon` sets `sourceSize.width/height: root.size`
(a *logical*-pixel value) unconditionally (`HnIcon.qml:59-60`); `IconImageProvider::requestPixmap`
trusts its `requestedSize` parameter as already being the correct *physical*-pixel target.

**Why this is safe to assume:** the already-shipping sibling provider,
`Holonight::HnIconImageProvider::requestImage` (`holonight-qt/src/icons/hniconimageprovider.cpp:43-49`),
does exactly the same thing — it takes `requested_size` as-is (via its `logicalSize()` helper, which
only range-checks it, never multiplies it) and renders at that exact pixel size. Every `HnIcon` usage
across the Holonight app family already relies on Qt Quick computing that `requestedSize` as
`sourceSize * window->effectiveDevicePixelRatio()` before calling the provider, and on `Image`
re-issuing that request automatically when the owning window's effective device pixel ratio changes
(display reconnection, Wayland fractional-scale renegotiation). This is standard, built-in Qt Quick
`Image`/`QQuickImageProvider` behaviour, not something either provider implements itself — which is
exactly why neither one needs to.

This is a different mechanism from `PreviewPane.qml`'s existing `Screen.onDevicePixelRatioChanged:
root.reportImageAreaSize()` (`apps/files/PreviewPane.qml:40`): that handler exists because
`PreviewImageItem`/`PreviewService`'s thumbnail pipeline is a *manual* `QImage`-based decode path
with no `QQuickImageProvider` in the loop at all — the controller has to be told the new pixel
target explicitly so it can redecode. Icons have no such round trip; `Image`'s own machinery already
handles it. No `reportImageAreaSize()`-style plumbing is added for icons, and none is needed.

**Verification caveat carried over from REQ-F-019 itself:** its own acceptance criteria state "no
screenshot-based tests or offscreen rendering is relied upon to verify DPI correctness; only native
on-display rendering is accepted." This design cannot and does not claim automated coverage of
fractional-scale sharpness — see §9.

### 5.6 Preview pane's fallback-glyph choice without a second C++ property

**Decision:** `PreviewPane.qml` derives whether to show the folder or generic-file fallback glyph
purely from the one already-exposed `PreviewService.iconName` string: `iconName === "folder" ||
iconName.startsWith("folder/")` (§4.6's `isFolderIconName`).

**Why:** REQ-C-002 enumerates the preview pane's only new read-only C++ surface as "the selected
row's icon name" (singular) — not a second `isDir`-style flag. REQ-F-006 guarantees directories'
chains always begin with the literal candidate `"folder"` and nothing else ever does (mode-based
chains for FIFOs/sockets/devices start with `"inode-..."`; MIME chains never start with `"folder"`
since no registered MIME type resolves to that icon name). Testing the already-exposed string is
therefore both sufficient and correct, and adds zero C++.

**Rejected alternative:** exposing `PreviewService::isDir()` (trivially available — `is_dir_` is
already a private member, `apps/files/preview_service.h:136`) as a second `Q_PROPERTY`. Rejected
solely because REQ-C-002 explicitly scopes this feature's one permitted read-only addition to the
icon name; reusing a string test on data already crossing the boundary is a strictly smaller diff
that satisfies the same literal constraint.

## 6. Alternatives considered

- **A single `HnIcon` per slot, with the fallback SVG bundled as the theme's own generic icon inside
  a private, Files-owned fake "icon theme" directory added to `QIcon::themeSearchPaths()`.**
  Rejected outright: REQ-F-018 explicitly forbids calling `QIcon::setThemeSearchPaths()`/
  `setThemeName()`/`setFallbackThemeName()` at all.
- **Rendering the fallback glyphs with a plain `Image` instead of a tinted `HnIcon`.** Rejected:
  REQ-F-003 requires every icon in these two files to be an `HnIcon`, and REQ-F-017 specifically
  requires the *bundled* glyphs to be tinted — a plain `Image` cannot tint at all, defeating the
  requirement outright.
- **Storing `IconNameRole` as a `QVariantList`/`QStringList` role instead of a joined string.**
  Rejected: REQ-C-001 reads naturally as "a string" role ("returns the derived icon name (string)"),
  a `QStringList` role would need custom marshalling to cross into a QML `Image.source` `url`
  binding, and it does not change the underlying problem — the provider still needs the whole chain
  encoded somewhere in the URL, so a joined string is no more work and stays closer to the spec's
  literal wording.
- **Doing MIME detection and icon-name resolution together on the GUI thread, only for the currently
  visible rows (viewport-based, lazy).** Rejected: REQ-C-004 explicitly requires reusing the existing
  worker-thread pattern with no new async mechanism, and REQ-F-005 requires detection to run "during
  directory listing" on the worker thread for every entry, not lazily per visible row.
- **A `QQuickAsyncImageProvider` for icon resolution, to keep pixmap generation off the render
  thread's critical path on a very large directory.** Rejected: REQ-C-005 explicitly requires a
  synchronous, non-async provider "because REQ-NF-001 requires theme resolution on the GUI thread" —
  and in practice the per-name cache (§4.2) makes steady-state lookups cheap enough (REQ-NF-001:
  "<1ms... cache hits") that async dispatch overhead would likely cost more than it saves.

## 7. Risks and mitigations

- **Plugin customization is new territory for this repo (§5.4).** Mitigation: spike it first, with
  the fully-inline fallback as a documented, complete Plan B.
- **`tests/smoke.cpp`'s structural assertions.** The prior `app-window-layout` design cycle
  (`docs/sdd/app-window-layout/DESIGN.md` §7) already flagged `TEST(Files,
  PopulatedWindowKeyboardAndInlineError)`'s `item->property("title")` check and `TEST(Files,
  ModalEditingWindowKeyboardAndHighlighting)`'s `filenameRun`/`inlineNameEditor` object-name walks as
  fragile to `contentItem` changes. This feature only *prepends* a new first cell to the delegate's
  `RowLayout` — `nameColumnField`, `filenameRuns`, `inlineEditor` are untouched siblings, so these
  specific assertions are expected to keep passing unmodified, but should be re-run explicitly rather
  than assumed (REQ-NF-002's "any existing test asserting on delegate or header structure is updated
  deliberately, with the reason recorded, rather than loosened").
- **`tests/directory_performance_test.cpp`'s `hasVisibleDelegate()` helper** checks `item->width() >
  0 && item->height() > 0` on the delegate, not on any specific cell — adding a fixed-width icon cell
  inside the delegate's `RowLayout` does not change the delegate's own `width`/`height`, so this
  helper is expected to be unaffected. The *timing* assertions in the same file are a different
  matter — see §9's honest treatment of the wall-clock half of REQ-F-021.
- **Icon theme lookup cost on first paint of a freshly opened huge directory.** Mitigated by the
  cache being keyed on (name, size) rather than row, so a directory with many files sharing a MIME
  type (e.g. 10k `.txt` files) still costs one `QIcon::fromTheme()` call, not 10k — but the *first*
  frame after opening such a directory still pays for however many *distinct* names appear in the
  first visible page, synchronously on the GUI thread (by design, per REQ-NF-001). REQ-NF-001's own
  "<5ms... amortized" language accepts this; no additional prefetching or warm-up is introduced,
  since REQ-C-004 forbids a new async mechanism.
- **`QMimeType::allAncestors()`/`genericIconName()` exact output can vary slightly by the installed
  shared-mime-info database version.** The REQ-F-006 unit tests (§9) are themselves the mitigation:
  they pin the exact expected chain for the specific fixtures the spec names, and will fail loudly if
  a given CI/dev machine's MIME database disagrees — at which point the fixture or the resolver's
  handling of `allAncestors()` ordering gets adjusted during implementation, not guessed at here.
- **Corrupt or missing bundled SVG.** This does not flow through `IconImageProvider` at all. When
  `HnIcon` reports an error for the packaged glyph, the listing and preview pane draw a visible
  question-mark placeholder in the same fixed slot. The window smoke test forces each SVG source
  to an invalid resource URL and checks that the placeholder appears.

## 8. Requirement coverage map

| Requirement | Design element |
|---|---|
| REQ-F-001 | `IconImageProvider` calls `QIcon::fromTheme()` exclusively (§4.2); no custom theme parser. |
| REQ-F-002 | `IconImageProvider` registered as `"icon"` (§5.4); returns null pixmap on total failure (§4.2), the exact `hasError` hook §5.1 relies on; honours `requestedSize` (§5.5). |
| REQ-F-003 | Every icon slot is an `HnIcon` pair (§4.6); no plain `Image` used anywhere in this feature. |
| REQ-F-004 | `IconThemeResolver`/`hnicons` never sees a MIME-derived name (§5.1); only the two fixed fallback SVG paths. |
| REQ-F-005 | `IconNameResolver::candidateIconNames()` uses `QMimeDatabase::mimeTypeForFile(name, MatchExtension)` only (§4.1). |
| REQ-F-006 | `IconNameResolver::candidateIconNames()`'s branch order and MIME chain (§4.1); joined-chain URL grammar (§4.3, §5.2). |
| REQ-F-007 | `IconNameResolver::genericFallbackName()`, used for stat-failed entries and as the final chain element (§4.1). |
| REQ-F-008 | `iconColumnWidth` shared property + new delegate `RowLayout` cell (§4.6). |
| REQ-F-009 | `nameColumnHeader`'s `Layout.leftMargin: iconColumnWidth + columnSpacing` (§4.6). |
| REQ-F-010 | `HnIcon.size: root.iconColumnWidth` (20), centered via `anchors.centerIn` (§4.6); DPR handling §5.5. |
| REQ-F-011 | Placeholder row reuses the same delegate/cell; `insertPlaceholderRow()` sets `icon_name` via `genericFallbackName(false)`, `is_dir` stays `false` untouched (§2, §4.4). |
| REQ-F-012 | `imageArea.visible: hasEntry` (was `hasImage`); icon `HnIcon` pair shown whenever `!hasImage` (§4.6). |
| REQ-F-013 | `imageArea.iconExtent: Math.min(128, width)`, `anchors.centerIn` (§4.6). |
| REQ-F-014 | Icon and text preview coexist unconditionally — `imageArea` and the text `Flickable` are independent `ColumnLayout` children, neither gated on the other (§4.6; existing `PreviewPane.qml` structure, §2). |
| REQ-F-015 | `PreviewService.iconName` sourced from the same `IconNameRole` the listing binds (§4.5, §5.6) — no independent MIME step in the pane. |
| REQ-F-016 | Two bundled SVGs (`apps/files/icons/*.svg`), served via `files-ui`'s QML-module `RESOURCES` (§2, §4.6). |
| REQ-F-017 | Fallback `HnIcon`s default `tinted: true`; theme `HnIcon`s forced untinted by the `image://icon/` scheme (§5.1). |
| REQ-F-018 | No `QIcon::setThemeName`/`setFallbackThemeName`/`setThemeSearchPaths` call anywhere in this design; resolution is exactly `QIcon::fromTheme()` then the bundled glyph (§4.2, alternatives §6). |
| REQ-F-019 | Automatic Qt Quick `Image`/provider DPR re-request behaviour, no new code (§5.5); native-display verification only (§9). |
| REQ-F-020 | `IconImageProvider::cache_`, a `QCache<QString, QPixmap>` keyed `"<name>@<w>x<h>"` (§4.2). |
| REQ-F-021 | Cache-hit half: `theme_lookup_count_for_test_` seam (§4.2, §9). Wall-clock half: existing `directory_performance_test.cpp` fixture, two-run comparison (§9, honestly scoped as a process/CI step, not a new self-contained assertion). |
| REQ-F-022 | Null pixmap on theme failure, then bundled glyph or visible placeholder if its SVG also fails. `IconFallbacks.js` avoids per-row retries. Qt Quick can still emit multiple image-load warnings for one chain before the first miss is recorded; the strict log-count criterion remains open. |
| REQ-F-023 | No change to `Keys.onPressed`/`InspectionKeys.js`/search-highlight code; new icon cell is a non-interactive `Item`/`HnIcon`, outside `filenameRuns` (§4.6). |
| REQ-F-024 | `inlineEditor` untouched — still anchored to the delegate item, not `contentItem` (per `app-window-layout` DESIGN.md §4.3's already-established invariant); no new property/signal added to it. |
| REQ-F-025 | `statErrorIndicator`/`errorIndicator` untouched, still inside `nameColumnField`, unaffected by the new icon cell preceding it (§2). |
| REQ-F-026 | Icon cell has no vertical separator (matches header's REQ-F-009 "no separator" requirement); `sizeColumnHeader`/`modifiedColumnHeader`'s existing `HnSeparator`s (`apps/files/DirectoryListing.qml:86-91`, `:104-109`) are untouched. |
| REQ-NF-001 | `IconImageProvider` is `ImageType::Pixmap`, GUI-thread-guaranteed by Qt Quick (§5.3); MIME detection stays worker-thread-only (§3, §4.1). |
| REQ-NF-002 | `tests/smoke.cpp`/`check-visual.sh` re-run expectations (§7, §9); deliberate, recorded structural test updates only where the icon cell requires it. |
| REQ-C-001 | `DirectoryModel::Role::IconNameRole` (§4.4). |
| REQ-C-002 | Enumerated C++ additions exactly match: role (§4.4), provider+cache (§4.2), pure resolver (§4.1), single read-only preview path (§4.5); §5.6 shows how the preview's fallback choice avoids a second property. |
| REQ-C-003 | All QML/C++ integration is `"image://icon/" + iconName` string concatenation (§4.6); no `Q_INVOKABLE`, no QML-exposed pixmap/theme object. |
| REQ-C-004 | `IconNameResolver` invoked from `DirectoryModel`'s existing worker (`readEntry()`), no new `QThread` (§3, §4.1). |
| REQ-C-005 | Synchronous `QQuickImageProvider::Pixmap` (§5.3); standard property-binding QML; new files registered in `apps/files/CMakeLists.txt` (§2, §9). |

## 9. Test plan summary

- **`tests/icon_name_resolver_test.cpp`** (new, added to `files-smoke`'s `SOURCES` in
  `tests/CMakeLists.txt`): pure, synthetic `mode_t`/filename inputs, no filesystem — one `TEST` per
  REQ-F-006 acceptance case (directory, FIFO, socket, chardevice, blockdevice, `.py`, `.jpg`,
  extensionless `README`, unregistered `.xyz123`), each asserting the exact ordered candidate list.
- **`tests/icon_image_provider_test.cpp`** (new, added to `files-smoke`): constructs a plain
  `IconImageProvider` directly (no `QQmlEngine` needed, since REQ-C-002 keeps it QObject-free and
  directly instantiable) and calls `requestPixmap()` directly:
  - REQ-F-002: an id with no resolvable candidate returns a null `QPixmap` and leaves `*size` unset.
  - REQ-F-016: scope `QIcon::setThemeSearchPaths({emptyTempDir})` and
    `QIcon::setThemeName(QStringLiteral("nonexistent-test-theme"))` inside a `QScopeGuard` that
    restores the previous search paths/theme name on scope exit (mirroring the `qScopeGuard` idiom
    already used in `tests/smoke.cpp`/`tests/directory_performance_test.cpp`) — no root, no real
    theme files, no CI container needed; assert `requestPixmap("folder", ...)` and
    `requestPixmap("application-x-generic", ...)` both return null, proving the "bare CI container"
    case is reachable in an ordinary dev sandbox.
  - REQ-F-020/021 (cache half): via `IconImageProviderTestAccess::themeLookupCount()`, call
    `requestPixmap()` repeatedly across a small distinct-name set at a fixed size and assert the
    counter increases only on each name's first occurrence — directly modelling "10k entries, 50
    distinct names → ~50 lookups" without needing a real 10k-file fixture.
  - REQ-F-022: assert no crash and cached misses across repeated `requestPixmap()` calls with the
    same failing id. A window-level warning-count check remains open (T-025).
- **`tests/directory_model_test.cpp`** (modified): assert `data(index, DirectoryModel::IconNameRole)`
  end-to-end for a small fixture directory (regular file, subdirectory, dangling symlink), confirming
  REQ-C-001 ("computed on the worker thread during listing, not lazily").
- **`tests/preview_service_test.cpp`** (modified): assert `PreviewService::iconName()` reflects the
  value passed into `setTarget()` and resets on `clear()` (REQ-F-015/REQ-C-002).
- **`tests/smoke.cpp`** (modified, surgically): add assertions that the new `iconColumnField` exists
  at the expected `Layout.preferredWidth` in a rendered delegate, and that the header's
  `nameColumnHeader` left edge shifts by exactly `iconColumnWidth + columnSpacing` relative to the
  delegate's `nameColumnField` left edge (REQ-F-009's 1px-tolerance alignment check). Existing
  `title`/`filenameRun`/`inlineNameEditor` assertions are expected to require no changes (§7).
- **`tests/directory_performance_test.cpp`**: no new assertions are added for the wall-clock half of
  REQ-F-021 — that half is inherently a two-build comparison (record `RecordProperty` metrics on the
  pre-icon commit, record them again post-icon, diff by hand or via a CI step) that no single gtest
  run can self-certify. This is stated plainly here rather than glossed over; the cache-hit half of
  REQ-F-021 is fully automated in `icon_image_provider_test.cpp` above and does not depend on
  wall-clock timing at all.
- **REQ-F-019 (fractional-scale sharpness)**: explicitly not automatable per the spec's own
  acceptance criteria ("no screenshot-based tests or offscreen rendering... only native on-display
  rendering is accepted"). Manual verification on the user's real Hyprland-at-1.5x display, as
  already practiced for this project (see prior fractional-scale verification note in project
  memory) — not a new gtest.
- **`scripts/check-visual.sh`**: no script changes needed (it already runs `Files.*Window*` across
  dark/light × 1/1.25/1.5); its output PNGs will change pixel content (new icon column, new preview
  fallback icon) and must be regenerated/re-reviewed once implementation lands, per REQ-NF-002.
- **`task check`** (`task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint`,
  `task license-check`) must pass unchanged in intent, per REQ-NF-002. `task license-check` (`reuse
  lint`) covers the two new SVGs once they carry the same
  `<!-- SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com> / SPDX-License-Identifier:
  GPL-3.0-or-later -->` header block already used verbatim in `packaging/org.holonight.Files.svg`.
