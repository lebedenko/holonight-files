# SDD Spec — entry-type-name-styling

## Overview

Display each directory entry's name in the main listing with distinct visual styling per entry type:
folders rendered bold, symlinks rendered italic with secondary text color (overriding folder-bold for symlink directories),
and regular files unchanged from current behavior. This styling is confined to the per-row name labels in `DirectoryListing.qml`
and interacts with existing search-mode match highlighting by applying type styling to the base unmatched runs while
preserving accent-cyan color and bold weight on matched runs.

---

## Functional Requirements

### Entry type styling

#### REQ-F-001 — Symlink precedence: italic + secondary color
**Ubiquitous:** When a directory entry is itself a symlink (regardless of its target or target resolution),
the entry's displayed name in the directory listing shall render in italic with `HoloniightPalette.textSecondary` color
and normal (non-bold) font weight as its base style. SEARCH-mode matched runs override color and weight
per REQ-F-007/008/017.

**Acceptance:** Given a symlink entry with `isSymlink = true` and `isDir = false` (a symlink to a regular file),
the rendered name HnLabel has `font.italic = true`, `color = textSecondary`, and `font.weight = Font.Normal`.
Given a symlink entry with `isSymlink = true` and `isDir = true` (a symlink to a directory), the rendered name HnLabel
has `font.italic = true`, `color = textSecondary`, and `font.weight = Font.Normal` (symlink styling overrides folder-bold).

#### REQ-F-002 — Folder styling: bold weight
**Ubiquitous:** When a directory entry is not itself a symlink and is a directory (where `isSymlink = false` and
`isDir = true`), the entry's displayed name shall render in bold weight with the default primary text color and
no italic.

**Acceptance:** Given a regular directory entry with `isSymlink = false` and `isDir = true`, the rendered name
HnLabel has `font.weight = Font.Bold`, `color = textPrimary`, and `font.italic = false`.

#### REQ-F-003 — Regular file styling: unchanged
**Ubiquitous:** When a directory entry is not a symlink and not a directory (where `isSymlink = false` and
`isDir = false`), the entry's displayed name shall render with normal weight, default primary text color, and
no italic, pixel-identical to current behavior.

**Acceptance:** Given a regular file entry with `isSymlink = false` and `isDir = false`, the rendered name HnLabel
has `font.weight = Font.Normal`, `color = textPrimary`, and `font.italic = false`. A visual regression test
confirming this state matches a baseline snapshot from before the feature shall pass.

### Special cases

#### REQ-F-004 — Parent ".." row styling
**Ubiquitous:** The synthetic ".." parent directory row is itself a directory entry (not a symlink),
so its displayed name shall render in bold weight per REQ-F-002.

**Acceptance:** The ".." row's name HnLabel has `font.weight = Font.Bold`, `color = textPrimary`, and `font.italic = false`.

#### REQ-F-005 — INSERT-mode placeholder row styling
**Ubiquitous:** During INSERT-mode creation (o/O create-placeholder), the placeholder row's displayed name
shall be styled according to the `isDir` state the placeholder carries at render time,
following REQ-F-001/F-002/F-003 normally; the placeholder is never a symlink.

**Acceptance:** A placeholder row for a new directory (isDir = true, isSymlink = false) renders name in bold;
a placeholder row for a new file (isDir = false, isSymlink = false) renders name with normal weight.

#### REQ-F-006 — Broken and dangling symlinks retain symlink styling
**Ubiquitous:** A symlink entry whose target cannot be reached (ENOENT, permission denied, or any other
stat() failure on the target) shall still be recognized as a symlink via lstat() and shall retain the
italic + secondary-color styling of REQ-F-001, independent of stat failure.

**Acceptance:** Given a dangling symlink entry (where lstat() succeeds and identifies a symlink, but stat()
fails on the target path), `isSymlink = true` is stored in DirectoryEntry, and the rendered name HnLabel
has `font.italic = true`, `color = textSecondary`, `font.weight = Font.Normal`.

### Interaction with search highlighting

#### REQ-F-007 — Search match color precedence
**State-driven:** During SEARCH-mode when the current cursor row has matched character runs in its name,
matched runs shall receive `color = accentCyan` (overriding the entry's base type color: symlink's secondary,
folder's primary, or file's primary), while unmatched runs shall receive the entry's base type color.

**Acceptance:** In SEARCH mode on a symlink row with one matched and one unmatched run, the matched run
has `color = accentCyan` and `font.italic = true`; the unmatched run has `color = textSecondary` and
`font.italic = true`. For a folder row, the matched run has `color = accentCyan` and `font.weight = Font.Bold`;
the unmatched run has `color = textPrimary` and `font.weight = Font.Bold`.

#### REQ-F-008 — Search match weight unchanged
**Ubiquitous:** The behavior of setting `font.weight = Font.Bold` on matched character runs during SEARCH-mode
shall not change; matched runs remain bold, and matched runs in a folder's name remain bold
(no weight conflict, as folder-bold and match-bold are identical).

**Acceptance:** In SEARCH mode on a folder row with matched runs, those runs have `font.weight = Font.Bold`
and `font.italic = false`. In SEARCH mode on a symlink row, matched runs have `font.weight = Font.Bold`
and `font.italic = true`; unmatched runs retain `Font.Normal` and `textSecondary`.

#### REQ-F-009 — Search italic driven by symlink-ness only
**Ubiquitous:** The `font.italic` property of every character run (matched or unmatched) in an entry's name
shall be driven solely by `isSymlink`, not by search mode or cursor state. Symlink entries' names are italic
in every run; non-symlinks' names are never italic in any run, regardless of match status.

**Acceptance:** In SEARCH mode on a symlink row, all character runs (matched and unmatched) have `font.italic = true`.
In SEARCH mode on a folder row, no character run has `font.italic = true`. When SEARCH mode exits and re-enters,
the italic property of each run remains unchanged.

### Backend: isSymlink field and lstat()

#### REQ-F-010 — New is_symlink field in DirectoryEntry
**Ubiquitous:** The `DirectoryEntry` struct in `apps/files/browsing/directory_model.h` shall include a new
`bool is_symlink` field, initialized to false by default, and set to true when the directory entry itself
(not its target) is identified as a symlink via lstat().

**Acceptance:** `directory_model.h` declares `DirectoryEntry` with a `bool is_symlink` member; a DirectoryEntry
constructed with default initialization has `is_symlink = false`; a DirectoryEntry created from a symlink entry
has `is_symlink = true` and `is_dir` reflecting the symlink's resolved target (unchanged behavior).

#### REQ-F-011 — is_symlink set in stat-success path
**Ubiquitous:** In `directory_model.cpp`, the `readEntry()` method shall call lstat() on the entry path
(in addition to the existing stat() call) and set the DirectoryEntry's `is_symlink` field to true if
lstat() identifies a symlink, even if the subsequent stat() on the target succeeds.

**Acceptance:** For a symlink entry pointing to an existing file, `readEntry()` calls lstat() before or
alongside stat(), and the returned DirectoryEntry has `is_symlink = true` and `is_dir = false`.
For a symlink entry pointing to an existing directory, `readEntry()` returns `is_symlink = true` and
`is_dir = true` (target's type). For a regular file, `readEntry()` returns `is_symlink = false`.

#### REQ-F-012 — is_symlink set in stat-failure path (reuse existing lstat)
**Ubiquitous:** In `directory_model.cpp`, the `readEntry()` method's stat-failure branch (currently used
only to build error messages) already calls lstat() on the entry. That lstat() result shall be reused:
if it identifies a symlink, the DirectoryEntry's `is_symlink` field shall be set to true
(e.g., a dangling symlink where stat() fails but lstat() succeeds).

**Acceptance:** For a dangling symlink entry, the existing lstat() call in the error path succeeds (because
lstat does not follow the link), and the returned DirectoryEntry has `is_symlink = true`. For a broken
permission case (stat fails on an ordinary file under unreadable directory), `is_symlink = false`.
The lstat() call is not duplicated; the result is stored once and reused.

#### REQ-F-013 — is_symlink not duplicated for regular files
**Ubiquitous:** For entries that stat() identifies as regular files (not symlinks), the system shall not
make redundant lstat() calls. Each entry is inspected once with lstat(), including regular files,
and that result is reused if stat() fails.

**Acceptance:** A profiling or call-counting test (via test access or strace on the file browser process)
shows one lstat() call per listed entry, including each of N regular files, with no additional
lstat() call in the stat-failure branch. Account separately for the parent row and unrelated process calls.

#### REQ-F-014 — IsSymlinkRole added to DirectoryModel
**Ubiquitous:** The `DirectoryModel::Role` enum in `apps/files/browsing/directory_model.h` shall include
a new `IsSymlinkRole`, and the `roleNames()` method shall expose it to QML as the property name `"isSymlink"`.

**Acceptance:** `grep IsSymlinkRole apps/files/browsing/directory_model.h` returns the enum declaration;
`grep isSymlink apps/files/browsing/directory_model.cpp` in `roleNames()` returns the mapping; QML code
can read `model.isSymlink` from a Repeater delegate bound to DirectoryModel.

#### REQ-F-015 — QML delegate requires isSymlink property
**Ubiquitous:** The DirectoryListing.qml delegate (the `Repeater` inside `nameColumnField`) shall declare
a `required property bool isSymlink` to consume the `IsSymlinkRole` from DirectoryModel.

**Acceptance:** `DirectoryListing.qml` declares `required property bool isSymlink` on the delegate;
the qmllint check passes; the `filenameRun` HnLabel instances inside the Repeater can read `isSymlink`.

### Applying styling to the name labels

#### REQ-F-016 — Precedence-driven style binding on HnLabel
**Ubiquitous:** Each `filenameRun` HnLabel in the `Repeater` under `nameColumnField` shall bind its
`font.italic`, `font.weight`, and `color` properties with the following base styles (matched runs override color and weight per REQ-F-017):
  - If `isSymlink` is true: `font.italic = true`, `font.weight = Font.Normal`, `color = textSecondary`.
  - Else if `isDir` is true: `font.weight = Font.Bold`, `font.italic = false`, `color = textPrimary`.
  - Else: `font.weight = Font.Normal`, `font.italic = false`, `color = textPrimary`.

**Acceptance:** Three test rows (symlink-to-file, regular folder, regular file) are rendered; inspecting
the HnLabel for each row confirms the correct font.weight, font.italic, and color values.
A visual regression test confirms that regular files render pixel-identically to their pre-feature state.

#### REQ-F-017 — Search match override preserved
**State-driven:** During SEARCH-mode match highlighting on the current cursor row, matched character runs
shall already have their `color` overridden to `accentCyan` and `font.weight` to `Font.Bold`
(existing behavior); this requirement preserves that behavior and adds that `font.italic` is NOT overridden
by search mode—it remains driven by `isSymlink` only.

**Acceptance:** Existing search highlight code in DirectoryListing.qml continues to override `color` and
`font.weight` for matched runs; `font.italic` is never set in the search highlight branch;
a matched run in a symlink's name has `font.italic = true` and matched runs in a folder's name
have both `font.italic = false` and `font.weight = Font.Bold`.

#### REQ-F-018 — Interaction with multi-run names
**Ubiquitous:** The `Repeater` that generates per-character-run HnLabel instances shall apply the
type styling consistently to every run in the entry's name, such that all runs of a symlink are italic,
all runs of a folder are bold and not italic, and unmatched file runs have normal weight and not italic.
Matched runs of every type remain bold per REQ-F-008.

**Acceptance:** A symlink entry with 10 character runs has all 10 runs with `font.italic = true`.
A folder entry with 10 character runs has all 10 runs with `font.weight = Font.Bold`.
A file entry with 10 character runs has all 10 runs with `font.italic = false`.

---

## Non-Functional Requirements

#### REQ-NF-001 — is_dir, mode, icon, subtitle unchanged
**Ubiquitous:** The existing `is_dir` and `mode` fields on DirectoryEntry, the icon resolution logic,
the "Folder" / "Symlink" subtitle text, and the directories-first sort order shall not change.
These properties shall continue to reflect the symlink-resolved target's type, independent of the new
`is_symlink` field.

**Acceptance:** An existing icon-resolution test and a subtitle test pass without modification.
A symlink-to-directory sorts with directories before files; a symlink-to-file sorts with files.
Neither is grouped separately by symlink status. The subtitle for a symlink-to-directory reads "Symlink" or "Folder", unchanged.

#### REQ-NF-002 — One lstat() per entry, no duplication
**Ubiquitous:** The total syscall count increase shall be at most one lstat() per directory entry.
A single lstat() result shall serve both the stat-success and stat-failure paths; the old
conditional failure-path call shall be removed rather than duplicated.

**Acceptance:** A benchmarking test or strace profile shows that opening a directory with N entries
incurs at most N new lstat() calls beyond the pre-feature calls for those entries.
Account separately for the parent row and unrelated process calls.

#### REQ-NF-003 — No new user-facing settings or toggles
**Ubiquitous:** No new configuration option, setting, preference, or checkbox shall be added to enable,
disable, or customize the entry-type styling. The styling shall always be applied where applicable.

**Acceptance:** No entry point for configurable styling appears in the application preferences dialog
or in configuration files; the styling is always active for all directory listings.

#### REQ-NF-004 — Negligible performance overhead
**Ubiquitous:** The rendering and binding evaluation overhead of the new `isSymlink` property shall be
negligible compared to existing DirectoryListing rendering cost.

**Acceptance:** A smoke test opening the file browser on a directory with 1000 entries takes < 50 ms longer
than the baseline (or shows no measurable increase within noise margin).

---

## Constraints

#### REQ-C-001 — Scope: DirectoryModel + DirectoryListing.qml only
**Ubiquitous:** Production code changes shall be confined to `apps/files/browsing/directory_model.h` and
`directory_model.cpp` (for the `is_symlink` field and lstat() logic) and `apps/files/qml/listing/DirectoryListing.qml`
(for the QML delegate binding). No changes to Places sidebar, breadcrumbs, Quick Look overlay, or other
file-browser UI surfaces.

**Acceptance:** Production code changes in `git diff` are limited to `directory_model.h`,
`directory_model.cpp`, and `DirectoryListing.qml`; supporting tests, README, and SDD documents may change. A grep for `isSymlink` or `italic` in Places/breadcrumb/QuickLook QML files
returns no results. The `.qml` files outside `DirectoryListing.qml` render directory entries identically to before.

#### REQ-C-002 — No holonight-qt changes
**Ubiquitous:** No modification to the `holonight-qt` repository, including no changes to `HnLabel`
primitive, `HnAppearance`, `HoloniightPalette`, or any other shared primitives.

**Acceptance:** `git -C ../holonight-qt status --porcelain` is empty after the feature implementation.
Existing `HnLabel` properties (`font.italic`, `color`, `font.weight`) are consumed as-is.

#### REQ-C-003 — Conventional Commits format
**Ubiquitous:** Any commit(s) introducing this feature shall follow Conventional Commits format:
`type(scope): imperative summary` or `type: imperative summary`, as specified in the repository's AGENTS.md.

**Acceptance:** `git log --oneline` for the feature commits matches `^(feat|fix|docs|refactor)(\(.+\))?: .+`
with an imperative summary. No specific co-author attribution is required by AGENTS.md.

#### REQ-C-004 — QML formatting and linting
**Ubiquitous:** Any modified or new `.qml` files shall pass `task qml-lint` and `task format-check`.

**Acceptance:** Running `task qml-lint` and `task format-check` shows no warnings or errors in `DirectoryListing.qml`
or any related files modified by the feature.

#### REQ-C-005 — Existing tests pass
**Ubiquitous:** All existing unit tests and integration tests (directory_model_test, directory_listing_test,
file_manager smoke tests) shall continue to pass; existing assertions may be extended without weakening them.

**Acceptance:** `task test` runs and all existing test targets pass.

---

## Non-Goals

- Change file-type icons or icon resolution logic.
- Change sort order, directory grouping, or "directories first" behavior.
- Change the Size, Modified Date, or other columns or their rendering.
- Change the subtitle/caption text or visibility.
- Add a user-facing setting or preference to toggle or customize styling.
- Modify holonight-qt primitives.
- Modify the Places sidebar, breadcrumbs, or Quick Look overlay rendering.

## Test fixtures and helpers

Use existing DirectoryEntry/DirectoryModel infrastructure in `tests/directory_model_test.cpp`.
Create temporary symlink and regular file/directory entries via standard filesystem calls
(symlink(), mkdir(), etc.) or the existing test helper functions. Verify the `isSymlink` property
via the DirectoryModel's data() method or via QML model inspection in integration tests.

## Verification checklist

- [x] `is_symlink` field is correctly populated for symlinks and regular entries in both stat-success and stat-failure paths.
- [x] `IsSymlinkRole` is exposed to QML and the `required property bool isSymlink` is declared in DirectoryListing.qml.
- [x] Symlink names render italic + secondary color; folder names render bold + primary color; file names render with normal weight + primary color.
- [x] Search highlighting overrides matched-run color but NOT italic; italic remains driven by `isSymlink`.
- [x] ".." row renders bold.
- [ ] INSERT-mode placeholder rendering acceptance remains pending; the model defaults `is_symlink` to false.
- [x] Dangling symlinks are marked `isSymlink = true` and render italic + secondary color.
- [x] Existing CTest targets pass (sandbox failures passed on unrestricted rerun; see TASKS.md).
- [ ] Visual regression confirms regular-file pixels match the pre-feature baseline (T-011).
- [ ] Syscall counts are recorded (T-012).
- [ ] Baseline-relative 1000-entry latency is recorded (T-013).
- [ ] Commits follow Conventional Commits format.
