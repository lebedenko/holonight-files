# Places Sidebar Sources — Requirements Specification (EARS Format)

## Status

**Implemented; review remediation verified automatically.** Native acceptance remains pending;
see [verification](VERIFICATION.md). The user authorized correction of the review
findings on 2026-09-17; earlier approval history is not recorded here.

## Overview

The Places sidebar source redesign introduces three distinct sources of locations: Home, XDG user directories, and user-managed bookmarks. The system reads locations from standard XDG paths on startup, applies deduplication and validity checks asynchronously to avoid UI freezing, and surfaces availability status in the UI. This spec preserves all prior Places behaviors (row styling, icons, focus/activation mechanics, accessible names) while replacing the fixed list (Home, QStandardPaths six dirs, hardcoded ~/Projects) with a dynamic, user-configurable model built from XDG conventions and a places.toml bookmark file.

## Scope

### In Scope

**Sources of Places (in display order):**

1. **Home** — `QDir::homePath()`, always shown, label "Home", icon `user-home`, origin `Home`.
2. **XDG User Directories** — Parsed directly from `${XDG_CONFIG_HOME:-~/.config}/user-dirs.dirs`. Display order: Desktop, Documents, Downloads, Pictures, Music, Videos, Projects, Templates, Public (each with translated label and theme icon). Origin: `XdgUserDirectory`.
3. **Bookmarks** — Parsed from `$XDG_DATA_HOME/holonight/holonight-files/places.toml` (fallback `~/.local/share`). File format: TOML with `version = 1`, `[[bookmarks]]` entries containing `path` (required, absolute or `~/`-relative) and optional `name` (defaults to directory base name). Origin: `Bookmark`. Icon: `user-bookmarks` with bundled folder fallback.

**XDG User Directories File Parsing:**

- Source file: `${XDG_CONFIG_HOME:-~/.config}/user-dirs.dirs` (same unset/empty/non-absolute fallback as xdg_paths).
- Accepted value forms: `$HOME/yyy` (shell-escaped, homedir-relative) and `/yyy` (absolute); shell escaping unescaped.
- Any other form or malformed line silently ignored.
- A value equal to `$HOME` is treated as disabled and the entry omitted.
- An entry is shown only if key present AND path does not resolve to `$HOME` AND the path is a directory (checked off the GUI thread; see Availability Checking).
- Parsing at startup only; no live reload or file watching.

**Bookmarks File Parsing:**

- File format: TOML 1.0.0, accessed only through the existing TOML adapter unit.
- Required fields: `version = 1` (top-level), `path` per bookmark (required, absolute or `~/`-relative, cleaned with `QDir::cleanPath`).
- Optional fields: `name` per bookmark (defaults to directory's base name if missing).
- File order determines display order.
- Error handling: missing file → silent, nothing created; unparseable TOML → all bookmarks ignored, exactly one warning to stderr with file path, line number, and error message; version missing or != 1 → whole file ignored, one warning; invalid entry (missing path, wrong type, relative path, `$VAR` or other non-`~/` non-absolute form) → that entry skipped with one warning, other entries still load; unknown keys (top-level or inside an entry) → one warning per unknown key, ignored, entry still loads if valid.

**Deduplication:**

- By cleaned path string (no symlink resolution).
- Priority: Home > XDG > Bookmarks (first wins). A later XDG entry duplicating an earlier XDG entry is dropped silently.
- A bookmark duplicating Home, an XDG place, or an earlier bookmark is dropped with one warning and omitted from the model.
- Names are never deduplicated; two places may have identical names.

**Availability Checking:**

- Rows (Home + all parsed candidates) appear immediately with status `Checking`.
- Existence and readability checks run off the GUI thread; GUI thread never blocks on filesystem stat.
- Parsing the two small local files (user-dirs.dirs, places.toml) may be synchronous at startup; checks are always async.
- When checks resolve, XDG candidates that are not directories are removed; bookmarks that are not readable directories are marked Unavailable and stay listed.
- A hung check (e.g., dead NFS/sshfs mount) must not prevent other places' checks from resolving.

**Bookmark Activation:**

- Whenever a bookmark is activated (Enter/Space/click in NORMAL mode, no task prompt, no Quick Look) — regardless of its current status — it is re-checked off the GUI thread.
- If available → navigate via `DirectoryController.open()` and row status updates to Available.
- If unavailable → no navigation, current folder unchanged, message "Location is currently unavailable" shown via the controller's existing error/status path, row status becomes Unavailable.

**Model & API:**

- `PlacesModel` exposes roles: `name`, `path`, `iconName` (existing), plus `origin` (Home/XdgUserDirectory/Bookmark) and `status` (Checking/Available/Unavailable).
- Parsing lives in pure testable units (user-dirs parser, bookmark store/reader).
- No new third-party dependencies (tomlplusplus already used by config, xdg_paths already available).

**Visual Design:**

- Identical row styling for all origins; one extra spacing gap between last standard place and first bookmark.
- No separator line, no bookmarks subheading.
- Preserved from prior spec: muted "Places" heading, compact shared delegates, theme icon with bundled fallback, token spacing, scrolling in short windows.
- Row highlighted exactly when path equals controller currentPath.
- Activation only in NORMAL mode with no task prompt and no Quick Look open (otherwise disabled).
- Accessible names, Tab focus, Up/Down navigation, Enter/Space activation (P-007 preserved).

### Out of Scope

- Trash, Devices, Network, Recent sources.
- UDisks2, mount integration, or mount watching.
- Importing GTK or KDE bookmarks.
- In-app add/remove/rename/reorder of bookmarks.
- Drag-and-drop or context menus for places.
- Writing or creating places.toml from the app (file format is designed for future app writes, but only manual editing is supported in this cycle).
- Live reload, file watching, or mount watching.
- Symlink canonicalisation.
- Custom per-bookmark icons.

## Non-Goals

This stage explicitly does not include, and these decisions are firm (not deferred):

- **Places UI controls:** No in-app bookmark editor, no add/remove/reorder interface. All management is via manual places.toml editing.
- **Advanced discovery:** No automatic scan of subdirectories, no "smart" suggestions, no recent-folder frequency analysis.
- **Cross-platform bookmark sync:** No cloud sync, no browser bookmark integration, no Nextcloud/Synology plugins.
- **Symbolic links and aliases:** Symlinks in paths are not canonicalised; two symlinks to the same directory are treated as separate places.
- **Per-bookmark icons:** No custom icon field; all bookmarks use `user-bookmarks` (with folder fallback).
- **File watching or live updates:** Statuses only change at startup resolution and on manual activation.

## Functional Requirements

### Source Parsing and Initialization

**REQ-F-001: Read Home Directory**

The system shall, at startup, expose Home as `QDir::homePath()` with label "Home", icon name `user-home`, origin `Home`, and status `Checking`.

Acceptance Criterion: A unit test verifies `PlacesModel` initializes with Home as the first row, path set to the result of `QDir::homePath()`, label "Home", icon `user-home`, and origin `Home`.

---

**REQ-F-002: Parse XDG User Directories File**

The system shall read `${XDG_CONFIG_HOME:-~/.config}/user-dirs.dirs` at startup and parse each line of the form `XDG_KEY_NAME="$HOME/subdir"` or `XDG_KEY_NAME="/absolute/path"`, treating other forms as malformed and silently ignoring them.

Acceptance Criterion: A unit test creates a user-dirs.dirs file with valid lines (Desktop, Documents, etc.), shell-escaped paths, and junk lines, verifies each valid line is parsed correctly, and confirms junk is ignored without warnings.

---

**REQ-F-003: Unescape Shell-Escaped Paths in user-dirs.dirs**

When parsing user-dirs.dirs, the system shall unescape the value (removing the surrounding quotes and interpreting backslash escapes such as `\"`, `\\`, `\$` and `` \` ``) and resolve `$HOME` placeholders.

Acceptance Criterion: A unit test parses `XDG_DOCUMENTS_DIR="$HOME/My Documents"`, `XDG_PICTURES_DIR="$HOME/Say \"Hi\""` and `XDG_MUSIC_DIR="/srv/\$music"` and verifies they resolve to `<home>/My Documents`, `<home>/Say "Hi"` and `/srv/$music` respectively.

---

**REQ-F-004: Hide XDG Dirs Where Value Equals Home**

An XDG entry whose resolved path equals `$HOME` is treated as disabled and omitted from the model, silently.

Acceptance Criterion: A unit test parses a user-dirs.dirs with `XDG_DOCUMENTS_DIR="$HOME"` and `XDG_DOWNLOAD_DIR="/home/user/dl"` (an existing directory), verifies Documents is not in the model and Downloads is present.

---

**REQ-F-005: Display XDG Directories in Fixed Order**

XDG user directories shall be displayed in the following order: Desktop, Documents, Downloads, Pictures, Music, Videos, Projects, Templates, Public.

Acceptance Criterion: A unit test with all nine XDG entries present verifies the model displays them in the specified order; a test with a subset verifies the order is preserved within the present entries.

---

**REQ-F-006: Assign Translated Labels and Theme Icons to XDG Directories**

Each XDG directory shall display with a translated label (Desktop, Documents, Downloads, Pictures, Music, Videos, Projects, Templates, Public) and corresponding theme icon (user-desktop, folder-documents, folder-download, folder-pictures, folder-music, folder-videos, folder-development, folder-templates, folder-publicshare).

Acceptance Criterion: A unit test with all nine XDG entries verifies each is assigned the correct label and icon name in the model.

---

**REQ-F-007: Parse Bookmarks from places.toml**

The system shall read and parse `$XDG_DATA_HOME/holonight/holonight-files/places.toml` (fallback `~/.local/share` when XDG_DATA_HOME is unset, empty, or non-absolute) and extract all `[[bookmarks]]` entries.

Acceptance Criterion: A unit test creates a places.toml with three bookmarks, starts the app with XDG_DATA_HOME redirected, verifies all three bookmarks are loaded and added to the model in file order.

---

**REQ-F-008: Extract Bookmark Path and Optional Name**

For each bookmark entry, the system shall extract the `path` field (required, string) and the `name` field (optional, string). If `name` is absent, the system shall use the directory's base name.

Acceptance Criterion: A unit test parses two bookmarks, one with an explicit name, one without, and verifies the explicit name is used for the first and the directory base name for the second.

---

**REQ-F-009: Resolve Bookmark Paths (Absolute or Home-Relative)**

Bookmark paths accepted forms are `~/...` (home-relative, cleaned with `QDir::cleanPath`) and `/...` (absolute). Relative paths, `$VAR` references, and other non-absolute forms are rejected as invalid.

Acceptance Criterion: A unit test parses bookmarks with paths `~/Media`, `/mnt/data`, `relative/path` (invalid), `$HOME/subdir` (invalid), verifies the first two resolve correctly, and confirms the last two are rejected with warnings.

---

**REQ-F-010: Missing user-dirs.dirs File Has No Fallback**

If `user-dirs.dirs` is missing, the system shall silently show no XDG directories (only Home + bookmarks), with no fallback to `QStandardPaths` or defaults.

Acceptance Criterion: A unit test redirects XDG_CONFIG_HOME to a temp directory with no user-dirs.dirs, starts the app, and verifies the model contains only Home and any bookmarks, no XDG entries.

---

**REQ-F-011: Missing places.toml File Is Silent**

If `places.toml` is missing, the system shall silently proceed with no bookmarks, creating no file and emitting no output.

Acceptance Criterion: A unit test redirects XDG_DATA_HOME to a temp directory with no places.toml, starts the app, verifies the model includes Home and any XDG dirs, no bookmarks, and confirms no places.toml is created.

---

**REQ-F-012: Unparseable TOML Drops All Bookmarks with One Warning**

If places.toml exists but contains invalid TOML syntax, the system shall ignore all bookmarks in the file and emit exactly one warning to stderr including the file path, line number, and parse error description.

Acceptance Criterion: A unit test creates a places.toml with a syntax error (unclosed bracket, invalid table header), captures stderr, starts the app, verifies exactly one warning line appears naming the file and line, and confirms no bookmarks are loaded.

---

**REQ-F-013: Version Mismatch Drops All Bookmarks with One Warning**

If places.toml has a top-level `version` field not equal to 1, or the field is missing, the system shall ignore all bookmarks and emit one warning to stderr.

Acceptance Criterion: A unit test creates two TOML files, one with `version = 999` and one with no version field, for each verifies no bookmarks load and exactly one warning names the file and the version issue.

---

**REQ-F-014: Invalid Bookmark Entry Skips That Entry with One Warning**

If a bookmark entry is invalid (missing `path`, `path` is not a string, `path` is relative or contains `$VAR`), the system shall skip that entry, emit one warning to stderr, and continue loading other valid entries.

Acceptance Criterion: A unit test creates a places.toml with five bookmarks, one missing path, one with path as a number, one with relative path, two valid. Verifies the two valid bookmarks load, exactly three warnings appear, and the invalid ones are skipped.

---

**REQ-F-015: Unknown Keys in places.toml Emit Warnings But Don't Block**

Unknown keys at the top level or within a bookmark entry cause one warning per unknown key to stderr, the key is ignored, and the entry loads if otherwise valid.

Acceptance Criterion: A unit test creates a places.toml with `[extra_field]` at top level and a bookmark with `custom_icon = "star"`, verifies both warnings appear and the bookmark still loads.

---

### Deduplication and Filtering

**REQ-F-016: Deduplicate by Cleaned Path (No Symlink Resolution)**

Locations are deduplicated by their cleaned path string (via `QDir::cleanPath`), with no symlink canonicalisation. If two entries resolve to the same path, the first in priority order (Home > XDG in display order > Bookmarks in file order) is kept. Dropped XDG duplicates are silent; dropped bookmark duplicates warn (REQ-F-017).

Acceptance Criterion: A unit test with Home at `/home/u`, an XDG dir at `/home/u/Downloads`, and a bookmark at `/home/u/Downloads` verifies only the XDG entry is in the model; a second test with `XDG_DESKTOP_DIR` and `XDG_DOCUMENTS_DIR` both set to `$HOME/Stuff` verifies only Desktop is present and no warning is emitted.

---

**REQ-F-017: Emit Warning When Bookmark Duplicates Existing Place**

When a bookmark is dropped due to duplication, the system shall emit one warning to stderr naming the bookmark path and the place it duplicates.

Acceptance Criterion: A unit test creates a places.toml with a bookmark duplicating Home and two bookmarks with the same path, captures warnings, and verifies exactly two warnings: one naming the bookmark path and "Home", one naming the repeated bookmark path; the first of the two identical bookmarks remains.

---

**REQ-F-018: Show Only Readable Directories**

When its availability check resolves, an XDG directory whose path is not a directory shall be removed from the model; a bookmark whose path is not a readable directory shall be marked Unavailable and retained. Both appear with status Checking until then (REQ-F-019).

Acceptance Criterion: A unit test with an XDG dir pointing to a nonexistent path and a bookmark to a nonexistent directory verifies that after checks resolve the XDG dir is absent and the bookmark is present with status Unavailable.

---

### Availability Checking

**REQ-F-019: Ubiquitous — Place Status Initially Checking**

The system shall add all parsed places (Home, XDG, Bookmarks) to the model with status `Checking` immediately after parsing, before any filesystem checks complete.

Acceptance Criterion: A unit test verifies the model is populated with all places at status Checking on startup, before availability checks finish.

---

**REQ-F-020: Event-Driven — Checks Resolve Asynchronously**

When an availability check for a place completes on the worker thread, if the path is readable and is a directory, the status shall update to `Available`; if not readable or not a directory (XDG only), the entry is removed; if not readable or not a directory (bookmarks only), status updates to `Unavailable`.

Acceptance Criterion: A unit test with async mocking verifies the model transitions from Checking to Available/Unavailable/removed as checks complete, with no blocking on the GUI thread.

---

**REQ-F-021: Hung Check Does Not Block Other Checks**

If a check for one place hangs (e.g., dead NFS mount), other places' checks shall still resolve and update the model independently.

Acceptance Criterion: A unit test with two places, one returning a result in 10 ms and one blocking indefinitely, verifies the first place's status updates while the second remains Checking.

---

**REQ-F-022: Event-Driven — Bookmark Recheck on Activation**

When a bookmark of any status is activated while activation is enabled (REQ-F-037), the system shall re-check its path off the GUI thread. If it is a readable directory, the row status becomes Available and navigation proceeds via `DirectoryController.open(path)`; otherwise the row status becomes Unavailable, no navigation occurs, the current folder is unchanged, and the message "Location is currently unavailable" is shown through the controller's existing error/status path.

Acceptance Criterion: A test creates the directory of a bookmark that resolved Unavailable, activates it, and verifies currentPath becomes that directory and status becomes Available; a second test removes the directory of an Available bookmark, activates it, and verifies currentPath is unchanged, status becomes Unavailable, and the controller error text equals "Location is currently unavailable".

---

**REQ-F-023: Unavailable Bookmark UI Treatment**

Bookmarks with status `Unavailable` are rendered muted, display a warning icon, include "unavailable" in their accessible description, and remain focusable, reachable with Up/Down, and activatable (triggering the REQ-F-022 re-check).

Acceptance Criterion: A QML test verifies an Unavailable bookmark displays the warning icon, renders muted (delegate opacity 0.5, matching the design system's disabled look, since the shared list delegate's title colour is not overridable), has "unavailable" in its accessible description, and receives focus on Up/Down navigation.

---

### Model and API Exposures

**REQ-F-024: Ubiquitous — PlacesModel Exposes Required Roles**

The model shall expose roles: `name` (human-readable label), `path` (absolute path string), `iconName` (icon name or alias chain), `origin` (Home/XdgUserDirectory/Bookmark), and `status` (Checking/Available/Unavailable), both usable from QML.

Acceptance Criterion: A unit test retrieves each role from the model via `data(index, role)` and verifies the correct value for a test place of each origin and status.

---

**REQ-F-025: Origin Values**

The origin role shall take one of exactly three values: `Home`, `XdgUserDirectory`, or `Bookmark`.

Acceptance Criterion: A unit test verifies Home has origin `Home`, Desktop XDG dir has origin `XdgUserDirectory`, and a bookmark has origin `Bookmark`.

---

**REQ-F-026: Icon Name Role**

The `iconName` role shall provide a theme icon name (e.g., `user-home`, `folder-documents`) or an alias chain, with the bundled folder fallback applied at render time.

Acceptance Criterion: A code review confirms icon names are standard theme names; a QML test verifies the bundled fallback is applied if the theme icon is missing.

---

**REQ-F-027: Model Row Count and Indexing**

The model shall support `rowCount()` reflecting the current number of places (after filtering). Rows may be removed (XDG non-directories) or status-updated (bookmarks) on check completion.

Acceptance Criterion: A unit test verifies rowCount() matches the number of visible places, updates when rows are removed, and remains stable when status changes.

---

### Visual Design and Interaction

**REQ-F-028: Ubiquitous — Spacing Gap Before Bookmarks**

The visual layout shall include one extra spacing gap between the last XDG directory and the first bookmark, creating visual separation between the two sections.

Acceptance Criterion: A window test with one XDG place and two bookmarks verifies the vertical gap between the last standard row and the first bookmark row is larger than the gap between the two bookmark rows, by one compact internal-spacing token.

---

**REQ-F-029: No Separator Line or Bookmarks Heading**

The design shall not include a horizontal separator line between XDG directories and bookmarks, and shall not display a "Bookmarks" subheading.

Acceptance Criterion: A QML visual test scans the PlacesPanel component and verifies no Separator element exists and no Text element with "Bookmarks" is rendered.

---

**REQ-F-030: Preserved — Muted Places Heading**

The "Places" sidebar heading shall be rendered with the Caption typography role and the muted text colour, preserved from the prior spec.

Acceptance Criterion: A code review confirms PlacesPanel.qml renders the heading with muted color tokens; a window test verifies the `placesHeading` label's color equals the muted palette token.

---

**REQ-F-031: Preserved — Compact Delegates and Shared Styling**

All place rows (Home, XDG, Bookmarks) shall use identical, compact delegate styling and shared row height tokens, preserving prior styling.

Acceptance Criterion: A code review confirms one shared delegate is used for all origins; a QML visual test verifies all rows are identically sized and styled.

---

**REQ-F-032: Preserved — Theme Icons with Bundled Fallback**

All places shall display a theme icon via icon name, with the bundled folder fallback applied if the theme icon is unavailable, preserving prior behavior.

Acceptance Criterion: A code review confirms icon lookup uses theme + fallback; a QML visual test verifies the fallback is applied when the theme icon is missing.

---

**REQ-F-033: Preserved — Row Highlighting on CurrentPath Match**

A row shall be highlighted (e.g., background color, border) exactly when its path equals `DirectoryController.currentPath`, matching the prior behavior.

Acceptance Criterion: A QML test navigates to a folder in the Places sidebar, verifies that row is highlighted, navigates to a different folder via another UI element, verifies the highlight shifts to the new row.

---

**REQ-F-034: Preserved — Token Spacing and Inset Selection**

All place rows shall use consistent token-based spacing (margins, padding) and inset selection styling, preserved from the prior spec.

Acceptance Criterion: A code review confirms spacing values are from the shared token system; a QML visual test measures margins and padding in tokens.

---

**REQ-F-035: Preserved — Scrolling in Short Windows**

If the Places panel is too short to display all places, the panel shall scroll vertically, retaining the muted heading.

Acceptance Criterion: A QML test renders the Places panel at 300 px height with 15+ places and verifies vertical scrolling is enabled and the heading remains visible.

---

**REQ-F-036: Preserved — Accessible Names and Tab Focus**

Each place row shall have an accessible name (its label; an Unavailable bookmark additionally has "unavailable" in its accessible description) and shall be focusable via Tab and keyboard Up/Down navigation.

Acceptance Criterion: An accessibility test reads the accessible name of each place (Home, XDG, Bookmark, and Unavailable bookmark) and verifies the name equals the label and only the Unavailable bookmark's description contains "unavailable"; a focus test verifies Tab navigates among places and Up/Down changes the highlighted row.

---

**REQ-F-037: Preserved — Enter/Space Activation**

When a place row is focused and the user presses Enter or Space, and the app is in NORMAL mode with no task prompt and no Quick Look open, the place shall be activated (call `DirectoryController.open(path)`). Otherwise, activation shall be disabled. Pending bookmark results shall also
respect these guards at delivery: update the row status, but neither navigate nor
replace the status message while a guard is active.

Acceptance Criterion: A QML test focuses a place row, verifies activation on Enter/Space in NORMAL mode, verifies activation is disabled in INSERT mode or while a task prompt is shown, and verifies errors are preserved through `DirectoryController.open`.

---

**REQ-F-038: Preserved — Error Handling on Activation**

When a place is activated via `DirectoryController.open()`, errors (permission denied, I/O error, network timeout) shall be preserved, history updated, preview refreshed, and listing focus returned, matching the existing DirectoryController behavior.

Acceptance Criterion: Code review confirms no error suppression or re-wrapping; a QML test activates a place with a mocked DirectoryController error and verifies the error is surfaced unchanged and history/preview are updated.

---

## Non-Functional Requirements

**REQ-NF-001: Only Source-File Parsing Is Synchronous**

The system may parse user-dirs.dirs and places.toml synchronously at startup; the only filesystem access performed on the GUI thread for Places shall be reading those two files. Directory existence/readability checks shall not run on the GUI thread.

Acceptance Criterion: A test constructs the places model with an injected availability checker that records the calling thread, and verifies every check call ran on a thread other than the GUI thread.

---

**REQ-NF-002: Availability Checks Are Truly Asynchronous**

Availability checks (directory existence, readability) shall run off the GUI thread. The GUI thread shall never wait on a check, even if the check hangs.

Acceptance Criterion: A test injects a checker that blocks until released, and verifies a 50 ms GUI-thread timer keeps firing and model construction returns while the check is still blocked.

---

**REQ-NF-003: Model Updates Are Atomic Per-Row**

Status updates and row removal triggered by check completion shall occur atomically (one model signal per change), not with multiple intermediate updates.

Acceptance Criterion: A unit test with model signals connected counts emitted `dataChanged` and `rowsRemoved` signals and verifies no duplicate signals or intermediate inconsistent states.

---

**REQ-NF-004: Status Re-checks Do Not Repeat Warnings**

The system shall emit parse and deduplication warnings only while building the model at startup; availability checks and activation re-checks shall emit no warnings.

Acceptance Criterion: A test with one duplicate bookmark and one unavailable bookmark captures warnings, activates the unavailable bookmark twice, and verifies the warning count is unchanged after the activations.

---

## Constraints

**REQ-C-001: XDG Path Resolution**

Configuration paths (user-dirs.dirs, places.toml) shall use XDG Base Directory Specification rules: unset, empty, or non-absolute XDG_* variables fall back to `$HOME/.config`, `$HOME/.local/share`, etc. Paths are resolved once at startup; no dynamic re-resolution on XDG_* changes during runtime.

Acceptance Criterion: A unit test resolves paths with XDG_CONFIG_HOME unset, empty, set to a relative path, and set to an absolute path, and verifies the expected paths in each case.

---

**REQ-C-002: TOML Format Only for Bookmarks**

Bookmarks shall be read only from TOML files; no JSON, YAML, INI or other formats are probed or supported.

Acceptance Criterion: A code review confirms `places.toml` is the only expected file name and TOML is the only supported format.

---

**REQ-C-003: TOML Parsing Through Adapter Only**

The TOML library (tomlplusplus) shall be included only by the existing TOML adapter unit. No direct inclusion by Places code is permitted.

Acceptance Criterion: A code review of Places implementation verifies no `#include <toml++/toml.hpp>` or similar appears outside the designated adapter.

---

**REQ-C-004: No Symlink Canonicalisation**

Path deduplication is performed on the string path returned by `QDir::cleanPath()`, with no symlink resolution via `QFileInfo::symLinkTarget()` or `realpath()`.

Acceptance Criterion: A unit test creates a symlink `/tmp/link → /tmp/target`, adds both to the model, and verifies both are retained as separate entries.

---

**REQ-C-005: Home Never Removed or Unavailable**

Home (always `QDir::homePath()`) shall always be present in the model with status Available after its check completes. It is never filtered out, deduplicated, or marked Unavailable.

Acceptance Criterion: A unit test with Home at a nonexistent path (test seam) verifies Home is still in the model; code review confirms the check for Home never results in removal.

---

**REQ-C-006: Bookmark Status Transitions**

A startup result superseded by an activation re-check shall be discarded, with no
status change or signal, even if it arrives after that re-check completes.

Bookmarks start as Checking and resolve to Available or Unavailable; afterwards they change between Available and Unavailable only as a result of an activation re-check (REQ-F-022). XDG directories resolve from Checking to Available or are removed, and are never Unavailable.

Acceptance Criterion: A unit test removes an Available bookmark's directory and verifies its status stays Available until it is activated; a second test verifies an XDG dir whose check fails is removed and no row ever reports Unavailable for origin XdgUserDirectory.

---

**REQ-C-007: Exact Warning Counts**

The system shall emit exactly one warning per parse error, type mismatch, unknown key, invalid entry, version mismatch, or duplicate. No warnings for missing files, an XDG value equal to `$HOME`, or junk lines in user-dirs.dirs.

Acceptance Criterion: A unit test exercises all error conditions and counts warnings, verifying no spurious warnings and one per distinct error.

---

**REQ-C-008: Warnings to stderr Only**

All warnings (parse errors, duplicates, unknown keys, invalid entries) shall be emitted to stderr only. No parse/dedup warning appears on stdout or in the UI (the REQ-F-022 "Location is currently unavailable" message is a user-facing error, not a warning).

Acceptance Criterion: A unit test captures stdout and stderr separately, triggers several warning conditions, and verifies all warnings appear on stderr only.

---

**REQ-C-009: No File Creation or Modification**

The system shall never create, modify, or delete user-dirs.dirs, places.toml, or their parent directories. All operations are read-only.

Acceptance Criterion: A unit test with isolated XDG directories records file times and permissions before and after parsing, verifies no changes, and confirms no new files are created.

---

**REQ-C-010: DirectoryController Integration**

Activation of a place shall call `DirectoryController.open(path)`, preserving the controller's error handling, history updates, preview updates, and focus return semantics without wrapping or modification.

Acceptance Criterion: A code review confirms Places calls `open()` directly; an integration test verifies history and preview behavior match non-Places navigation.

---

## Behaviour Changes vs Current

1. **Configured-but-nonexistent XDG directories are now hidden** — Previously shown if configured in QStandardPaths; now only displayed if they are readable directories.
2. **Desktop, Templates, and Public directories are now shown** — Previously omitted; now displayed if their XDG_* entries exist and paths are readable.
3. **~/Projects is no longer hardcoded** — Previously shown if `~/Projects` existed; now shown only via `XDG_PROJECTS_DIR` or a bookmark.
4. **QStandardPaths is no longer used for places** — Previously used for six standard directories; now XDG user-dirs.dirs is the authoritative source.
5. **User-managed bookmarks are introduced** — New capability: users can add custom locations via places.toml.
6. **Asynchronous availability feedback** — Places now display Checking/Available/Unavailable status, with Checking showing immediately and status updating as checks complete (no visible change in typical scenarios, but slower mounts show visual feedback).
7. **Availability re-checks on bookmark activation** — When a bookmark is activated, its path is checked again (previously no availability checks existed).

## Known Risks

1. **Hung stat on dead network mount** — If a bookmark or XDG dir points to a dead NFS or sshfs mount, the check for that place may hang indefinitely. The design mitigates this by running checks on a worker thread and not blocking other checks, but a long hung check will leave the place in Checking state until the mount recovers or the application is killed.

2. **XDG_PROJECTS_DIR support only in newer xdg-user-dirs** — The `XDG_PROJECTS_DIR` entry is not universally supported in older xdg-user-dirs versions. Users on older systems will not have the Projects directory unless they add it manually as a bookmark.

3. **User edits to places.toml are not live-reloaded** — Changes to places.toml require an application restart. In-progress edits may cause parse errors if the file is being edited while the app reads it (TOCTOU).

4. **Future app writes to places.toml will lose user comments and formatting** — The TOML format supports comments and custom formatting. If the app writes to places.toml in a future cycle, the TOML library may not preserve these; user edits will be reconstructed on next app write, losing formatting and comments. This is not a blocker for this cycle (file is read-only) but is a known trade-off for future app writes.

5. **User-dirs.dirs parser supports only two forms** — Shell-escaped home-relative (`$HOME/...`) and absolute paths. Other forms (symlinks, relative paths, `$VAR` references other than `$HOME`) are silently ignored. This is intentional but may surprise users familiar with other tools' broader support.

## Acceptance Criteria Summary

Every REQ-F-XXX, REQ-NF-XXX, and REQ-C-XXX requirement above includes an inline, independently-verifiable acceptance criterion. Acceptance is contingent on:

1. **Unit tests:** Parser tests for user-dirs.dirs (valid/invalid forms, escaping, disabled entries, missing file), places.toml (valid/invalid TOML, version, entries, unknown keys, duplicates), deduplication (priority, warnings), and model initialization.
2. **Integration tests:** Async availability checking (transitions, hung checks, recheck on activation), DirectoryController integration (errors, history, preview), UI behavior (highlighting, accessible names, focus, activation).
3. **Code review:** Icon role, origin/status values, warning counts, file operations (read-only), TOML adapter inclusion, XDG path resolution, DirectoryController call semantics.
4. **QML visual/accessibility tests:** Heading muting, delegate styling, spacing gap, icon fallback, row highlighting, accessible names, Tab focus, Up/Down navigation.
5. **Build and task checks:** `task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint` all pass.

No requirement lacks an acceptance criterion. Implementation cannot proceed to code review until this spec is approved.

## Acceptance Scenarios Summary

1. **Minimal setup (no user-dirs.dirs, no places.toml):** Model shows only Home with status Available.
2. **Standard XDG setup (user-dirs.dirs present, bookmarks absent):** Model shows Home + available XDG dirs in order, all Available after checks.
3. **With bookmarks (user-dirs.dirs + valid places.toml):** Model shows Home + XDG dirs + bookmarks, spacing gap before bookmarks, all resolved.
4. **Bookmark duplication (bookmark path matches XDG dir):** Duplicate is dropped with one warning, model shows only the XDG entry.
5. **Invalid bookmark (relative path, missing path, wrong type):** Entry skipped with one warning, other bookmarks load.
6. **Unparseable TOML:** All bookmarks ignored, one warning, Home + XDG dirs still load.
7. **Nonexistent bookmark path:** Bookmark appears with status Unavailable, warning icon, re-checkable on activation.
8. **Hung mount (bookmark or XDG dir):** That entry remains Checking while others resolve.
9. **Activate unavailable bookmark:** Error message shown, no navigation, status remains Unavailable.
10. **Activate available place:** DirectoryController.open() called, history and preview updated, listing focus returned.
11. **Place row highlighting:** Row highlights when its path == currentPath, unhighlights on navigation.
12. **Existing build/test/format/tidy/qml-lint checks:** Pass with Places implementation.
13. **Manual native verification on Hyprland @ 1.5x fractional scale:** All rows render correctly, text is legible, icons display without scaling artifacts.

## Related Documents

- **CLAUDE.md** — Project state and relationship to HoloNight umbrella.
- **docs/sdd/places/SPEC.md** — Prior Places sidebar spec (P-003..P-007 behaviours preserved here).
- **docs/sdd/app-configuration/SPEC.md** — Configuration and state management (shared TOML adapter, XDG path resolution patterns).
- **apps/files/places_model.{h,cpp}** — Current Places model implementation (to be redesigned).
- **apps/files/PlacesPanel.qml** — Current Places sidebar UI (preserved styling, updated for new model roles).
- **apps/files/settings/xdg_paths.{h,cpp}** — Existing XDG path resolution helper (reused pattern).
- **apps/files/settings/toml_document.{h,cpp}** — Existing TOML adapter (reused for places.toml parsing).

