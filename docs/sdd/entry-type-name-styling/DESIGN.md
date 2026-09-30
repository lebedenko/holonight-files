# Entry-Type Name Styling — Design

Status: Draft
Spec: `docs/sdd/entry-type-name-styling/SPEC.md`

## Spec conflicts found

Review corrections: matched symlink runs are bold and accent-cyan while remaining italic;
unmatched symlink runs retain normal weight and secondary color. REQ-F-008's acceptance now
agrees with REQ-F-017. REQ-F-013 permits one unconditional lstat() per entry; REQ-NF-001's
acceptance preserves target-based directories-first sorting. The scope constraint permits
supporting tests and documentation, and the commit constraint follows AGENTS.md without
requiring a specific co-author. These corrections preserve the implemented design.

## 1. Overview

This feature adds per-entry-type name styling to `apps/files/qml/listing/DirectoryListing.qml`'s
row delegate: symlinks render italic in `textSecondary`, folders render bold in `textPrimary`,
regular files are unchanged. The fact "is this entry itself a symlink" is computed once, on the
worker thread, in `apps/files/browsing/directory_model.cpp`'s existing `readEntry()` function (one
added `::lstat()` call, reusing the one already present in the stat-failure branch), stored as a new
`bool is_symlink` member on `DirectoryEntry` (`apps/files/browsing/directory_model.h`), and exposed
to QML as a new `IsSymlinkRole`/`"isSymlink"` role. The QML delegate consumes it exactly the way it
already consumes `isDir`, `statFailed`, etc. — a `required property bool isSymlink` — and derives
three small delegate-level style properties that the existing per-character-run `Repeater`/`HnLabel`
pattern (search-match highlighting) already knows how to layer a color/weight override on top of.

No source code is written as part of this document — this is a design-only deliverable per the SDD
Stage 2 contract.

## 2. Component inventory

| File | Status | Responsibility |
|---|---|---|
| `apps/files/browsing/directory_model.h` | Modified | `DirectoryEntry` gains `bool is_symlink = false;` (data layer owns the raw fact "is this path itself a symlink," independent of what it resolves to). `Role` enum gains `IsSymlinkRole`, appended after `IsParentRole`. |
| `apps/files/browsing/directory_model.cpp` | Modified | `readEntry()` gains one hoisted `::lstat()` call feeding `entry.is_symlink`, reused by both the stat-success and stat-failure branches (replacing the failure branch's now-redundant local `lstat()`). `data()` gains a `case IsSymlinkRole:` returning `entry.is_symlink`. `roleNames()` gains `{IsSymlinkRole, "isSymlink"}`. |
| `apps/files/qml/listing/DirectoryListing.qml` | Modified | Delegate gains `required property bool isSymlink` (presentation layer owns *how* the fact renders: italic/weight/color precedence, layered under the existing search-match override). Three new delegate-level `readonly property` style values; the `filenameRun` `HnLabel`'s `font.italic`/`font.weight`/`color` bindings read them. Supporting tests and documentation may change (REQ-C-001). |
| `tests/directory_model_test.cpp` | To be extended (implementation stage, not this document) | `is_symlink`/`IsSymlinkRole` coverage for symlink-to-file, symlink-to-dir, dangling symlink (reusing the existing `PermissionFixture::dangling_link`/`broken_perm_link` fixtures in `tests/directory_fixtures.h`), and a regular file/dir baseline. |
| `tests/smoke.cpp` | To be extended (implementation stage, not this document) | `filenameRun` object-name walk (already present at `tests/smoke.cpp:1288` for search highlighting) extended to assert `font.italic`/`color`/`font.weight` per REQ-F-016 on fixture rows. |

Explicitly **not** touched, per REQ-C-001/REQ-C-002: Places sidebar, breadcrumbs, Quick Look
overlay, `DirectoryProxyModel` (no `roleNames()` override there today, so it forwards
`IsSymlinkRole` automatically — same mechanism already relied on for `IconNameRole`), sort order,
icon resolution, subtitle text, and any file in `holonight-qt` (`HnLabel` already accepts
per-instance `font.italic`/`font.weight`/`color`; nothing there needs to change).

## 3. Data flow

```
Worker thread (DirectoryModel::readEntry(), per entry, during listing/refresh — unchanged batching,
apps/files/browsing/directory_model.cpp:37)
  ::lstat(encoded)  -- NEW, one call, before the existing ::stat(encoded) --
       │
       ├─ entry.is_symlink = (lstat succeeded) && S_ISLNK(linkInfo.st_mode)
       │
       ▼
  ::stat(encoded)  -- existing call, unchanged, follows symlinks to the target --
       ├─ success ──> entry.is_dir/size/mode/modified/icon_name set from the TARGET (unchanged,
       │               REQ-NF-001) — entry.is_symlink already set above, independent of this branch
       └─ failure ──> entry.stat_failed = true; dangling-message check reuses entry.is_symlink
                       instead of its own lstat() call (was: a second, now-redundant lstat())
       │
       ▼  (existing QMetaObject::invokeMethod ... Qt::QueuedConnection batching, unchanged)
GUI thread: DirectoryModel::data(index, IsSymlinkRole) -> entry.is_symlink   (new role, §4.2)
       │  (QSortFilterProxyModel-style forwarding — no DirectoryProxyModel change, same as IconNameRole)
       ▼
QML: delegate.isSymlink  (required property bool, bound to model.isSymlink)
       │
       ▼
delegate.typeItalic / typeWeight / typeColor   (three readonly properties, computed once per
  delegate from isSymlink + isDir — §4.3)
       │
       ▼
Repeater's filenameRun HnLabel instances (one per character run):
  font.italic: delegate.typeItalic
  font.weight: modelData.matched ? Font.Bold : delegate.typeWeight
  color:       modelData.matched ? accentCyan : delegate.typeColor
```

## 4. Interfaces

### 4.1 `DirectoryEntry` field addition (`apps/files/browsing/directory_model.h`)

```cpp
struct DirectoryEntry {
  QString name;
  QString absolute_path;
  bool is_dir = false;
  qint64 size = -1;
  QDateTime modified;
  quint32 mode = 0;
  bool stat_failed = false;
  QString stat_error;
  QString icon_name;
  bool is_placeholder = false;
  bool is_parent = false;
  // REQ-F-010: true when the entry's own directory-listing path is a symlink, via lstat(), regardless
  // of whether its target resolves or what type the target is. Independent of is_dir/mode, which stay
  // target-resolved (REQ-NF-001). Default-initialized like every other bool member, so INSERT-mode
  // placeholder rows (constructed without going through readEntry(), see insertPlaceholderRow()) get
  // is_symlink = false for free — no placeholder-specific code path needed (REQ-F-005).
  bool is_symlink = false;
  bool operator==(const DirectoryEntry&) const = default;
};
```

Placed after `is_parent` (last member before `operator==`), so the defaulted `operator==`
automatically starts comparing it — no manual edit to `operator==` itself is needed, it is
`= default`. This is also the reason a name-only symlink-vs-file flip (target replaced by a
differently-typed, differently-named-nothing entry without the directory entry itself being
renamed) is caught by the existing diff-refresh machinery automatically, the same way `is_dir`
changes already are today (see §5.4 and §7).

### 4.2 `readEntry()` — hoisted `lstat()` (`apps/files/browsing/directory_model.cpp:37`)

```cpp
DirectoryEntry readEntry(const QString& path, const QString& name, const PlaceList::IconMap* places) {
  DirectoryEntry entry;
  entry.name = name;
  entry.absolute_path = QDir(path).absoluteFilePath(name);
  const auto encoded = QFile::encodeName(entry.absolute_path);

  // NEW (REQ-F-010/011/012, REQ-NF-002): one lstat() per entry, computed before the target-resolving
  // stat() call below, so both branches can read entry.is_symlink without a second syscall.
  struct stat linkInfo{};
  entry.is_symlink = ::lstat(encoded.constData(), &linkInfo) == 0 && S_ISLNK(linkInfo.st_mode);

  struct stat info{};
  if (::stat(encoded.constData(), &info) == 0) {
    entry.is_dir = S_ISDIR(info.st_mode);
    entry.size = entry.is_dir ? -1 : info.st_size;
    entry.mode = info.st_mode;
    entry.modified =
        QDateTime::fromMSecsSinceEpoch((qint64{info.st_mtim.tv_sec} * 1000) + (info.st_mtim.tv_nsec / 1000000));
    const auto named = entry.is_dir ? namedIconFor(places, entry.absolute_path) : QString();
    entry.icon_name =
        IconNameResolver::candidateIconNames(info.st_mode, name, named).join(IconNameResolver::kChainSeparator);
  } else {
    const int error = errno;
    entry.stat_failed = true;
    entry.icon_name = IconNameResolver::genericFallbackName(false);
    // CHANGED (REQ-F-012): reuse entry.is_symlink computed above instead of a second lstat() call —
    // the old local `struct stat linkInfo` + its own ::lstat() call in this branch are removed.
    const bool dangling = (error == ENOENT || error == ENOTDIR) && entry.is_symlink;
    entry.stat_error =
        dangling ? DirectoryModel::tr("Broken symbolic link") : QString::fromLocal8Bit(std::strerror(error));
  }
  return entry;
}
```

**Placement choice — before `::stat()`, not after or inside each branch:** a single, unconditional
call site above the `if`/`else` means `entry.is_symlink` is available identically to both branches
with no duplicated logic and no risk of one branch forgetting it. Placing it after `::stat()` would
require either duplicating the call inside both branches (exactly the redundancy REQ-F-013
forbids) or restructuring the `if`/`else` to fall through to a shared tail — more change to working
code for no benefit. This also does not introduce a new class of TOCTOU race: the entry can already
change on disk between `readdir()` yielding its name and this function's `::stat()` call; adding an
`::lstat()` immediately before that `::stat()` only adds one adjacent instant to an already-inherent
window, not a new race class.

`readParentEntry()` (`directory_model.cpp:68`) calls `readEntry(parentPath, ".", nullptr)` and then
overwrites `name`/`absolute_path`/`is_parent` — it does not touch `is_symlink`, so the `".."` row
inherits whatever `readEntry()` computed for the parent directory's own path. In practice this is
almost always `false` (a directory's own path is essentially never itself a dangling/symlink race at
this point), which is why REQ-F-004 states the `".."` row renders bold as a fact rather than as a
case this design special-cases — no `".."`-specific code is added. `syntheticParentEntry()`
(`directory_model.cpp:204`, used only when the parent itself fails to stat) never sets `is_symlink`
either, so it gets the struct's default `false`, consistent with the same reasoning.

### 4.3 `Role` enum, `data()`, `roleNames()` (`apps/files/browsing/directory_model.h` / `.cpp`)

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
  IconNameRole,
  IsParentRole,
  IsSymlinkRole,  // NEW — appended last, REQ-F-014
};
```

These values are `Qt::UserRole + N` runtime enum constants used only in-process (`data()`'s
`switch`, `roleNames()`'s hash, and QML's role-name lookup) — nothing in this repository persists
or serializes them (no `QSettings`, no on-disk cache keyed by role integer). Renumbering would
therefore be safe, but appending after the existing last entry (`IsParentRole`) is strictly simpler:
it is a pure addition with a zero-line diff to every existing enumerator, and avoids having to
re-audit for any hypothetical hardcoded raw role integer elsewhere in the codebase (none were found,
but appending sidesteps the question entirely).

`data()` (`directory_model.cpp:162`) gains, alongside the existing `case IsParentRole:`:

```cpp
    case IsSymlinkRole:
      return entry.is_symlink;
```

`roleNames()` (`directory_model.cpp:194`) gains:

```cpp
      {IsSymlinkRole, "isSymlink"},
```

added to the existing brace-initializer list, next to `{IsParentRole, "isParent"}`.

### 4.4 QML delegate property + style bindings (`apps/files/qml/listing/DirectoryListing.qml`)

Delegate property (next to the existing `required property bool isDir` at line ~265):

```qml
required property bool isSymlink
```

Three new delegate-level `readonly property` values (placed alongside the existing
`editingThis` computed property at line ~273), computed once per delegate rather than once per
character run:

```qml
readonly property bool typeItalic: delegate.isSymlink
readonly property int typeWeight: delegate.isSymlink ? Font.Normal : (delegate.isDir ? Font.Bold : Font.Normal)
readonly property color typeColor: delegate.isSymlink ? HoloniightPalette.textSecondary : HoloniightPalette.textPrimary
```

The `filenameRun` `HnLabel` (line ~400-409) bindings become:

```qml
HnLabel {
    required property var modelData
    objectName: "filenameRun"
    role: HnTypographyRole.Body
    rawText: modelData.text
    textFormat: Text.PlainText
    color: modelData.matched ? HoloniightPalette.accentCyan : delegate.typeColor
    font.weight: modelData.matched ? Font.Bold : delegate.typeWeight
    font.italic: delegate.typeItalic
    Accessible.ignored: true
}
```

`color` and `font.weight` change from their current two-way (`matched` only) ternary to read the
new `delegate.type*` property instead of the old hardcoded `textPrimary`/`Font.Normal` fallback —
this is the REQ-F-007/008/017 "search overrides color+weight on matched runs, but the unmatched-run
base now comes from entry type instead of always being file-styling" change. `font.italic` is a new
binding (previously unset, defaulting to `false`); it reads only `delegate.typeItalic`, never
`modelData.matched` — this is the literal mechanism behind REQ-F-009's "italic is driven solely by
`isSymlink`, never by search/match state."

**No change needed to:** `filenameRuns`' `width` binding (line 382, depends only on
`statErrorIndicator`/`columnSpacing`, unrelated to entry type), the `Repeater`'s `model` computation
(lines 385-399, unrelated — it only splits `delegate.name` into matched/unmatched character runs,
never reasons about type), or anything else in `nameColumnField`/`contentItem` (REQ-C-001's scope
fence).

## 5. Key decisions with rationale

### 5.1 Additive `bool is_symlink`, not a repurposed `mode`

**Decision:** add a new, independent `bool is_symlink` field rather than reinterpreting the
existing `mode` (a `stat()`-resolved `st_mode`, already used for icon resolution and directories-
first sorting).

**Why:** `mode` reflects the *target's* type by design — a symlink-to-directory's `mode` has
`S_ISDIR` set, which is exactly what icon resolution and sort order need (REQ-NF-001 requires both
to stay unchanged, target-resolved). Folding symlink-ness into `mode` (e.g. checking `S_ISLNK` on a
hypothetical unresolved mode) would either require carrying a *second* mode value anyway (the raw
`lstat()` mode) defeating the point, or would break icon/sort behavior that other features already
depend on. A separate bool is the smallest change that keeps every existing consumer of `mode`
untouched.

### 5.2 `lstat()` in the worker thread is safe and cheap

**Decision:** the new `lstat()` call runs on the same worker thread as the existing `stat()` call,
inside `readEntry()`, with no new synchronization.

**Why:** `lstat()` is a single syscall with no directory traversal beyond what `stat()` already does
on the same path (both resolve all but the final path component identically; `lstat()` simply skips
following the final component if it is a symlink). It runs once per entry, on a thread that already
performs one `stat()` per entry as part of the existing batch-oriented walk
(`kBatchEntryThreshold`/`kBatchTimeThresholdMs`, `directory_model.cpp:19-20`) — doubling one
syscall to two per entry is the bounded, negligible cost REQ-NF-002/004 require, and introduces no
new threading concern since it is added to code that already runs exclusively on `thread_`/`worker_`.

### 5.3 No `holonight-qt` change needed

**Decision:** consume `HnLabel`'s existing `font.italic`/`font.weight`/`color` properties as-is;
no change to `HnLabel`, `HnAppearance`, or `HoloniightPalette`.

**Why:** these are ordinary `Text`/`Label`-inherited properties `HnLabel` already forwards
per-instance (confirmed before this design cycle began); `HoloniightPalette.textSecondary` is
already used elsewhere in this same file (e.g. the size column, line ~425) alongside
`textPrimary`/`accentCyan` (already used in the pre-existing `filenameRun` binding). Nothing new is
required from the shared primitive, satisfying REQ-C-002 by construction rather than by omission.

### 5.4 `".."` and placeholder rows need no special-casing

**Decision:** neither `readParentEntry()` nor `insertPlaceholderRow()` gains any `is_symlink`-related
code.

**Why:** `DirectoryEntry::is_symlink` default-initializes to `false` (§4.1); `insertPlaceholderRow()`
(`directory_model.cpp:273-282`) constructs its `DirectoryEntry placeholder;` via default
member-initialization and never touches `is_symlink`, so it is `false` for free (REQ-F-005).
`readParentEntry()` calls `readEntry()` on the parent path itself (§4.2), so `".."` inherits whatever
that computes — REQ-F-004 treats "the `".."` row is not a symlink" as an established fact about how
parent directories are reached, not a rule this design needs to enforce separately.

## 6. Alternatives considered and rejected

- **Computing "is this a symlink" client-side in QML from an existing property.** Rejected: no
  existing role or property exposes anything symlink-related — `isDir`/`mode` are both
  target-resolved by design (REQ-NF-001), so the fact does not exist anywhere client-side to derive
  from. It can only originate from an `lstat()` on the backend.
- **A richer `EntryKind` enum** (`File`/`Directory`/`Symlink`/`SymlinkToDirectory`/`Broken`) instead
  of a plain bool. Rejected as unnecessary: REQ-F-001..003/016's precedence rule needs exactly two
  independent booleans (`isSymlink`, `isDir`) to fully determine styling — a richer enum would encode
  the same three outcomes with more states than the spec defines (e.g. a separate "broken" state),
  contradicting REQ-F-006's explicit requirement that broken/dangling symlinks render *identically*
  to any other symlink, not distinctly. The two-bool design is the minimum that satisfies the spec
  and generalizes for free to combinations the spec does not currently distinguish.
- **A single combined style object property** (e.g. `delegate.nameStyle: {italic, weight, color}`)
  computed once per delegate, versus three separate `readonly property`s (§4.4's actual choice).
  Rejected in favor of three scalar properties: a QML object-literal-valued property is recreated
  (and every binding depending on it re-evaluated) whenever *any* of its constituent fields' bindings
  re-evaluate, and reading `delegate.nameStyle.color` from each `HnLabel` adds one property-lookup
  indirection per run for no behavioral gain over `delegate.typeColor`. Three scalar properties are
  each independently bindable/cacheable by the QML engine and read exactly like the file's existing
  style bindings.
- **Duplicating the full three-way ternary chain inline inside every `filenameRun` `HnLabel`'s own
  `color`/`font.weight`/`font.italic` bindings** (mirroring the file's existing style of inline
  ternaries, e.g. the `subtitle` binding at line 283), instead of hoisting the type-precedence part
  to delegate-level properties. Rejected: the `Repeater` instantiates one `HnLabel` per character
  run (REQ-F-018), so the *type* part of the precedence (symlink > folder > file) is identical across
  every run in a row and would be recomputed once per run for no reason; hoisting it to three
  delegate-level properties computes it once per row while still letting each `HnLabel`'s own binding
  stay a short, single-level ternary against `modelData.matched` — consistent with the file's inline-
  ternary style at the point where a real per-run decision (matched vs. not) actually exists.

## 7. Known risks / edge cases

- **Symlink loops (`ELOOP`).** `::lstat()` never follows the final path component, so a symlink loop
  cannot make `lstat()` itself fail with `ELOOP` — only the subsequent `::stat()` (which does follow
  links) can. Because `entry.is_symlink` is computed unconditionally before the `stat()` branch runs
  (§4.2), a looping symlink still correctly gets `is_symlink = true` and `stat_failed = true`; its
  `stat_error` text falls through to `strerror(ELOOP)` rather than the "Broken symbolic link" string
  (that string is reserved for `ENOENT`/`ENOTDIR`, unchanged existing behavior, out of this feature's
  scope per REQ-C-001) — but REQ-F-006's actual requirement, retaining italic/secondary-color
  styling independent of the stat failure, holds regardless of which error string is shown.
- **`lstat()` itself failing on an already-`readdir()`'d entry.** Rare (the containing directory was
  already successfully `opendir()`'d, so ordinary parent-directory permission issues are already
  ruled out) but possible under a raced deletion or an unreliable filesystem (e.g. a flaky network
  mount returning a transient I/O error). In that case `entry.is_symlink` evaluates to `false` (the
  `::lstat() == 0` half of the assignment is false, short-circuiting the whole expression), so the
  entry degrades gracefully to non-symlink (file or directory, depending on what the subsequent
  `::stat()` finds) styling rather than crashing or leaving the field uninitialized.
- **`operator==` correctness when a file is replaced by a symlink of the same name (or vice versa)
  via `refresh()`, without a full model reset.** `DirectoryModel::applyDiffEntries()`
  (`directory_model.cpp:478-495`) and `replaceParentRow()` (`directory_model.cpp:467-476`) both
  already compare old vs. newly-read `DirectoryEntry` values with `!=` (the defaulted `operator==`'s
  negation) to decide whether to emit `dataChanged()` for an existing row. Since `is_symlink` is now
  a member participating in that same defaulted comparison (§4.1), a refresh that finds the same-
  named entry now symlink-vs-not (even if every *other* field happened to stay identical) is
  correctly detected as changed and triggers `dataChanged()` — no new diff logic is needed; this is
  exactly the same mechanism that already catches `is_dir` flips today.

## 8. Requirement coverage map

| Requirement | Design element |
|---|---|
| REQ-F-001 | `typeItalic`/`typeWeight`/`typeColor` all resolve to the symlink branch first (§4.4). |
| REQ-F-002 | `typeWeight`/`typeColor`'s `isDir` branch, reached only when `isSymlink` is false (§4.4). |
| REQ-F-003 | The final `else` of each `type*` ternary — unchanged from current file/`textPrimary`/`Font.Normal` behavior (§4.4). |
| REQ-F-004 | `readParentEntry()` inherits `is_symlink = false` from `readEntry()` on the parent path; no `".."`-specific code (§4.2, §5.4). |
| REQ-F-005 | `insertPlaceholderRow()`'s default-initialized `DirectoryEntry` gets `is_symlink = false` for free (§4.1, §5.4). |
| REQ-F-006 | `lstat()` runs unconditionally before the `stat()` success/failure branch, so a dangling/broken symlink still gets `is_symlink = true` (§4.2, §7). |
| REQ-F-007 | `filenameRun`'s `color` ternary: `matched ? accentCyan : delegate.typeColor` (§4.4). |
| REQ-F-008 | `font.weight` ternary: `matched ? Font.Bold : delegate.typeWeight` — folder-bold and match-bold coincide, no conflict (§4.4). |
| REQ-F-009 | `font.italic: delegate.typeItalic` never references `modelData.matched` (§4.4). |
| REQ-F-010 | `DirectoryEntry::is_symlink`, default `false` (§4.1). |
| REQ-F-011 | Hoisted `::lstat()` feeds `is_symlink` regardless of subsequent `::stat()` outcome (§4.2). |
| REQ-F-012 | Stat-failure branch reuses `entry.is_symlink` instead of its own `lstat()` call (§4.2). |
| REQ-F-013 | Exactly one `lstat()` call site in `readEntry()`, used by both branches — no duplication (§4.2, §5.2). |
| REQ-F-014 | `IsSymlinkRole` appended to `Role`; `roleNames()` maps it to `"isSymlink"` (§4.3). |
| REQ-F-015 | Delegate `required property bool isSymlink` (§4.4). |
| REQ-F-016 | `typeItalic`/`typeWeight`/`typeColor` precedence exactly matches the spec's three cases (§4.4). |
| REQ-F-017 | `color`/`font.weight` matched-run overrides preserved verbatim; `font.italic` binding never touches `modelData.matched` (§4.4). |
| REQ-F-018 | `type*` properties are delegate-level (computed once per row), read identically by every `Repeater`-instantiated `HnLabel` (§4.4, §6). |
| REQ-NF-001 | `is_dir`/`mode`/icon resolution/subtitle/sort order untouched — `is_symlink` is a wholly new, independent field (§5.1). |
| REQ-NF-002 | One hoisted `lstat()` per entry, reused by both branches, no duplication (§4.2, §5.2). |
| REQ-NF-003 | No new setting/property/toggle introduced anywhere in this design — styling is unconditional. |
| REQ-NF-004 | One extra syscall per entry on the worker thread (§5.2); three cheap scalar QML property reads per delegate, no per-run recomputation (§4.4, §6). |
| REQ-C-001 | Changes confined to `directory_model.h`/`.cpp` and `DirectoryListing.qml` (§2, §4). |
| REQ-C-002 | No `holonight-qt` file touched; existing `HnLabel` properties consumed as-is (§5.3). |
| REQ-C-003 | Addressed at commit time, not by this document (implementation-stage concern). |
| REQ-C-004 | Addressed at commit time via `task qml-lint`/`task format-check` (implementation-stage concern). |
| REQ-C-005 | No existing test's structure is changed by this design — only `directory_model_test.cpp`/`smoke.cpp` gain new assertions (§2). |

## 9. Test plan summary

- **`tests/directory_model_test.cpp`**: new assertions for `data(index, DirectoryModel::IsSymlinkRole)`
  across a symlink-to-file, a symlink-to-directory, a regular file, and a regular directory, plus
  reuse of the existing `PermissionFixture` (`tests/directory_fixtures.h:62-66`, already builds
  `broken_perm_link`/`dangling_link`) to assert `is_symlink = true` on both the EACCES-on-target and
  ENOENT-on-target dangling cases (REQ-F-006/011/012).
- **`tests/smoke.cpp`**: extend the existing `filenameRun` object-name walk (already present at
  `tests/smoke.cpp:1288` for search-highlight assertions) with fixture rows for a symlink, a folder,
  and a regular file, asserting `font.italic`/`color`/`font` (bold) per REQ-F-016, and re-asserting
  the existing search-highlight case (REQ-F-017) still holds with the new `typeColor`/`typeWeight`
  base applied underneath the matched-run override.
- **REQ-F-013/REQ-NF-002 (lstat syscall count)**: per the spec's own acceptance text, verified via a
  benchmarking/strace-based check or call-counting test seam (mirroring the existing
  `read_error_after_for_test_`/`before_open_for_test_` pattern on `DirectoryModelTestAccess`) —
  the exact mechanism is an implementation-stage decision, not fixed by this document.
- **`task qml-lint`/`task format-check`** (REQ-C-004) and **`task test`** (REQ-C-005) are run
  unchanged in intent; no new `.qml` files are introduced, so no Taskfile/lint-script registration
  chore applies this cycle.
