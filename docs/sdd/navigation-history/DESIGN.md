# Navigation History Feature — Design

**Document ID:** SDD-NAV-HIST-DESIGN
**Version:** 1.0
**Date:** 2026-09-13
**Status:** Complete
**Traces to:** `docs/sdd/navigation-history/SPEC.md` (REQ-F-001..039, REQ-NF-001..006, REQ-C-001..007)

---

## 1. Overview

The feature adds a Vim-style jump list to `DirectoryController` (back/forward directory history via
Ctrl+O/Ctrl+I and two header buttons), plus an independent cursor-restore fix for plain `h`
(navigateParent) that positions the cursor on the child directory's name in the parent listing.

Both mechanisms share one piece of machinery: a **pending cursor restore** — a target entry *name*
recorded at the moment a navigation call is made, applied once the resulting asynchronous
`DirectoryModel` load settles, and silently dropped if the user moves the cursor first or a new
navigation supersedes it before that happens.

All jump-list bookkeeping (add/dedup/evict/traverse) lives in a new plain C++ value class,
`JumpList`, with no `QObject` base and no filesystem access of its own (existence checks are
injected as a predicate). `DirectoryController` owns one `JumpList` instance and is the only piece
of code that touches the filesystem or the async model.

Key bindings are delivered through **window-level QML `Shortcut` items** (the same pattern already
used for `F`/`Escape`/`Q`/`Ctrl+C` in `Main.qml`), not through `handleKey()`/`InspectionKeys.js`.
Section 5.1 explains why, including a real key-delivery hazard specific to Ctrl+I that the SPEC's
own acceptance criteria understate.

---

## 2. Components

### 2.1 New files

| File | Purpose |
|---|---|
| `apps/files/jump_list.h` / `.cpp` | `JumpList` value class: entries, current index, `recordVisit()`, `traverse()`. REQ-F-001..012, 025-027, REQ-NF-001/002/003, REQ-C-002. |
| `tests/jump_list_test.cpp` | Pure GoogleTest unit tests for `JumpList`, no QObject/QML/filesystem. |
| `tests/history_navigation_window_test.cpp` | Rendered-window tests (header buttons, focus, breadcrumb position, Ctrl+O/Ctrl+I delivery incl. Quick Look and count) — REQ-F-020, REQ-F-030..037. |
| `apps/files/icons/go-back.svg` / `apps/files/icons/go-forward.svg` | Bundled 24×24 stroke-arrow icons (SPDX GPL-3.0-or-later header, `stroke="#000000"` so `HnIcon`'s tint replaces it) — REQ-C-007. |

### 2.2 Modified files

| File | Change |
|---|---|
| `apps/files/directory_controller.h` / `.cpp` | Owns `JumpList jump_list_`; refactors `open()` into a shared `openInternal()`; adds pending-restore state; adds `canGoBack`/`canGoForward` properties and `goBack()`/`goForward()`/`navigateHistoryBack()`/`navigateHistoryForward()` invokables; wires `navigateParent()`'s independent restore. |
| `apps/files/Main.qml` | Two new window-level `Shortcut` items for Ctrl+O / Ctrl+I. |
| `apps/files/AppHeaderBar.qml` | Two `HnIconButton` instances added inside the existing `content: Item { ... }`, left of the (unmoved) breadcrumb container. |
| `apps/files/CMakeLists.txt` | Adds `jump_list.h jump_list.cpp` to `files-ui`'s `SOURCES`; adds `icons/go-back.svg icons/go-forward.svg` to the QML module `RESOURCES`. |
| `tests/CMakeLists.txt` | Adds `jump_list_test.cpp` and `history_navigation_window_test.cpp` to `files-smoke`'s sources. |

### 2.3 Unmodified but load-bearing

- `apps/files/directory_model.{h,cpp}` — batching/generation/`scanning()` semantics the restore hook depends on (see §5.2).
- `apps/files/directory_proxy_model.{h,cpp}` — name→row lookup is done by linear scan over `proxy_.rowCount()`/`entryNameAt()`, exactly as `ensureSearchCurrent()` already does; no new proxy API needed.
- `apps/files/vim_mode_controller.h`, `apps/files/task_manager.h` — read-only gating inputs (`currentMode()`, `hasPrompt()`).
- `/home/andrii/Projects/pet/holonight/holonight-qt/qml/controls/HnIconButton.qml` — used as-is; `focusPolicy` and `Accessible.name` are set at the call site, not inside the design-system component.

---

## 3. Data flow / sequences

Notation: `oi()` = `DirectoryController::openInternal()` (new private helper `open()` now delegates
to); `jl` = `jump_list_`.

### 3.1 `l` into a directory (openEntry → navigateInto → open)

1. `handleKey("l")` → `takeCount()`; `openEntry(cursor_row_)` → `navigateInto(row)` → `open(path)`.
2. `open(path)` calls `oi(path, /*fallback=*/{}, /*restoreName=*/{}, /*recordHistory=*/true)`.
3. `oi()`: captures `entryNameAt(cursor_row_)` first; removes watcher paths; `resetForNavigation()` (closes Quick Look, exits Visual/Search/Insert — REQ-F-024); `jl.recordVisit(path, capturedName)` — records the *old* directory's cursor name (REQ-F-038) and dedups/appends/evicts (REQ-F-003/005); sets `current_path_ = path`, `cursor_row_ = 0`, `pending_restore_name_ = restoreName`; `model_.load(path)`; **then** `awaiting_initial_load_ = true` (set only after `load()` returns, so any synchronous `model_.changed()` emitted from inside `load()`'s reset — possibly with `scanning()` still false — cannot apply the restore against an empty listing; see §7); `watcher_.addPath(path)`; `emit navigated(); emit changed();`. Implementation must confirm the ordering of `scanning_ = true` vs. `changed()` inside `DirectoryModel::load()` and cover it with `RestoreNotAppliedAgainstEmptyListingDuringLoadReset`.
4. `model_.load()` synchronously empties the model (`beginResetModel/endResetModel`) then starts the worker walk. Batches arrive as `rowsInserted`; each triggers `listingChanged()` (clamps `cursor_row_`, no restore effect yet).
5. On the batch where `DirectoryModel::applyBatch()` sets `scanning_ = false` (last batch, or a `directory_error`), `model_.changed()` fires *after* that flip. The connected `maybeApplyPendingRestore()` slot runs: `awaiting_initial_load_` is true, `model_.scanning()` is false → since `pending_restore_name_` is empty, target row is `0`; `setCursorRow(0)` (no-op, already 0); `awaiting_initial_load_ = false`.

### 3.2 `h` to parent, with the independent cursor fix (REQ-F-018)

1. Current directory `D = ~/Pictures`. `handleKey("h")` → `takeCount()`; `navigateParent()`.
2. `navigateParent()` reads `current_path_` **before** it changes: `basename = QFileInfo(current_path_).fileName()` (`"Pictures"`), `parent = QFileInfo(current_path_).absolutePath()` (`"~"`).
3. Calls `oi(parent, {}, /*restoreName=*/basename, /*recordHistory=*/true)`. Note both things happen here: `jl.recordVisit(parent, entryNameAt(cursor_row_))` records `D`'s *own* jump-list entry with whatever was under the cursor in `D` (an independent, REQ-F-038 concern — e.g. some unrelated file the user had selected), while `restoreName = basename` drives what row to land on once `~` loads (a REQ-F-018 concern). These two names are usually different and both requirements are satisfied simultaneously by threading them through the same call.
4. `~` loads; on settle, `maybeApplyPendingRestore()` looks for an entry named `"Pictures"` among `~`'s rows. If found, cursor goes there (REQ-F-018); if not (renamed/hidden/deleted), row 0 (REQ-F-019).
5. This restore has nothing to do with `jl`'s own stored `cursorEntryName` for `~` (if `~` had been visited before via history, that stored name is unrelated and is not consulted here — REQ-F-018's "independent of history navigation").

### 3.3 Ctrl+O with count and a missing skip

1. User typed `3` then Ctrl+O. Digits went through `handleKey("3")` (unaffected by this feature) setting `pending_count_ = 3`.
2. Ctrl+O is delivered as a window-level `Shortcut` (see §5.1), gated `enabled` by `vim.currentMode === Normal && !tasks.hasPrompt`. `onActivated: controller.navigateHistoryBack()`.
3. `navigateHistoryBack()` evaluates `traverseHistory(-1, takeCount())` — `takeCount()` returns 3 and clears the pending count (REQ-F-023) *before* `traverseHistory` runs.
4. `traverseHistory()` re-checks `hasPrompt()`/mode (defense in depth for direct test/API callers) then calls `jl.traverse(-1, 3, entryNameAt(cursor_row_), isValidDirectory)`, where `isValidDirectory = [](path){ return QFileInfo(path).isDir(); }` — the *only* filesystem access in the whole traversal path, one `stat()`-class call per candidate examined (REQ-F-028/NF-002).
5. `JumpList::traverse()` walks backward from the current index, stat-checking each candidate; a missing one is removed in place (list shrinks, `skippedPaths` grows) and the scan continues in the same direction without consuming a valid step (REQ-F-025); it stops once 3 valid steps are found or the list is exhausted (REQ-F-007/027).
6. Back in `traverseHistory()`: if `skippedPaths` is non-empty, `status_message_ = tr("Skipped missing: %1").arg(joined)` and `emit changed()` (REQ-F-026) — this happens whether or not the traversal ultimately lands anywhere (REQ-F-027).
7. If `result.moved`, `oi(result.targetPath, {}, result.restoreName, /*recordHistory=*/false)` — `recordHistory=false` because `jl`'s index/entries were already updated by `traverse()` itself; `oi()` still does the watcher/reset/`current_path_`/pending-restore/`model_.load()` work identically to §3.1.

### 3.4 Header button click

1. `HnIconButton.onClicked: controller.goBack()`. The button is only clickable when `enabled` (REQ-F-032), so gating has already passed, but `goBack()` still re-checks (see `traverseHistory()`).
2. `goBack()`: `takeCount();` (discards any stray leftover vim count so a button click is never accidentally amplified by digits typed earlier — REQ-F-033's "count 1" is literal) `traverseHistory(-1, 1);`.
3. Same `traverseHistory`/`JumpList::traverse`/`oi()` path as §3.3, with `count = 1` fixed.
4. Because `HnIconButton` instances are given `focusPolicy: Qt.NoFocus`, the click never moves `activeFocus` away from the `ListView` (REQ-F-034) — no QML wiring beyond that property is needed.

### 3.5 Pending restore cancelled by `j`

1. `h` from `~/a/b` sets `pending_restore_name_ = "b"`, `awaiting_initial_load_ = true`, starts loading `~/a`.
2. Before that load's final batch arrives, the user presses `j`. `handleCountAndMotionKeys()` calls `setCursorRow(cursor_row_ + 1)`.
3. `setCursorRow()`'s first statement (new): `if (!applying_restore_) { awaiting_initial_load_ = false; }` — an explicit, non-restore-driven call to `setCursorRow()` unconditionally cancels any pending restore (REQ-F-015). The `applying_restore_` guard exists solely so the restore's *own* internal call to `setCursorRow()` (§3.1 step 5) doesn't immediately cancel itself.
4. When `~/a`'s load later settles, `maybeApplyPendingRestore()` sees `awaiting_initial_load_ == false` and returns immediately — the cursor stays wherever `j` put it.

### 3.6 Watcher refresh after a restore has been applied

1. Restore already applied for the current directory (`awaiting_initial_load_ == false`, cursor sitting on the restored row).
2. An external process changes a file; `QFileSystemWatcher::directoryChanged` fires → `model_.refresh()`.
3. `refresh()` runs a diff walk; batches call `applyDiffEntries`/`finishDiff`, eventually setting `scanning_ = false` and emitting `model_.changed()`.
4. `maybeApplyPendingRestore()` runs again (it is connected to every `model_.changed()` emission, not just the one after `load()`), but `awaiting_initial_load_` is already `false`, so it returns immediately without touching `cursor_row_` (REQ-F-017). No `diff`-vs-`load` flag is needed from `DirectoryModel` at all — the controller's own boolean, cleared exactly once per navigation, is sufficient and simpler.

---

## 4. Interfaces / APIs

### 4.1 `JumpList` (new, `apps/files/jump_list.h`)

```cpp
struct JumpListEntry {
  QString path;
  QString cursorEntryName;
};

struct TraverseResult {
  bool moved = false;
  QString targetPath;
  QString restoreName;
  QStringList skippedPaths;  // paths removed as missing during this traversal, in removal order
};

class JumpList {
 public:
  static constexpr int kCapacity = 100;

  int size() const;
  int currentIndex() const;
  QString currentPath() const;      // entries()[currentIndex()].path, or {} if empty
  bool canGoBack() const;           // currentIndex() > 0 — no filesystem access (REQ-F-028)
  bool canGoForward() const;        // currentIndex() < size() - 1

  // REQ-F-003/004/005/038. No-op (entries/index untouched) if newPath == currentPath().
  // Otherwise captures outgoingCursorName into the entry being left, dedups newPath if present,
  // appends it, evicts index 0 if over kCapacity, and sets currentIndex() to the new last entry.
  void recordVisit(const QString& newPath, const QString& outgoingCursorName);

  // REQ-F-006/007/010/011/025-029/038. direction is -1 (back) or +1 (forward); count >= 1.
  // Captures outgoingCursorName into the entry being left, then scans in `direction`, removing
  // any candidate for which isValidDirectory(path) is false (added to result.skippedPaths,
  // uncounted) and continuing, until `count` valid steps are taken or the list end is reached.
  // Only mutates currentIndex()/entries when result.moved is true; missing entries are always
  // removed regardless of whether the overall traversal ultimately moves (REQ-F-027).
  TraverseResult traverse(int direction, int count, const QString& outgoingCursorName,
                          const std::function<bool(const QString&)>& isValidDirectory);

 private:
  QList<JumpListEntry> entries_;
  int index_ = -1;
};
```

No `Q_OBJECT`, no signals, no QML registration — satisfies REQ-NF-003 directly (constructible and
exercisable in a plain GoogleTest `TEST()`).

### 4.2 `DirectoryController` additions

```cpp
// New Q_PROPERTYs
Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY changed)
Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY changed)

bool canGoBack() const { return jump_list_.canGoBack(); }
bool canGoForward() const { return jump_list_.canGoForward(); }

// New Q_INVOKABLEs
// Header buttons (REQ-F-033): count is always 1; any pending vim count is discarded, not reused.
Q_INVOKABLE void goBack();
Q_INVOKABLE void goForward();
// Bound only from the Main.qml window Shortcuts for Ctrl+O/Ctrl+I; honors a pending vim count
// (REQ-F-007/011/023). Q_INVOKABLE only because QML must call it, not part of the button API.
Q_INVOKABLE void navigateHistoryBack();
Q_INVOKABLE void navigateHistoryForward();

private:
 void openInternal(const QString& path, const QString& fallbackReason, const QString& restoreName,
                    bool recordHistory);
 void traverseHistory(int direction, int count);
 void maybeApplyPendingRestore();  // slot, connected to model_.changed()

 JumpList jump_list_;
 QString pending_restore_name_;
 bool awaiting_initial_load_ = false;
 bool applying_restore_ = false;  // guards setCursorRow() during restore application (§3.5)
```

`open()`'s existing public signature and behavior are unchanged from the caller's perspective:

```cpp
void DirectoryController::open(const QString& path, const QString& fallbackReason) {
  openInternal(path, fallbackReason, /*restoreName=*/{}, /*recordHistory=*/true);
}
void DirectoryController::navigateParent() {
  if (current_path_.isEmpty()) return;
  const auto basename = QFileInfo(current_path_).fileName();
  openInternal(QFileInfo(current_path_).absolutePath(), {}, basename, /*recordHistory=*/true);
}
void DirectoryController::goBack() { takeCount(); traverseHistory(-1, 1); }
void DirectoryController::goForward() { takeCount(); traverseHistory(+1, 1); }
void DirectoryController::navigateHistoryBack() { traverseHistory(-1, takeCount()); }
void DirectoryController::navigateHistoryForward() { traverseHistory(+1, takeCount()); }
```

`navigateInto()`/`openEntry()` are unchanged (they call `open()`, which now routes through
`openInternal()` with an empty `restoreName` — REQ-F-016's "none (row 0) for any other open()").

### 4.3 QML: `Main.qml` Shortcuts

```qml
Shortcut {
    sequence: "Ctrl+O"
    enabled: window.controller.vim.currentMode === VimModeController.Normal && !window.controller.tasks.hasPrompt
    onActivated: window.controller.navigateHistoryBack()
}
Shortcut {
    sequence: "Ctrl+I"
    enabled: window.controller.vim.currentMode === VimModeController.Normal && !window.controller.tasks.hasPrompt
    onActivated: window.controller.navigateHistoryForward()
}
```

Two conditions must be verified during implementation (window tests in §8.3), with the stated fallback if either fails:

- **Quick Look popup:** if `QuickLookOverlay` is a modal `Popup`, Qt Quick blocks window-context shortcuts outside it. Fallback: `InspectionKeys.press()` forwards `Key_O`/`Key_I` + Control while `popup === true` to the same invokables.
- **Count survives the Ctrl press:** pressing Control alone produces a key event with empty text; confirm `InspectionKeys.press()`/`handleKey()` does not clear `pending_count_` on it (test `3`, Ctrl+O lands three entries back). Fallback: ignore modifier-only key events before `handleKey()`.

### 4.4 QML: `AppHeaderBar.qml` buttons

Added as siblings of the existing `breadcrumbContainer` inside `content: Item { ... }` (that
`Item`'s children are absolutely positioned already; `breadcrumbContainer.x` is untouched —
REQ-F-036):

```qml
HnIconButton {
    id: backButton
    objectName: "historyBackButton"
    focusPolicy: Qt.NoFocus                                    // REQ-F-034
    sizeRole: HnControlSize.Compact
    anchors.verticalCenter: parent.verticalCenter
    x: 0
    icon.source: "qrc:/qt/qml/HolonightFiles/icons/go-back.svg"  // REQ-C-007, bundled
    enabled: root.controller.canGoBack
             && root.controller.vim.currentMode === VimModeController.Normal
             && !root.controller.tasks.hasPrompt                // REQ-F-032
    Accessible.name: qsTr("Back")                                // REQ-F-037
    onClicked: root.controller.goBack()
}
HnIconButton {
    id: forwardButton
    objectName: "historyForwardButton"
    focusPolicy: Qt.NoFocus
    sizeRole: HnControlSize.Compact
    anchors.verticalCenter: parent.verticalCenter
    x: backButton.x + backButton.width
    icon.source: "qrc:/qt/qml/HolonightFiles/icons/go-forward.svg"
    enabled: root.controller.canGoForward
             && root.controller.vim.currentMode === VimModeController.Normal
             && !root.controller.tasks.hasPrompt
    Accessible.name: qsTr("Forward")
    onClicked: root.controller.goForward()
}
```

Both buttons sit at `x ∈ [0, HnMetrics.controlHeight(Compact) * 2)`, well inside
`sidebarWidth = 200` even at `minimumWidth = 420` (REQ-F-035); `breadcrumbContainer.x` stays
`root.breadcrumbLeftInset - root.breadcrumbPadding`, unchanged (REQ-F-036).

**Icons (resolves SPEC's REQ-C-007 open item): bundled SVGs, no theme lookup.** The buttons must
look identical regardless of the user's icon theme, so two new app-owned SVGs are added:
`apps/files/icons/go-back.svg` and `apps/files/icons/go-forward.svg`, registered in the QML module
`RESOURCES` next to the existing `icons/folder-fallback.svg`/`generic-file-fallback.svg` and
referenced as `qrc:/qt/qml/HolonightFiles/icons/go-{back,forward}.svg`.

SVG format (matches the existing fallback icons and the mockup's thin arrows):
<!-- REUSE-IgnoreStart -->
- SPDX comment header (`SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com>`,
  `SPDX-License-Identifier: GPL-3.0-or-later`).
<!-- REUSE-IgnoreEnd -->
- `viewBox="0 0 24 24" fill="none" stroke="#000000" stroke-width="1.75" stroke-linecap="round"
  stroke-linejoin="round"`.
- Back: horizontal shaft plus open chevron head pointing left (e.g. `M19 12H5` and `M11 6l-6 6 6 6`);
  forward is the mirror image (`M5 12h14`, `M13 6l6 6-6 6`).

Tinting: `HnIcon` (inside `HnIconButton`) routes any non-`image://icon/` source through
`HnIconProvider`, whose `IconRenderer` replaces literal paint colors with the state color — so
normal/hover/pressed/disabled (dimmed) rendering comes from the design system with no per-state
assets. Because the source is a vector rendered at `sourceSize` = icon size × device pixel ratio,
the 1.5× fractional-scale case is handled by the existing provider path.

---

## 5. Key decisions with rationale

### 5.1 Ctrl+O/Ctrl+I delivery: window-level `Shortcut`, not `handleKey()`/`InspectionKeys.js`

**Chosen:** two `Shortcut` items in `Main.qml` (§4.3), calling dedicated `Q_INVOKABLE`s that
internally re-derive the count via `takeCount()`.

**Rejected:** extending `InspectionKeys.js`/`handleKey()` with synthetic tokens like `"<C-o>"`.

Rationale:

1. **Precedent already exists.** `Main.qml` already has exactly this shape for `Ctrl+C`
   ("fires regardless of which mode owns keyboard focus — including while SEARCH's own TextField
   holds it"). Ctrl+O/I have the identical requirement (REQ-F-020/021: must work regardless of
   whether focus is on the `ListView`, a delegate's inline editor, or the Quick Look popup) and the
   identical solution: a `Shortcut`'s `Qt.WindowShortcut` context is evaluated against the window,
   not the focused item, so it fires no matter which of those three currently has focus — without
   touching any of their per-item `Keys.onPressed` handlers.
2. **Quick Look's key allowlist would need touching either way.** `InspectionKeys.press()` restricts
   keys reaching `handleKey()` while a popup is open to `" "`/`"Escape"`/`"j"`/`"k"` only
   (`QuickLookOverlay.qml` passes `popup=true`). Extending `handleKey()` (option 2) would require
   widening that allowlist to admit new tokens, adding surface area to a `.js` file with fragile
   string-based gating. The `Shortcut` approach bypasses `InspectionKeys.js`/`handleKey()` entirely,
   so nothing there needs to change, and `QuickLookOverlay.qml`'s `Keys.onShortcutOverride` (via
   `InspectionKeys.overrideShortcut(event, true)`) never claims Ctrl+O/I, letting the window
   `Shortcut` win by default.
3. **Count still works.** `pending_count_`/`has_pending_count_` are accumulated by ordinary digit
   keypresses through the *existing* `handleKey()` path (digits are plain `Keys.onPressed` text,
   never a `Shortcut`) — completely unaffected by where Ctrl+O/I themselves are wired. By the time
   the `Shortcut` fires, `takeCount()` reads whatever was already accumulated.
4. **Ctrl+I is matched by key code, not text.** A `Shortcut { sequence: "Ctrl+I" }` matches `Qt::Key_I` + Control; the event's text (`"\t"`) is irrelevant. Automated tests synthesize the key code directly, so one manual check on the real Hyprland session is required (§7).
5. **`Ctrl+I` and Tab-focus-chain traversal.** `Shortcut` items are resolved from the window's
   shortcut map during `QEvent::ShortcutOverride`, which runs *before* an item's default key
   handling (including any built-in Tab-based focus-chain traversal QtQuick Controls or
   `HnApplicationWindow` might perform). Because no existing `Keys.onShortcutOverride` handler in
   this app (`InspectionKeys.overrideShortcut`) claims Ctrl+O/Ctrl+I (it only accepts Space and,
   conditionally, Escape), the window `Shortcut` gets first refusal and consumes the event before
   any focus-chain code sees it.

### 5.2 Restore hook: `model_.changed()` + `!model_.scanning()`, gated by a controller-owned boolean

**Chosen:** connect a new slot to the *existing* `DirectoryModel::changed()` signal; inside it, act
only when `awaiting_initial_load_ && !model_.scanning()`.

**Rejected:** hooking `listingChanged()` (the slot already connected to `proxy_`'s
`rowsInserted`/`modelReset`/`layoutChanged`/`dataChanged`) or having `DirectoryModel` expose its
`diff` flag to distinguish load from refresh.

Rationale:

- `listingChanged()` fires **mid-batch, before `scanning_` flips to false.** Tracing
  `DirectoryModel::applyBatch()`: for the batch with `batch.finished == true`, `appendEntries()`
  (or `applyDiffEntries()`) runs — synchronously triggering `rowsInserted` and thus
  `listingChanged()` — *before* `scanning_ = false` is set later in the same function. A restore
  check inside `listingChanged()` would see `model_.scanning() == true` on the very batch that
  should trigger it, and never see it flip afterward (no further proxy signal fires once entries
  stop changing). `model_.changed()`, however, is emitted from `applyBatch()` *after* `scanning_`
  is updated on every path (both the error branch and the normal-completion branch) — by
  construction, the last time `model_.changed()` is emitted for a given walk, `scanning()` is
  already `false`, and the proxy has already processed that batch's `rowsInserted` (Qt's item-view
  signals are synchronous), so row lookups in the restore code see the final state.
- **No new signal or flag from `DirectoryModel` is needed to distinguish load from refresh.**
  `awaiting_initial_load_` is a controller-owned boolean set to `true` only inside `openInternal()`
  and cleared the first time it is consulted with `scanning() == false` (i.e., the very next
  settle). A later watcher-triggered `refresh()` also ends by emitting `model_.changed()` with
  `scanning() == false`, but by then the flag is already `false`, so the handler is a no-op
  (§3.6) — this is simpler than exposing `DirectoryModel`'s internal `diff`/`generation_` machinery
  and is exactly what REQ-F-017 requires (a load-completion vs. refresh distinction), not what the
  model itself needs to know about.

### 5.3 Restore applied only at final settle, not on the first batch where the name appears

`DirectoryProxyModel` uses `dynamicSortFilter`, so a name found mid-walk (e.g., after the first of
several batches on a 10,000-entry network directory) is at its *provisional* sorted row, which can
still shift as later batches insert entries that sort before it. Applying the restore immediately
on first sight risks landing on a row that is stale by the time the walk finishes. The design
always waits for the final settle (`scanning() == false`) before doing the name lookup, at the cost
of a restore that (for very large/slow directories only) visibly lands slightly later than the
first-paint. This is judged the right trade for correctness; see §7 for the reverse trade-off if a
future stage wants "restore as soon as visible" behavior for large-directory responsiveness.

### 5.4 Button count vs. keyboard count are two different call paths

Both `goBack()`/`goForward()` (button) and `navigateHistoryBack()`/`navigateHistoryForward()`
(keyboard) end up at the same `traverseHistory(direction, count)`, but the button path calls
`takeCount()` and discards its result (always using literal `1`), while the keyboard path uses the
`takeCount()` result directly. This is necessary because REQ-F-033 requires a click to always mean
count 1 even if the user had typed digits earlier and never pressed Ctrl+O/I — a single shared
"take-count-and-use-it" invokable would let a stray typed count leak into a later button click.

### 5.5 `setCursorRow()` cancels the pending restore unless `applying_restore_` is set

Rather than auditing every call site of `setCursorRow()` to decide whether it counts as "explicit
user motion" (REQ-F-015 names `j`/`k`/`gg`/`G`/count-motions/mouse explicitly), the design treats
*every* call to `setCursorRow()` as cancelling, except the restore's own internal call. This also
correctly cancels a pending restore if the user starts an INSERT-mode create (`o`/`O`, which calls
`setCursorRow()` to place the cursor on the new placeholder) or a SEARCH-mode jump — not explicitly
required by the SPEC, but consistent with its intent and cheaper than enumerating call sites.
`clampCursorRow()` (used by `listingChanged()` on every batch) deliberately does **not** go through
`setCursorRow()` — it mutates `cursor_row_` directly — so ordinary batch-arrival clamping during a
load never cancels the very restore that load is trying to fulfil.

---

## 6. Alternatives considered and rejected

| Alternative | Rejected because |
|---|---|
| Store restore target as a **row index** instead of a name | SPEC's own "Notes for Implementer" and REQ-F-013/014 require name-based matching because sort/filter/watcher changes shift indices; a captured row would be meaningless once entries are re-sorted or hidden files toggle. |
| `JumpList` as a `QObject` with signals | Adds QML-registration ceremony and violates REQ-NF-003's "testable without QML" more than necessary; a plain value type returned/queried synchronously by its owner is simpler and just as testable. |
| Truncate forward entries when navigating away from mid-history (classic browser behavior) | Explicitly out of scope (SPEC §2, REQ-F-003's examples show `D` retained after `open(C)` from the middle of the list) — Vim's jump list never truncates. |
| Expose `DirectoryModel`'s `diff`/`generation_` to the controller to distinguish load vs. refresh | Unnecessary coupling; the controller already knows *why* it called `load()` because it just called it — see §5.2. |
| Apply the restore optimistically on the first batch where the name is found | Rejected for correctness against `dynamicSortFilter` re-sorting as later batches land; see §5.3. |
| A single `goBack(int count = 1)` overload shared by button and keyboard | The button must ignore any pending vim count (REQ-F-033), the keyboard must honor it (REQ-F-007/023) — a shared default-argument entry point can't express "ignore existing state" vs. "consume existing state" without an extra parameter that just recreates the two-method split anyway. |
| Route Ctrl+O/I through `handleKey()`/`InspectionKeys.js` with synthetic tokens | See §5.1. |

---

## 7. Known risks & mitigations

| Risk | Mitigation | Verification |
|---|---|---|
| **Ctrl+I delivery on the real session** — `QTest` synthesizes `Key_I`+Control directly and bypasses xkbcommon, so automated tests cannot prove a physical Ctrl+I reaches the `Shortcut` as `Key_I`+Control (expected on Wayland/Hyprland; only the event *text* is `"\t"`). | Bind exactly `Ctrl+I` (no extra `Ctrl+Tab` binding — not requested). | Manual check on the user's Hyprland session during implementation; if it fails, fall back to an application-level `eventFilter` matching the raw `QKeyEvent`. |
| **Restore applied against empty listing** — `model_.load()` may emit `changed()` synchronously during its reset. | `awaiting_initial_load_` set only after `load()` returns (§3.1). | `DirectoryController.RestoreNotAppliedAgainstEmptyListingDuringLoadReset`. |
| **Modal Quick Look blocks window shortcuts** / **Ctrl press clears count** | Fallbacks in §4.3. | Window tests `QuickLookOpenThenCtrlOClosesItAndNavigates`, `CountThenCtrlOTraversesCountEntries`. |
| **Async load race**: a second navigation starts before the first's restore settles. | `pending_restore_name_`/`awaiting_initial_load_` are unconditionally overwritten by every `openInternal()` call; stale batches from the superseded walk are already discarded by `DirectoryModel`'s own `generation_` check, so only the latest navigation's completion ever reaches `maybeApplyPendingRestore()`. | Controller test: rapid double-`h` (§3.2 worked example), and open-then-click-Places-before-settle (REQ-F-016 acceptance criteria). |
| **Missing-entry traversal loop** (all candidates in a direction are missing). | `JumpList::traverse()`'s scan is bounded by list size (`candidate` moves strictly toward an edge every iteration whether or not a removal occurs) and always terminates when `candidate` runs off the end. | Unit test: all-missing list, assert `moved == false` and no timeout/hang (REQ-F-027). |
| **Quick Look focus**: `Shortcut`'s `Qt.WindowShortcut` context could in principle be pre-empted by a future `Keys.onShortcutOverride` change inside `QuickLookOverlay.qml` that starts claiming Ctrl+O/I. | None needed today (current `overrideShortcut()` only claims Space/Escape) — documented here so a future change to `QuickLookOverlay.qml` doesn't silently break history navigation while Quick Look is open. | Rendered-window test opens Quick Look, presses Ctrl+O, asserts both navigation occurred and Quick Look closed (REQ-F-024). |
| **Partial load batches / large directories**: restore intentionally waits for final settle (§5.3), so on a very large or slow (network) directory the visible restore lags first paint. | Accepted trade-off; documented as a deliberate choice, not a bug. | N/A (behavioral, not a defect) — could be revisited if a future stage needs incremental restore for huge directories. |
| **clang-tidy cognitive complexity (25)** on new/modified functions. | Every new function is single-purpose and short: `JumpList::traverse()`'s one bounded loop with two branches is the most complex addition and stays well under the limit; `openInternal()`/`traverseHistory()`/`maybeApplyPendingRestore()` are each a handful of statements, mirroring how `handleKey()` was already pre-split into three dispatchers for the same reason. | `task tidy`. |
| **`HeaderFilterRegex` gap** (project memory: `.clang-tidy`'s `HeaderFilterRegex: 'src/.*\.h$'` does not match `apps/files/*.h`) — pre-existing, not introduced by this feature, but `jump_list.h` inherits it too. | None specific to this feature; naming/other header-only clang-tidy checks simply won't fire on `jump_list.h`, same as every other header in `apps/files/`. Documented so it isn't mistaken for new-code oversight during review. | N/A. |
| **Fractional scaling of new icons** (user runs Hyprland @1.5). Bundled SVGs are rendered by `HnIconProvider` at `sourceSize` (icon size); verify crispness at 1.5×. | No new code expected; if blurry, request `sourceSize` × `Screen.devicePixelRatio` as `DirectoryListing.qml` does. | Manual visual check on the real display (project memory: fractional-scale rendering gotcha). |
| **Bundled icon tinting** — if the SVG uses a paint form `IconRenderer` doesn't rewrite (e.g. `currentColor`, CSS `style=`), the arrow renders black on the dark header. | Use literal `stroke="#000000"` attributes exactly like `folder-fallback.svg`. | Window test asserts `hnIconButtonIcon.hasError === false`; manual visual check at 1.5× on the real display. |

---

## 8. Test plan

### 8.1 `tests/jump_list_test.cpp` (new, pure unit, no QObject/filesystem/QML)

- `JumpList.FreshInstanceIsEmptyAndCannotTraverse` — REQ-F-001.
- `JumpList.RecordVisitDedupsWithoutTruncatingForwardEntries` (the `[A,B,C,D]`→`open(C)`→`[A,B,D,C]` example) — REQ-F-003.
- `JumpList.RecordVisitOfCurrentPathIsNoOp` — REQ-F-004.
- `JumpList.CapacityEvictsOldestEntryAtOneHundredOne` — REQ-F-005, REQ-C-002.
- `JumpList.TraverseBackOneStepMovesIndexAndReturnsCursorName` — REQ-F-006, REQ-F-038 (name threading).
- `JumpList.TraverseBackWithCountSkipsMissingAndLandsOnValidStep` (both `[A..F]` examples from REQ-F-007) — REQ-F-007, REQ-F-025.
- `JumpList.BackThenForwardRestoresWithoutChangingListSize` — REQ-F-008.
- `JumpList.TraverseBackAtIndexZeroDoesNothing` — REQ-F-009.
- `JumpList.TraverseForwardOneStep` / `TraverseForwardWithCountLandsOnHighestAvailable` — REQ-F-010/011.
- `JumpList.TraverseForwardAtLastIndexDoesNothing` — REQ-F-012.
- `JumpList.TraverseRemovesAllMissingInDirectionWithoutMoving` (the `[A,B]` A-deleted example) — REQ-F-027.
- `JumpList.CanGoBackAndForwardIgnoreFilesystemState` (predicate never invoked by the getters) — REQ-F-028, REQ-NF-002.
- `JumpList.TraverseInvokesPredicateOncePerExaminedCandidate` (counting predicate) — REQ-F-028, REQ-NF-002.
- `JumpList.RestoreNameCaseIsPreservedNotNormalized` — REQ-C-004 (JumpList stores/returns exact strings; case-sensitivity of the *lookup* is exercised at the controller level, §8.2).
- `JumpList.RenamedDirectoryTreatedAsMissingByPredicate` — REQ-C-005 (same mechanism as REQ-F-025, exercised with a predicate that returns false for the old path).

### 8.2 `tests/directory_controller_test.cpp` (extended)

- `DirectoryController.InitialOpenAddsFirstJumpListEntryWithEmptyCursorName` — REQ-F-002.
- `DirectoryController.OpenNavigateIntoNavigateParentAllUpdateHistory` — REQ-F-003/004 via the real controller (`DirectoryControllerTestAccess` extended with a `jumpList()` accessor).
- `DirectoryController.RestoreNotAppliedAgainstEmptyListingDuringLoadReset` — REQ-F-013 (§3.1 ordering).
- `DirectoryController.RestoreAppliedOnLoadCompletionToNamedRow` — REQ-F-013.
- `DirectoryController.RestoreFallsBackToRowZeroWhenTargetNotVisible` (hidden-file case) — REQ-F-014.
- `DirectoryController.ExplicitJCancelsPendingRestoreBeforeLoadCompletes` — REQ-F-015.
- `DirectoryController.SecondNavigationBeforeSettleReplacesPendingRestore` (double-`h` example, §3.2) — REQ-F-016.
- `DirectoryController.WatcherRefreshAfterRestoreDoesNotMoveCursor` — REQ-F-017.
- `DirectoryController.NavigateParentPositionsCursorOnChildBasename` / `...FallsBackToRowZeroWhenBasenameMissing` — REQ-F-018/019.
- `DirectoryController.HistoryNavigationInertInVisualSearchInsertModes` — REQ-F-021.
- `DirectoryController.HistoryNavigationInertWhilePromptOpen` — REQ-F-022.
- `DirectoryController.PendingCountConsumedByHistoryNavigationRegardlessOfOutcome` — REQ-F-023.
- `DirectoryController.HistoryNavigationClosesQuickLook` — REQ-F-024.
- `DirectoryController.BackSkipsMissingEntriesAndReportsSkippedStatusMessage` — REQ-F-025/026.
- `DirectoryController.AllEntriesMissingInDirectionShowsStatusAndDoesNotNavigate` — REQ-F-027.
- `DirectoryController.InaccessibleDirectoryIsTraversedNotSkippedAndKeptInHistory` — REQ-F-029.
- `DirectoryController.CursorEntryNameCapturedOnEveryNavigationAwayMethod` (parametrized over open/navigateInto/navigateParent/back/forward) — REQ-F-038.
- `DirectoryController.EmptyOrUnloadedDirectoryCapturesEmptyCursorName` (both REQ-F-039 examples) — REQ-F-039.
- `DirectoryController.TwoInstancesDoNotShareHistory` / `FreshInstanceHistoryIsEmpty` — REQ-C-001.
- `DirectoryController.HistoryPathIsCleanedAbsoluteNotSymlinkResolved` — REQ-C-003.
- `DirectoryController.GoBackAndGoForwardAlwaysUseCountOneIgnoringPendingCount` — REQ-F-033 (controller-level half; button click itself is covered in §8.3).

### 8.3 `tests/history_navigation_window_test.cpp` (new, rendered window, no `fs_isolation`)

Modeled on `window_cross_filesystem_test.cpp`'s `RenderedWindow` struct (`QQmlApplicationEngine` +
`loadFromModule("HolonightFiles", "Main")`) minus the mount-namespace machinery, since no
cross-filesystem behavior is involved:

- `WindowHistoryNavigation.BackButtonSitsLeftOfForwardButtonWithinSidebarWidth` — REQ-F-030/031.
- `WindowHistoryNavigation.ButtonsEnabledStateTracksCanGoBackForwardModeAndPrompt` (four combinations from REQ-F-032's acceptance criteria) — REQ-F-032.
- `WindowHistoryNavigation.ClickingBackButtonNavigatesLikeCtrlO` / `...ForwardButton...CtrlI` — REQ-F-033.
- `WindowHistoryNavigation.ClickingButtonLeavesFocusOnListingAndVimKeysStillWork` (click, then `QTest::keyClick` `j`, assert cursor moved) — REQ-F-034.
- `WindowHistoryNavigation.BothButtonsFitWithinSidebarAtMinimumWindowWidth` (resize to 420, assert mapped right edges ≤ breadcrumb x) — REQ-F-035.
- `WindowHistoryNavigation.BreadcrumbContainerXUnchangedAtDefaultAndMinimumWidth` (assert `breadcrumbContainer.x == appHeaderBar.breadcrumbLeftInset - appHeaderBar.breadcrumbPadding` at both widths, and that changing `sidebarWidth` moves it by the same delta as before this feature) — REQ-F-036.
- `WindowHistoryNavigation.ButtonsHaveBackAndForwardAccessibleNames` — REQ-F-037.
- `WindowHistoryNavigation.QuickLookOpenThenCtrlOClosesItAndNavigates` — REQ-F-024 (rendered-level confirmation alongside the controller-level test in §8.2).
- `WindowHistoryNavigation.CtrlOAndCtrlIKeyClicksNavigateFromListingFocus` (`QTest::keyClick` with `Key_O`/`Key_I` + Control) — REQ-F-020.
- `WindowHistoryNavigation.CtrlIDoesNotMoveFocusOrNavigateInInsertMode` — REQ-F-020/021.
- `WindowHistoryNavigation.CountThenCtrlOTraversesCountEntries` — REQ-F-007/023 via real key events.
- `WindowHistoryNavigation.HistoryButtonIconsLoadFromBundledResources` (`hnIconButtonIcon.hasError === false`, source starts with `qrc:`) — REQ-C-007.

### 8.4 Tooling (REQ-NF-005/006)

`task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint` all pass. No new
`.qml` files are introduced (only `Main.qml` and `AppHeaderBar.qml`, both already listed in
`Taskfile.yml`'s `qmlformat`/`check-qml-format.sh` file lists), so REQ-NF-006 requires no changes.

---

## 9. CMake / Taskfile / build registration changes

- **`apps/files/CMakeLists.txt`**: add `jump_list.h jump_list.cpp` to `files-ui`'s `SOURCES` list (alongside `icon_name_resolver.h icon_name_resolver.cpp`, the closest existing analog — a pure, `QObject`-free, unit-testable class); append `icons/go-back.svg icons/go-forward.svg` to the module's `RESOURCES` line. No `QML_FILES` change (no new `.qml`).
- **Licensing**: both SVGs carry SPDX headers like the existing fallback icons; check whether the repo's REUSE/licensing declaration (added in commit `7ff68fe`) needs the new files listed.
- **`tests/CMakeLists.txt`**: add `jump_list_test.cpp` and `history_navigation_window_test.cpp` to `files-smoke`'s `qt_add_executable` source list (same binary as `directory_controller_test.cpp`, `directory_performance_test.cpp`, etc. — no `fs_isolation` dependency, so no new test binary/`add_test()` needed).
- **`Taskfile.yml` / `scripts/check-qml-format.sh`**: **no changes** — both modified `.qml` files (`Main.qml`, `AppHeaderBar.qml`) are already present in both lists. (Project memory: new `.qml` files must be added manually to both; this feature adds none.)
- **`.clang-tidy`**: no changes; default cognitive-complexity threshold (25) is sufficient (§7). Note for reviewers: `HeaderFilterRegex: 'src/.*\.h$'` does not match `apps/files/jump_list.h`, consistent with every other header in this directory (pre-existing gap, not introduced here).

---

## 10. Spec deviations / clarifications

1. **REQ-F-020 verification is partly manual.** Key-code matching is implemented as specified via `Shortcut { sequence: "Ctrl+I" }`, but `QTest`-synthesized events cannot prove what the real compositor delivers; one manual Hyprland check is required (§7). No extra `Ctrl+Tab` binding is added.
2. **REQ-F-031's wording is self-contradictory as written**: "The system shall place an HnIconButton
   (forward/→ symbol) **at the far left** of AppHeaderBar, immediately to the right of the back
   button" — a button cannot be both "at the far left" and "immediately right of" another button at
   the far left. Read as intended (forward is immediately right of back; both are within the
   `sidebarWidth` region considered "the left side" of the header), which is what §4.4 implements.
3. **REQ-F-023 vs. mode/prompt gating ordering**: the SPEC doesn't say whether a count typed before
   an Escape-gated (wrong-mode) or prompt-gated Ctrl+O/I should be consumed. This design consumes it
   unconditionally (`takeCount()` is evaluated as a function argument before `traverseHistory()`'s
   internal gating check runs), matching the literal "whether or not navigation succeeds" wording
   and keeping the behavior independent of *why* it didn't succeed.
4. **REQ-C-007 resolved as bundled SVGs, not theme icons** (user decision): `icons/go-back.svg` / `icons/go-forward.svg` in the app's QML module resources, tinted by `HnIcon` (§4.4). SPEC REQ-C-007 updated accordingly.
5. **Partial-batch restore timing** (SPEC §9.1 item 3's "Stat Check Performance" and the
   Implementer Notes' silence on batching): the SPEC does not address multi-batch loads directly.
   §5.3 documents the choice (restore at final settle only) and the rejected alternative
   (restore as soon as the name first appears), since REQ-F-013's "applied exactly once per load
   completion" is compatible with either reading but the two have materially different behavior on
   large directories.

---

## 11. Traceability matrix

| Requirement | Component(s) | Test(s) |
|---|---|---|
| REQ-F-001 | `JumpList` | `JumpList.FreshInstanceIsEmptyAndCannotTraverse` |
| REQ-F-002 | `DirectoryController::open()` (via `main.cpp`'s existing startup call) | `DirectoryController.InitialOpenAddsFirstJumpListEntryWithEmptyCursorName` |
| REQ-F-003 | `JumpList::recordVisit()` | `JumpList.RecordVisitDedupsWithoutTruncatingForwardEntries` |
| REQ-F-004 | `JumpList::recordVisit()` | `JumpList.RecordVisitOfCurrentPathIsNoOp` |
| REQ-F-005 | `JumpList::recordVisit()` | `JumpList.CapacityEvictsOldestEntryAtOneHundredOne` |
| REQ-F-006 | `JumpList::traverse()`, `DirectoryController::traverseHistory()` | `JumpList.TraverseBackOneStepMovesIndexAndReturnsCursorName`, `DirectoryController.RestoreAppliedOnLoadCompletionToNamedRow` |
| REQ-F-007 | `JumpList::traverse()` | `JumpList.TraverseBackWithCountSkipsMissingAndLandsOnValidStep` |
| REQ-F-008 | `JumpList::recordVisit()`/`traverse()` (append-only invariant) | `JumpList.BackThenForwardRestoresWithoutChangingListSize` |
| REQ-F-009 | `JumpList::traverse()` | `JumpList.TraverseBackAtIndexZeroDoesNothing` |
| REQ-F-010 | `JumpList::traverse()` | `JumpList.TraverseForwardOneStep` |
| REQ-F-011 | `JumpList::traverse()` | `JumpList.TraverseForwardWithCountLandsOnHighestAvailable` |
| REQ-F-012 | `JumpList::traverse()` | `JumpList.TraverseForwardAtLastIndexDoesNothing` |
| REQ-F-013 | `DirectoryController::maybeApplyPendingRestore()` | `DirectoryController.RestoreAppliedOnLoadCompletionToNamedRow` |
| REQ-F-014 | `DirectoryController::maybeApplyPendingRestore()` | `DirectoryController.RestoreFallsBackToRowZeroWhenTargetNotVisible` |
| REQ-F-015 | `DirectoryController::setCursorRow()` (`applying_restore_` guard) | `DirectoryController.ExplicitJCancelsPendingRestoreBeforeLoadCompletes` |
| REQ-F-016 | `DirectoryController::openInternal()` | `DirectoryController.SecondNavigationBeforeSettleReplacesPendingRestore` |
| REQ-F-017 | `DirectoryController::maybeApplyPendingRestore()` (`awaiting_initial_load_`) | `DirectoryController.WatcherRefreshAfterRestoreDoesNotMoveCursor` |
| REQ-F-018 | `DirectoryController::navigateParent()` | `DirectoryController.NavigateParentPositionsCursorOnChildBasename` |
| REQ-F-019 | `DirectoryController::maybeApplyPendingRestore()` | `DirectoryController.NavigateParentFallsBackToRowZeroWhenBasenameMissing` |
| REQ-F-020 | `Main.qml` `Shortcut` sequences (key-code based, not text) | `WindowHistoryNavigation.CtrlOAndCtrlIKeyClicksNavigateFromListingFocus`, `...CtrlIDoesNotMoveFocusOrNavigateInInsertMode`, manual Hyprland check |
| REQ-F-021 | `Main.qml` `Shortcut.enabled`, `DirectoryController::traverseHistory()` | `DirectoryController.HistoryNavigationInertInVisualSearchInsertModes` |
| REQ-F-022 | `DirectoryController::traverseHistory()` | `DirectoryController.HistoryNavigationInertWhilePromptOpen` |
| REQ-F-023 | `DirectoryController::navigateHistoryBack/Forward()` (`takeCount()`) | `DirectoryController.PendingCountConsumedByHistoryNavigationRegardlessOfOutcome` |
| REQ-F-024 | `DirectoryController::openInternal()` → `resetForNavigation()` | `DirectoryController.HistoryNavigationClosesQuickLook`, `WindowHistoryNavigation.QuickLookOpenThenCtrlOClosesItAndNavigates` |
| REQ-F-025 | `JumpList::traverse()` | `JumpList.TraverseBackWithCountSkipsMissingAndLandsOnValidStep` |
| REQ-F-026 | `DirectoryController::traverseHistory()` | `DirectoryController.BackSkipsMissingEntriesAndReportsSkippedStatusMessage` |
| REQ-F-027 | `JumpList::traverse()`, `DirectoryController::traverseHistory()` | `JumpList.TraverseRemovesAllMissingInDirectionWithoutMoving`, `DirectoryController.AllEntriesMissingInDirectionShowsStatusAndDoesNotNavigate` |
| REQ-F-028 | `DirectoryController::traverseHistory()` (predicate = one `QFileInfo::isDir()` per candidate), `JumpList::canGoBack/Forward()` | `JumpList.CanGoBackAndForwardIgnoreFilesystemState`, `JumpList.TraverseInvokesPredicateOncePerExaminedCandidate` |
| REQ-F-029 | `JumpList::traverse()` (predicate only checks `isDir`, not readability) | `DirectoryController.InaccessibleDirectoryIsTraversedNotSkippedAndKeptInHistory` |
| REQ-F-030 | `AppHeaderBar.qml` `backButton` | `WindowHistoryNavigation.BackButtonSitsLeftOfForwardButtonWithinSidebarWidth` |
| REQ-F-031 | `AppHeaderBar.qml` `forwardButton` | `WindowHistoryNavigation.BackButtonSitsLeftOfForwardButtonWithinSidebarWidth` |
| REQ-F-032 | `AppHeaderBar.qml` `enabled:` bindings | `WindowHistoryNavigation.ButtonsEnabledStateTracksCanGoBackForwardModeAndPrompt` |
| REQ-F-033 | `DirectoryController::goBack/goForward()` | `WindowHistoryNavigation.ClickingBackButtonNavigatesLikeCtrlO`, `DirectoryController.GoBackAndGoForwardAlwaysUseCountOneIgnoringPendingCount` |
| REQ-F-034 | `AppHeaderBar.qml` `focusPolicy: Qt.NoFocus` | `WindowHistoryNavigation.ClickingButtonLeavesFocusOnListingAndVimKeysStillWork` |
| REQ-F-035 | `AppHeaderBar.qml` layout (§4.4) | `WindowHistoryNavigation.BothButtonsFitWithinSidebarAtMinimumWindowWidth` |
| REQ-F-036 | `AppHeaderBar.qml` (`breadcrumbContainer.x` untouched) | `WindowHistoryNavigation.BreadcrumbContainerXUnchangedAtDefaultAndMinimumWidth` |
| REQ-F-037 | `AppHeaderBar.qml` `Accessible.name` | `WindowHistoryNavigation.ButtonsHaveBackAndForwardAccessibleNames` |
| REQ-F-038 | `JumpList::recordVisit()`/`traverse()` (`outgoingCursorName`), `DirectoryController::entryNameAt()` | `DirectoryController.CursorEntryNameCapturedOnEveryNavigationAwayMethod` |
| REQ-F-039 | `DirectoryController::entryNameAt()` (bounds-checked, returns `{}`) | `DirectoryController.EmptyOrUnloadedDirectoryCapturesEmptyCursorName` |
| REQ-NF-001 | `JumpList` (bounded loops, `n ≤ 100`) | Code review |
| REQ-NF-002 | `DirectoryController::traverseHistory()`'s predicate; `JumpList::canGoBack/Forward()` | `JumpList.CanGoBackAndForwardIgnoreFilesystemState` |
| REQ-NF-003 | `JumpList` (no `QObject`, no QML) | `tests/jump_list_test.cpp` compiles/links without QML |
| REQ-NF-004 | All new/modified C++ | Code review, `task tidy`/`task format-check` |
| REQ-NF-005 | All | `task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint` |
| REQ-NF-006 | N/A (no new `.qml` files) | Manual inspection — no `Taskfile.yml` change needed |
| REQ-C-001 | `DirectoryController` owns `jump_list_` as a plain member (no static/shared state) | `DirectoryController.TwoInstancesDoNotShareHistory` |
| REQ-C-002 | `JumpList::kCapacity` | `JumpList.CapacityEvictsOldestEntryAtOneHundredOne` |
| REQ-C-003 | `DirectoryController::openInternal()` (`current_path_` unchanged assignment, unmodified from today) | `DirectoryController.HistoryPathIsCleanedAbsoluteNotSymlinkResolved` |
| REQ-C-004 | `DirectoryController::maybeApplyPendingRestore()` (`QString::operator==`, case-sensitive) | `JumpList.RestoreNameCaseIsPreservedNotNormalized` |
| REQ-C-005 | `JumpList::traverse()` (`isValidDirectory` predicate) | `JumpList.RenamedDirectoryTreatedAsMissingByPredicate`, `DirectoryController.RenamedDirectoryDetectedAsMissingOnTraversal` |
| REQ-C-006 | No history UI added anywhere | Code review |
| REQ-C-007 | `icons/go-back.svg`, `icons/go-forward.svg`, `AppHeaderBar.qml` (§4.4) | `WindowHistoryNavigation.HistoryButtonIconsLoadFromBundledResources`, visual inspection |

---

**End of Design**
