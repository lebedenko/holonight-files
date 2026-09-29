# Named-Folder Icons in the Main Directory Listing

## Context

HoloNight Files displays a directory listing with file-type icons (resolved via the main-view-icons specification, Stage 1). Well-known XDG user directories (Home, Desktop, Documents, Downloads, Pictures, Music, Videos, Projects, Templates, Public) are also shown in the sidebar (PlacesPanel) with named folder icons (e.g., `folder-documents`, `folder-pictures`). This specification formalizes the resolution of these same well-known directories to their named folder icons when they appear in the main directory listing and the parent (`..`) row, keeping the listing and sidebar visually consistent.

For example, when navigating to the home directory, the "Documents" entry in the listing shall show the `folder-documents` icon (or fallback to the generic `folder` icon if the theme lacks a named variant), the same as the sidebar shows. When a theme does not provide `folder-documents`, the icon chain includes the generic `folder` as a fallback candidate, so the icon is never blank.

The feature operates within the scope of directory listing display (Stage 1) and places/sidebar consistency; it does not interact with multi-selection, nested directory navigation strategies, or runtime theme switching.

---

## Explicit Non-Goals

The following capabilities are explicitly OUT OF SCOPE for this specification:

1. **Sidebar Icon Changes** — The sidebar's own icon resolution and display are unchanged; this spec ensures the listing is consistent with the sidebar's choices, not vice versa.
2. **Bookmark, Trash, and Network Icons** — Only XDG user directories (the 9 keys recognized by `UserDirsParser`) are assigned named icons. Bookmarks, Trash, and network locations are out of scope.
3. **Name-Only Matching** — Directories whose name matches a well-known folder (e.g., `~/work/Documents`) but whose absolute path does not match the XDG user-dirs entry are never assigned a named icon; they show the generic folder icon.
4. **Symlink Canonicalization** — A symlink whose target is a well-known directory (e.g., `~/Docs -> ~/Documents`) is matched against its listed path, not its target; it shows the generic folder icon. No per-entry `realpath()` or target resolution is performed.
5. **Runtime Path Updates** — The path→icon-name map is built once at application startup from the user-dirs.dirs file; it is not re-read or refreshed if the file changes during the session.
6. **Per-Filesystem Scope** — Well-known folder matching is not restricted by filesystem boundaries; a directory on a different partition or mount that happens to match a place path is still assigned its named icon.
7. **Non-Directory Entries** — A regular file whose absolute path matches a place path cannot exist (place paths are always directories); non-directory entries are never assigned named folder icons, even if a match were possible.
8. **Quick Look, Info Sidebar, and Preview Pane Icons** — These components inherit the icon name from the DirectoryModel (no separate resolution); their display is unchanged by this spec.

---

## Requirements

### Icon Name Resolution for Well-Known Directories

#### REQ-F-001: One Shared Place List as Source of Truth

**EARS:** Ubiquitous

The system shall derive the path→icon-name mapping from one shared place list that the sidebar and `DirectoryModel` both consume. Its XDG entries come from the `UserDirsParser` module, which reads `${XDG_CONFIG_HOME:-~/.config}/user-dirs.dirs` at startup. The parser provides both the absolute path and the named icon name (e.g., `Key::Documents` → `"folder-documents"`) for each recognized directory. DirectoryModel shall use this mapping (not re-parse or re-detect) to resolve well-known directory paths to their named icons.

**Acceptance Criteria:**
- `UserDirsParser::iconName(key)` returns the icon name string for each of the 9 XDG keys (`Desktop`, `Documents`, `Downloads`, `Pictures`, `Music`, `Videos`, `Projects`, `Templates`, `Public`), matching the names exposed in the sidebar.
- `UserDirsParser` is the only module responsible for parsing user-dirs.dirs; no second independent file parsing or table exists in DirectoryModel or elsewhere.
- Home's `user-home` name is currently hard-coded in `PlacesModel::buildPlaces`, not in `UserDirsParser`. It moves into the shared place list (single definition), and `PlacesModel` consumes that list too, so no icon name is defined in two places.
- The path→icon-name map is built from the output of `UserDirsParser::parse()` (or `parseFile()`), with entries keyed by their cleaned absolute path and no additional filtering or transformation.
- Application startup passes the path→icon-name map to DirectoryModel (via constructor, setter, or dependency injection); the map is immutable during the listing lifetime.

---

#### REQ-F-002: Exact Path Matching, No Name-Only Heuristics

**EARS:** Ubiquitous

The system shall assign a named icon to a directory entry in the listing only if the entry's cleaned absolute path exactly equals a place path from the `UserDirsParser` map. Name-only matching (e.g., an entry named "Documents" in a different parent directory) is not performed.

**Acceptance Criteria:**
- A directory at `/home/alice/Documents` is assigned the `folder-documents` icon only if `/home/alice` is the user's home and `/home/alice/Documents` is the `XDG_DOCUMENTS_DIR` value.
- A directory at `/home/alice/work/Documents` (a subdirectory with a matching name but not matching absolute path) is assigned the generic `folder` icon, not `folder-documents`.
- A directory at `/mnt/external/Documents` (on a different filesystem) matches only if `/mnt/external/Documents` is explicitly configured in user-dirs.dirs and matches exactly.
- Matching is performed after `QDir::cleanPath()` normalization, so trailing slashes and `.` components do not affect the comparison.
- A unit test asserts that a directory named "Documents" at a different parent path stays generic.

---

#### REQ-F-003: Icon Name Prepended to the Theme Chain

**EARS:** Ubiquitous

When a directory entry's absolute path matches a place, the system shall prepend the corresponding named icon name to the icon candidate chain, producing a chain such as `folder-documents/folder/inode-directory`. The icon name resolution logic (REQ-F-006 in main-view-icons SPEC) attempts the named icon first; if the theme lacks that name, the chain falls back to the generic folder candidates.

**Acceptance Criteria:**
- A matched directory entry's icon name role contains the chain `folder-documents/folder/inode-directory` (or equivalent with a different named icon).
- The chain is stored as a single string using the `/` separator (defined in `IconNameResolver::kChainSeparator`).
- Theme resolution attempts `folder-documents` first; if not found, it falls back to `folder`, then `inode-directory` (following main-view-icons REQ-F-006 candidate logic).
- If the theme provides a `folder-documents` icon, it is used; if not, the generic `folder` icon is used; in both cases, a visible icon is shown.
- A unit test asserts the exact chain for a Documents directory and a generic directory.

---

#### REQ-F-004: Symlinks Matched by Listed Path, Not Target

**EARS:** Ubiquitous

The system shall resolve symlinks to a named icon based on the symlink's own listed absolute path, not the target path. A symlink `~/Docs -> ~/Documents` is matched against `~/Docs` (not `~/Documents`), and since `~/Docs` is not a place, the symlink shows the generic folder icon.

**Acceptance Criteria:**
- A symlink whose listed path matches a place path shows the named icon for that place.
- A symlink whose target is a place path, but whose own path does not match, shows the generic folder icon.
- No per-entry `realpath()`, `readlink()`, or target resolution is performed for this matching.
- A unit test asserts that a symlink to Documents shows generic, while a symlink named to match a place path shows that place's icon.

---

#### REQ-F-005: Non-Directory Entries Never Match

**EARS:** Constraint

A regular file, FIFO, socket, or other non-directory entry shall never be assigned a named folder icon, even if its absolute path were to match a place path (which cannot happen in normal operation, since place paths are always directories). If a non-directory entry is encountered with a path matching a place, the generic file or special-file icon is used.

**Acceptance Criteria:**
- Directories are identified by the `S_ISDIR` bit in their `stat()` mode.
- A regular file at a path matching a place path (if such an anomaly occurs) is assigned the generic file chain, not a named folder icon.
- Code review confirms the named-icon matching logic checks `S_ISDIR` before prepending the named icon name.
- A unit test asserts that a regular file in place of a directory entry shows a generic file icon.

---

### Icon Resolution Chain and Theme Integration

#### REQ-F-006: Icon Chain Fallback Preserves Folder Fallback Decision

**EARS:** Conditional

`PreviewPane.qml` currently decides the folder-vs-generic-file fallback glyph from `PreviewService.iconName` alone (`iconName === "folder" || iconName.startsWith("folder/")`); it has no directory flag. A chain beginning `folder-documents/` would fail that test and show the generic-file glyph when the theme lacks the named icon. The decision shall therefore not depend on the chain's first name.

**Acceptance Criteria:**
- `PreviewPane.qml` no longer tests `startsWith("folder/")` / `=== "folder"` on the chain.
- The decision is true exactly when the chain's candidates (split on `IconNameResolver::kChainSeparator`) contain `folder`, or when `PreviewService` exposes an is-directory flag; the design picks one. `DirectoryListing.qml` already uses the model's `isDir` and is unaffected.
- A directory whose chain is `folder-documents/folder/inode-directory` shows the folder-fallback glyph when nothing resolves; a regular file never does.
- A test with a theme lacking `folder-documents` asserts that a Documents directory shows the folder-fallback glyph, not the generic-file glyph.

---

#### REQ-F-007: Provider Failure Logging Does Not Regress

**EARS:** Unwanted Behaviour

Icon lookups for named folders that the theme does not provide shall not make warning output worse than today. The existing `IconFallbacks` cache is keyed by the whole chain string, so a missing named icon adds at most one further failed lookup per distinct chain (e.g. `folder-documents/folder/inode-directory`), not per row. This spec does not require the open main-view-icons task T-025.

**Acceptance Criteria:**
- With a theme lacking `folder-documents`, listing 100 rows that share one named chain triggers at most one uncached failed lookup for that chain (later rows skip via `IconFallbacks`).
- No new per-row logging is added.
- Code review confirms no per-row logging is added for named-icon fallbacks; the provider's existing caching and per-name tracking suffices.
- A test with a theme lacking named folder icons asserts no log inflation.

---

### Directory Listing Row Behavior

#### REQ-F-008: Listing Rows Resolve Well-Known Directories

**EARS:** Ubiquitous

Directory listing rows shall resolve their icon names using the well-known directory mapping. If a row's path matches a place, its icon name is the prepended chain; otherwise, it is the generic directory chain.

**Acceptance Criteria:**
- A directory entry in DirectoryModel is assigned an icon name via the new `IconNameRole` (or equivalent).
- For each entry, the icon-name resolution logic first checks if the entry's path matches a place; if so, the named icon is prepended to the generic chain.
- If no match, the entry is resolved as a generic directory (following main-view-icons REQ-F-006).
- The icon name is computed on the DirectoryModel worker thread during listing (no GUI thread involvement in this matching logic).
- A test with a listing including a Documents directory asserts the icon name includes the named icon at the front of the chain.

---

#### REQ-F-009: Parent Row (`..`) Resolves Its Own Path

**EARS:** Ubiquitous

The synthetic parent row (`..`) is resolved using the parent directory's own absolute path. If the parent directory is a well-known place, the named icon is prepended; otherwise, the generic folder chain is used.

**Acceptance Criteria:**
- When displaying a subdirectory of a place (e.g., `~/Documents/Projects`), the `..` row shows the parent's icon: if the parent is `~/Documents`, the `..` row shows `folder-documents/folder/inode-directory`.
- The `..` row is resolved at the time the parent entry's path is known (either from `readParentEntry()` or `syntheticParentEntry()`).
- A test navigates into a subdirectory of Documents and asserts that the `..` row's icon is `folder-documents/...`.

---

#### REQ-F-010: Home Directory Uses `user-home` Icon

**EARS:** Ubiquitous

The home directory is assigned the `user-home` icon name (from the shared place list, REQ-F-001), not a generic folder icon. It is matched by the same exact-path mechanism as other well-known directories.

**Acceptance Criteria:**
- When a directory listing shows the home directory (e.g., navigating to `~` or opening a window at the home path), the icon name includes `user-home/folder/inode-directory`.
- A test that lists the home directory asserts the icon name contains `user-home`.

---

### Placeholder and No-Selection Rows

#### REQ-F-011: INSERT-Mode Placeholder Stays Generic

**EARS:** Ubiquitous

The INSERT-mode placeholder row (REQ-F-011 in main-view-icons) shall always keep its existing generic icon (`application-x-generic`), never a named folder icon, even if it appears as a new entry in a well-known directory.

**Acceptance Criteria:**
- A placeholder row created during INSERT mode in a Documents directory keeps its existing generic chain (`application-x-generic`, unchanged from main-view-icons), never `folder-documents/...`.
- The placeholder is not resolved against the place mapping; it always uses its existing generic chain.
- Code review confirms the placeholder row's icon is hardcoded to the generic chain, not computed from its anchor row's path.

---

### Worker Thread and Threading Model

#### REQ-F-012: Named-Icon Matching on Worker Thread

**EARS:** Ubiquitous

The named-icon matching (path → place mapping lookup) shall occur on the DirectoryModel worker thread alongside existing file stat and name extraction. The worker thread reads an immutable path→icon-name map, looks up each entry's path, and appends the result to the icon name chain. No GUI thread involvement or per-row I/O is required.

**Acceptance Criteria:**
- The DirectoryModel worker thread is passed the path→icon-name map during initialization (e.g., in the constructor or via a setter).
- The map is immutable: the worker thread only reads it; no writes occur during listing.
- For each directory entry in the listing, the worker computes the icon name (including place matching) before returning the row to the GUI thread.
- The map is built once at startup, not re-read during each directory listing.
- No additional threads are introduced; the work reuses the existing DirectoryModel worker pattern.

---

#### REQ-F-013: Map Initialization at Application Startup

**EARS:** Ubiquitous

The application shall initialize the path→icon-name map once at startup, before any directory listing occurs. The map is passed to DirectoryModel during construction or before the first listing. If user-dirs.dirs does not exist or is malformed, an empty map (or default home-only map) is used; missing user-dirs.dirs is not an error.

**Acceptance Criteria:**
- `UserDirsParser::parseFile()` is called once during application initialization, reading the user-dirs.dirs file.
- The resulting path→icon-name map is passed to DirectoryModel (e.g., via a `setPlaceIconMap()` method or constructor parameter).
- If the file does not exist, `parseFile()` returns an empty vector (or a minimal vector with only the home directory); no error is logged (follows UserDirsParser spec).
- Subsequent directory listings in DirectoryModel use this same map; no re-read occurs.
- A test asserts that DirectoryModel can list a directory before the map is set (defaulting to generic icons) and that after the map is set, named icons are resolved correctly.

---

### Consistency with Sidebar

#### REQ-F-014: Listing and Sidebar Use Identical Icon Names

**EARS:** Ubiquitous

When a well-known directory appears in both the sidebar (PlacesPanel) and the main directory listing, both shall display the same icon name (e.g., `folder-documents`). This ensures visual consistency: the user sees the same icon in the sidebar and in the listing for the same directory.

**Acceptance Criteria:**
- The sidebar's icon resolution (PlacesModel) and the listing's icon resolution (DirectoryModel with place mapping) use the same source: the shared place list of REQ-F-001.
- Both the sidebar and listing use the same directory path values from `UserDirsParser`.
- No independent icon name assignment or hardcoding occurs in either component; all names come from the shared place list.
- A test or code review asserts that PlacesModel and DirectoryModel resolve the same icon name for the same directory path.

---

#### REQ-F-015: Info Sidebar and Quick Look Inherit the Icon

**EARS:** Ubiquitous

UI components that display the icon for the currently selected or navigated directory (the info sidebar / preview pane; Quick Look) shall inherit the icon name from the DirectoryModel (via the `IconNameRole` or equivalent), not re-resolve or re-detect it. This ensures consistency across all UI regions.

**Acceptance Criteria:**
- The info sidebar, preview pane, and Quick Look overlay (if they display icons) use the same icon name role from DirectoryModel that the listing uses.
- No separate named-icon matching or place-path lookup occurs in these components.
- A test navigates to a well-known directory and asserts that all UI components showing that directory's icon display the same icon name (e.g., `folder-documents/...`).

---

### Testing and Verification

#### REQ-F-016: Unit Tests for Icon Name Derivation

**EARS:** Ubiquitous

The system shall include unit tests for named-folder icon resolution, covering exact-path matching, symlink handling, and chain construction.

**Acceptance Criteria:**
- A gtest module (e.g., `icon_name_resolver_test.cpp`) tests the icon-name derivation for directories matched to places and directories that do not match.
- Test cases cover: (1) a directory matching a place path; (2) a directory with a matching name but different parent; (3) a symlink to a matched directory; (4) a symlink named to match a place path (matches its own path, not target); (5) non-directory entries; (6) the home directory; (7) the parent row for a subdirectory of a place.
- Each test asserts the exact icon name chain (e.g., `folder-documents/folder/inode-directory` or `folder/inode-directory`).

---

#### REQ-F-017: DirectoryModel Test for Listing Integration

**EARS:** Ubiquitous

A DirectoryModel test shall verify that the named-icon map is correctly applied during listing and that icon names are exposed via the DirectoryModel role.

**Acceptance Criteria:**
- A test creates a DirectoryModel with a place map (or passes one via a setter) and lists a directory containing matched and unmatched subdirectories.
- The test asserts that matched directories have the named icon prepended in their icon name role.
- The test asserts that unmatched directories show the generic folder chain.
- A parent row (`..`) test asserts that if the parent is a place, its icon is named.

---

### Existing Behavior Preservation

## Non-Functional Requirements

#### REQ-NF-001: Place Map Immutability and Worker Thread Safety

**EARS:** Ubiquitous

The path→icon-name map shall be immutable and thread-safe for read access from the DirectoryModel worker thread. No mutable state is introduced; the map is read-only and shared across all directory listings.

**Acceptance Criteria:**
- The map is passed to DirectoryModel as a const reference or held as a `const` member variable.
- No synchronization primitives (locks, mutexes) are needed; the map is not modified after initialization.
- The worker thread accesses the map concurrently with other listing operations without data races.
- The worker thread calls only const/read methods on the map; no component writes to it after startup.

---

#### REQ-NF-002: No Performance Regression

**EARS:** Ubiquitous

Adding named-icon resolution shall not cause measurable performance regression in directory listing speed, icon rendering, or memory usage.

**Acceptance Criteria:**
- Path matching is a hash lookup (O(1) per entry), never a linear scan and never file I/O.
- Code review confirms no `stat()`, `realpath()` or other syscall is added per entry for this feature.

---

## Constraints

#### REQ-C-001: Icon Names Have One Definition

**EARS:** Constraint

The `UserDirsParser` module shall remain the only source of XDG icon names (`iconName(key)`), and the shared place list (REQ-F-001) shall be the only source of the Home icon name. No other icon-name table exists.

**Acceptance Criteria:**
- `UserDirsParser::iconName(UserDirsParser::Key key)` returns a `QString` for each of the 9 keys.
- The returned icon names match the theme-provided names (e.g., `folder-documents`, `folder-pictures`).
- No other module or file defines icon names for places, including `PlacesModel` (its hard-coded `user-home` is removed).

---

#### REQ-C-002: DirectoryModel Role for Icon Names

**EARS:** Constraint

The DirectoryModel shall expose the icon name for each entry via the existing `IconNameRole` (added by main-view-icons spec REQ-C-001). The role returns the computed icon name chain (including prepended named icon if matched to a place).

**Acceptance Criteria:**
- `DirectoryModel::data(index, IconNameRole)` returns the computed chain for each entry.
- For a matched place directory, the chain begins with the named icon (e.g., `folder-documents/...`).
- For other directories, the chain is the generic folder chain (`folder/inode-directory`).
- The role is computed on the worker thread and passed to the GUI thread alongside other row data.

---

#### REQ-C-003: No New QML-Visible API on DirectoryModel

**EARS:** Constraint

No new `Q_PROPERTY`, signal or role is added to `DirectoryModel` for this feature. The place map is supplied through a C++-only constructor argument or setter, not exposed to QML.

**Acceptance Criteria:**
- No new `Q_PROPERTY`, `Q_INVOKABLE`, signal or role enumerator is added.
- The map setter/constructor parameter is not `Q_INVOKABLE`.

---

#### REQ-C-004: Minimal C++ Changes

**EARS:** Constraint

C++ changes shall be limited to the shared place list (REQ-F-001), the place-map initialization logic, the per-entry path-matching lookup in the icon-name derivation, and internal DirectoryModel state to hold the map. No new classes, complex data structures, or I/O operations are introduced.

**Acceptance Criteria:**
- Place-map initialization is a few-line function call (e.g., `model->setPlaceIconMap(parser.parse(...))`).
- Per-entry matching is a simple map lookup.
- The map is a standard C++ container (e.g., `QMap` or `std::unordered_map` keyed by path strings).
- No new async mechanisms, threads, or callbacks are added.
- The shared place list may be one small new header/function; no new QObject or worker class.

---

#### REQ-C-005: Reuse of IconNameResolver Namespace

**EARS:** Constraint

Icon-name derivation (icon chain construction from matched/unmatched state) shall be performed by the existing `IconNameResolver` namespace (created by main-view-icons) or a minimal extension. No new icon-name computation is introduced.

**Acceptance Criteria:**
- `IconNameResolver::candidateIconNames()` is called with the entry's mode and name.
- If the entry's path matches a place, the named icon name is inserted at the front of the result (before the generic folder chain).
- If no match, the result is returned unchanged (generic chain).
- Code review confirms no new icon-name logic is added outside IconNameResolver.

---

## Summary

This specification formalizes the resolution of well-known XDG user directories (via `UserDirsParser`) to named folder icons (e.g., `folder-documents`, `folder-pictures`) in the main directory listing and parent row, matching the sidebar's visual presentation. The system reads a one-time immutable path→icon-name map at startup, passes it to DirectoryModel, and uses it during directory listing on the worker thread to prepend named icons to the generic folder icon chain for matched directories. Exact-path matching ensures that name-only matches (e.g., a `Documents` folder at a different parent) stay generic; symlinks are matched by their listed path, not their target. The named icon is added to the front of the candidate chain (e.g., `folder-documents/folder/inode-directory`), allowing theme fallback to generic `folder` if the named icon is unavailable. PreviewPane's folder-vs-file fallback decision is made independently of the chain's first component, preserving correct fallback glyph selection. Eight explicit non-goals (sidebar changes, bookmarks, name-only matching, symlink canonicalization, runtime updates, cross-filesystem restrictions, non-directories, Quick Look/preview changes) are excluded from scope. All icon-name matching uses existing `IconNameResolver` and `UserDirsParser` modules, with worker-thread-only map access and no performance regression. Consistency with the sidebar is maintained through the shared `UserDirsParser` source and identical icon-name values.

---

## Fixtures and Test Data

### Required Test Cases

- **Icon Name Derivation:** Gtest covering matched well-known directories (Home, Documents, etc.), unmatched directories with similar names, the home directory specifically, and the parent row.
- **Symlink Handling:** Symlinks to well-known directories (matched by own path), symlinks with names matching places but different targets (matched by own path), and dangling symlinks (unmatched).
- **Non-Directory Entries:** A regular file or FIFO at a path matching a place (if constructible in tests) shows generic non-directory icons, not named folder icons.
- **Sidebar Consistency:** A test asserting `PlacesModel` and `DirectoryModel` yield the same icon name for the same path; native visual check on the real display (fractional scale).
- **Preview Pane Fallback:** A directory whose named icon is missing from the theme (e.g., `folder-documents` on a minimal theme) shows the bundled folder-fallback.svg glyph, not the generic-file glyph.

---

## Testing and Acceptance

| Requirement | Verifiable As |
|-------------|---------------|
| REQ-F-001 to REQ-F-005 | Unit tests for path matching and chain construction; code review of place-map initialization |
| REQ-F-006 to REQ-F-007 | PreviewPane test with missing named icons; log inspection for unbounded warnings |
| REQ-F-008 to REQ-F-011 | DirectoryModel test with place map; visual inspection of listing rows and parent row |
| REQ-F-012 to REQ-F-015 | Code review of worker-thread map access; consistency test of sidebar and listing icons |
| REQ-F-016 to REQ-F-017 | Gtest suite covering icon derivation and DirectoryModel integration |
| Whole suite | `task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint` all pass with no unexplained test edits |
| REQ-NF-001 to REQ-NF-002 | Thread-safety review; code review for no per-entry syscalls |
| REQ-C-001 to REQ-C-005 | Code review: UserDirsParser icon names, DirectoryModel role, IconNameResolver reuse |
