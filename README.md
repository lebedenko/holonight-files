# HoloNight Files

A keyboard-driven, Vim-like HoloNight file manager for native Wayland. Stage 1
("Browse a folder") has its implementation and regression coverage in place;
acceptance status is tracked in [browse-folder verification](docs/sdd/browse-folder/VERIFICATION.md). It provides an asynchronously populated, naturally
sorted directory listing with a fixed places sidebar, live filesystem
watching, and NORMAL-mode keyboard navigation (`j`/`k`, count-prefixed
motions, `gg`/`G`, `h`/`l`/Enter, `.` for hidden files, `s` to reverse filename sorting).
The compact Places sidebar has three sources, in this order: Home; the XDG user
directories declared in `${XDG_CONFIG_HOME:-~/.config}/user-dirs.dirs`
(Desktop, Documents, Downloads, Pictures, Music, Videos, Projects, Templates,
Public — shown only for the ones actually configured, with no fallback if
that file is missing, so `~/Projects` appears only via its own
`XDG_PROJECTS_DIR` entry or a bookmark); and user-managed bookmarks read from
`$XDG_DATA_HOME/holonight/holonight-files/places.toml` (`~/.local/share/...` when
`XDG_DATA_HOME` is unset, empty, or not absolute). Files never creates,
writes, or watches either file — edit them yourself (or with
`xdg-user-dirs-update` for the first) and restart to apply changes.

`places.toml` looks like:

```toml
version = 1

[[bookmarks]]
path = "/mnt/data"
name = "Data Drive"   # optional; defaults to the path's directory name

[[bookmarks]]
path = "~/Projects/side-project"
```

Each bookmark's `path` must be absolute or `~/`-relative; other forms (a bare
relative path, `$VAR` references) are rejected. A missing `places.toml` is
silent. An unparseable file, a missing or wrong `version`, an invalid entry,
or an unknown key each produce one warning on stderr; other valid bookmarks
still load. A directory configured via `user-dirs.dirs` that turns out not to
exist is simply hidden, exactly like a bookmark whose target does not exist
except that a missing bookmark stays listed, shown muted with a warning badge
("unavailable"); activating it re-checks the path and either navigates or
shows a status message, without disturbing the current folder. Two entries
resolving to the same cleaned path are deduplicated (Home, then XDG, then
bookmarks in file order — the first wins); symlinks are never canonicalised
for this comparison. Tab into Places, move with Up/Down, and activate with
Enter/Space or a click; activation requires NORMAL mode without a prompt or
Quick Look. Navigation returns focus to the listing and participates in
history. See the [places sources specification](docs/sdd/places-sources/SPEC.md)
and the prior [Places verification](docs/sdd/places/VERIFICATION.md).
Stage 2 ("Inspect a selection") is also implemented: a docked preview pane
(resizable via a divider next to the listing, down to 220px) shows a rounded
thumbnail sized to the image's aspect ratio (up to 240px tall), the name, a
human-readable type ("JPEG image"), and a Size/Dimensions/Modified table.
Images with EXIF data add a Camera/Lens/Aperture/Exposure/ISO/Focal length
table. Both tables share left-aligned label and value columns. Rows without a value are hidden, and long values wrap instead of being
cut off. The sidebar scrolls vertically when its content exceeds the window height.
Size text matches the listing exactly, and folders show `Dir`. The
sidebar no longer shows text content or permissions; press `Space` to open
Quick Look, a centered card over the dimmed window (at most 92% of it). It opens
only for images and `text/plain` files; on directories, archives and other types
(including JSON and Markdown) `Space` does nothing. Images are framed at their own
aspect ratio. Text files get a read-only monospace line viewer with a line-number
gutter and a highlighted current line: at most the first 100 KiB is loaded (the
caption then adds `· truncated`), `LF`, `CRLF` and `CR` all end a line, invalid
UTF-8 shows as `�`, and an empty file shows one empty line. Long lines are clipped,
not wrapped. Unreadable files and undecodable images get a compact icon card with
the error. Below the preview it shows the name, a metadata line
(`6000 × 4000 · 8.3 MB`, the size, or the error) and a close hint.

Quick Look is pinned to the file it was opened on. In a text file `j`/`k` (or
`ArrowDown`/`ArrowUp`) move the current line and the view follows it; the mouse wheel
scrolls without changing it. They never move the listing cursor or switch the
preview to another file, and every other key is ignored. Close with `Space` or
`Escape`, move in the listing, and press `Space` again to view another file. If
the listing changes under the pinned file, Quick Look closes. See the
[Quick Look text viewer spec](docs/sdd/quick-look-text-viewer/SPEC.md) and its
[design](docs/sdd/quick-look-text-viewer/DESIGN.md); the earlier
[Quick Look verification](docs/sdd/quick-look-redesign/VERIFICATION.md) predates the
pinned behavior (its live-update-on-`j`/`k` evidence is superseded) but still records
pending native acceptance.
Quick Look's C++ presentation model owns classification, retained geometry and
decode-size requests; QML supplies measurements and renders the card. See the
[C++ presentation verification](docs/sdd/quick-look-cpp-presentation/VERIFICATION.md).
Shared size formatting, per-engine failed-icon tracking and inspection key routing
also use C++ QML singletons; embedded QML handlers retain presentation and event
acceptance. See [helper migration verification](docs/sdd/javascript-to-cpp/VERIFICATION.md).
See the [info sidebar redesign verification](docs/sdd/info-sidebar-redesign/VERIFICATION.md)
for evidence and pending manual acceptance. Image thumbnails are cached per the
freedesktop Thumbnail Managing Standard in on-demand 128/256/512/1024px tiers
under `$XDG_CACHE_HOME/thumbnails/`. Only the needed tier is generated; adequate
larger entries are reused. Views above 1024px decode directly into memory.
Space is consumed before delegate button activation; Enter/`l` still opens files.
Previews use one verified regular-file descriptor, display-quality image
updates before EXIF, bounded EXIF reads, and a two-entry/64 MiB image cache.
New selections show the icon/loading state until adequate pixels are ready;
resizing retains the current image during upgrades. Small originals stay at native
resolution. See the [sharp previews cycle](docs/sdd/sharp-previews/VERIFICATION.md). Selected-file
changes refresh previews without moving the cursor. See the
[inspection verification](docs/sdd/inspect-selection/VERIFICATION.md) for evidence
and remaining acceptance limitations.
Stage 3 ("Modal editing") adds a Vim-style NORMAL/VISUAL/SEARCH/INSERT mode state
machine. `v`/`V` enters VISUAL for range selection (`j`/`k`/`gg`/`G` extend it, a
status-bar counter tracks the count; no operation consumes the selection yet).
`/` enters SEARCH: a live fuzzy jump-to-match with substring highlighting, `n`/`N`
to cycle committed matches (with wraparound) in NORMAL after `Enter`; SEARCH accepts
literal n/N. `Escape` restores the original entry by filename identity. `i`/`I`/`a`/`A` rename the selected entry
in place (cursor at the start/end); pressing `Enter` with the name unchanged just
touches its modification time, a changed name renames it, `Escape` discards the
edit with no side effect. `o`/`O` create a new file or folder inline, positioned
immediately below/above the cursor regardless of alphabetical order; a name ending
in `/` creates a directory. Invalid names (parent traversal, nested paths, empty,
reserved, too long, or colliding) are rejected live with a status-bar message and
a recolored input, and permission failures surface only at commit time. There is
Folder navigation cancels unfinished edits and clears modal/search state. Listings
and sort/filter settings stay stable during INSERT; commit/cancel refreshes external
changes. Creation is exclusive, and touch changes the entry’s own mtime, including
symlinks. NORMAL Escape closes Quick Look first, then retains fullscreen exit. There is
still no `:` command palette — see
[modal editing verification](docs/sdd/vim-modal-editing/VERIFICATION.md) for scope
and known limitations.
Stage 4 ("File operations") adds asynchronous, cancellable copy/move/trash over a
single clipboard register: `yy`/`dd` (NORMAL, doubled key) yank/cut the entry under
the cursor; `y`/`d` (VISUAL, single key) act on the whole selection and return to
NORMAL; `p` pastes into the currently displayed directory (never the cursor item);
`D` (NORMAL or VISUAL) trashes with a mandatory one-key confirmation. Name
collisions pause the operation with an inline skip/overwrite/auto-rename/cancel
prompt; only the top-level collision prompts, nested collisions inside a
recursive copy resolve automatically as skip. `Ctrl+C` cancels the in-flight
operation from any mode, including focused editors. Escape declines trash confirmation
and leaves conflict prompts unresolved. Incomplete directory moves retain the entire
source tree; completed destination copies remain and the summary explains skipped
children and errors. Copies stage file and symlink replacements so failure or
cancellation preserves the previous destination.
Trash uses validated home or per-partition `.Trash/$uid` / `.Trash-$uid` storage.
If validation, metadata creation or relocation fails, the item remains untouched and
the summary reports why. Permanent deletion is unavailable. Progress, prompts, and
the completion summary render inline in the existing status bar; there is no `:` command
palette. See [file operations verification](docs/sdd/file-operations/VERIFICATION.md)
for evidence and known gaps.
See [docs/BACKLOG.md](docs/BACKLOG.md) for the planned stages and
[docs/mockups/moc1.png](docs/mockups/moc1.png) for visual direction.

The listing prioritizes filenames when space is limited, hiding Modified and then
Size columns. Each row begins with a Vim-style hybrid line-number gutter: the cursor
row shows its absolute 1-based number and every other row its distance from the cursor,
which matches count prefixes such as `5j`. The gutter is at least three digits wide,
grows with the entry count, and is always shown. The header breadcrumb lines up with the
gutter's left edge. The column-hiding widths do not yet account for the gutter, so
filenames are narrower in small windows. See the [layout verification record](docs/sdd/app-window-layout/VERIFICATION.md)
for review fixes and remaining minimum-window limitations.

Each listing row starts with a file-type icon, and the preview pane shows the same
icon (up to 128px) when no thumbnail is available. Icons come from the icon theme
Qt already selected (`QIcon::fromTheme`), matched by filename and extension, never
by reading file contents. When the theme has no match, bundled folder and generic-file
glyphs are drawn in the palette colours instead. Files never changes the icon theme
or its search paths; an icon theme installed while Files is running is picked up
after a restart. See the [icons verification record](docs/sdd/main-view-icons/VERIFICATION.md).

Navigation history works like Vim's jump list: `Ctrl+O` goes back and `Ctrl+I` goes forward
through previously visited folders (count prefixes such as `3 Ctrl+O` work), as do the back/forward
arrow buttons at the left of the header. Returning to a folder puts the cursor back on the entry it
was on, and `h` places the cursor on the folder you just left. Revisiting a folder moves it to the
end of the history instead of discarding forward entries. History holds up to 100 folders, lives in
memory only (not kept across restarts), works in NORMAL mode only, and pauses while a prompt or Quick Look is open.
Folders that no longer exist are skipped and dropped, with a "Skipped missing" status message.
See the [navigation history specification](docs/sdd/navigation-history/SPEC.md) and
[verification record](docs/sdd/navigation-history/VERIFICATION.md).

The footer status line starts with a pill badge naming the current mode — NORMAL, VISUAL,
SEARCH or INSERT — filled blue, violet, yellow or green respectively. It is display-only:
it takes no clicks and no keyboard focus, and it keeps a fixed width so the rest of the
status line never shifts as you change modes. Because the badge names the mode, the labels
beside it no longer repeat it: VISUAL shows just the selection count and INSERT just the
commit hint or the validation error. The badge stays visible, showing NORMAL, while a task
runs or a confirmation prompt is open. Its text contrast is verified against the default
dark scheme only. See the [mode status badge specification](docs/sdd/mode-status-badge/SPEC.md)
and [verification record](docs/sdd/mode-status-badge/VERIFICATION.md) for automated evidence
and pending native visual and accessibility acceptance.

### Configuration and remembered location

Files reads an optional `config.toml` once at startup from
`$XDG_CONFIG_HOME/holonight-files/config.toml`, or `~/.config/holonight-files/config.toml`
when `XDG_CONFIG_HOME` is unset, empty or not an absolute path. Files never creates or
changes this file; edit it yourself and restart to apply changes. One setting exists:

```toml
[general]
restore_last_location = true   # default: false
```

With it enabled and no folder argument, Files reopens the last folder you had open when it
last closed normally. Only local folders are remembered: network (NFS, SMB, SSHFS, GVFS, ...)
and removable (USB, `/media`, `/run/media`) folders are skipped, and the most recent local
folder is kept instead. A folder argument always wins. If the remembered folder is gone,
not a directory, unreadable or no longer local, Files opens your home folder and the status
bar says why (for example "last location does not exist"). The location is saved to
`$XDG_STATE_HOME/holonight-files/state.toml` (default `~/.local/state/holonight-files/`)
only on a normal close and only while the setting is enabled. When several windows close,
the last one wins. A crash or kill leaves the previous state unchanged.

A missing config file means defaults with no messages. An unparseable file is ignored as a
whole with a single warning on stderr giving the path and line. A value of the wrong type
falls back to its default, and unknown keys or sections are ignored. Each of these gets its
own stderr warning. An unusable `state.toml` is also reported on stderr and treated as no
remembered location. See the [app configuration specification](docs/sdd/app-configuration/SPEC.md).

Requires C++23, Qt 6.11+ (including the Svg component), CMake 3.25+, Ninja,
Task, libexif (via pkg-config), tomlplusplus 3.4+ (shared library, CMake config), and installed HolonightQt::Core /
HolonightQt::Controls. Tests use Qt Test and GTest. Checks need clang-format,
clang-tidy (run-clang-tidy), REUSE, desktop-file-utils and Python 3.
`task isolated-runtime-check` additionally needs Docker and a locally built
`files-ci` image (`docker build -t files-ci -f packaging/Dockerfile.ci .`).
On Arch, the [CI Dockerfile](packaging/Dockerfile.ci) lists the packages.

```sh
task deps                 # builds sibling providers locally, without source changes
task build
task run
# f toggles fullscreen, Escape leaves fullscreen (preserving tiling), q quits
# Space opens/closes Quick Look for an image or text/plain file; j/k then move its current line
task test
task build PRESET=release
task format-check
task tidy
task qml-lint
task lint                 # tidy + qml-lint
task license-check
task install-check
task check                # (alias: verify) build×2, test, format-check, lint, license-check, install-check, QML policy/metadata, in order
task visual-check         # captures under build/visual for inspection
task isolated-runtime-check  # builds Release, verifies the staged payload in a network-isolated Docker container
task clean                   # removes build/{debug,release,test}; preserves build/deps and check evidence

# Coordinated system installation/removal is owned by the umbrella:
# ../scripts/install.sh / ../scripts/uninstall.sh (after published onboarding)
```

`--help` and `--version` are supported, along with an optional positional
folder argument (`task run -- ~/Downloads`, or `hn-files ~/Downloads`
directly): a missing, nonexistent, non-directory, or unreadable path falls
back to the home directory with a status message explaining why. More than
one positional argument is rejected.

Provider defaults are ../holonight-config and ../holonight-qt. Override their
locations with HOLONIGHT_CONFIG_SOURCE and HOLONIGHT_QT_SOURCE for `task deps`.
Builds and the staging prefix live under build/deps. Files and dependency builds
use CMake/Ninja automatic parallelism by default, including in CI. Ninja chooses
its worker count; this is not necessarily exactly the logical CPU count.
Dependency providers remain sequential because later providers use earlier
installations. `task check`, `task lint`, and tidy's prerequisite build also run
in sequence, while each build or analysis stage can use the available CPUs.

Use explicit limits when needed:

```sh
CMAKE_BUILD_PARALLEL_LEVEL=2 task build  # limit Files compiler jobs
CMAKE_BUILD_PARALLEL_LEVEL=2 task deps   # limit provider compiler jobs
JOBS=2 task deps                        # explicit provider limit; overrides CMake's environment limit
cmake --build --preset debug --parallel 2  # direct CMake override
```

Unset or empty `JOBS` adds no parallel option, so CMake's environment override
or Ninja's default applies. `JOBS` only affects dependency builds.
`run-clang-tidy` keeps its default of all detected CPUs: compiler job limits
(`JOBS`, `CMAKE_BUILD_PARALLEL_LEVEL`, or build `--parallel`) do not limit tidy
workers. See the [build parallelism verification](docs/sdd/automatic-parallelism/VERIFICATION.md)
for observed results; automatic scheduling does not promise a specific speedup.

Override HOLONIGHT_DEPENDENCY_PREFIX and
HOLONIGHT_QML_IMPORT_PATH as Task variables for an existing installation.
Direct CMake users can set those cache variables with `cmake --preset debug
-D...`; CMAKE_PREFIX_PATH and QML_IMPORT_PATH select providers without being overridden by generic CMake. The
debug/release/test presets default to the local prefix. Task run/test set the
installed QML and library search paths. Each build checks the selected Config and Qt provider
Git revisions against `build/deps/provider-revisions.tsv`; changed providers are rebuilt before
configuration, while unchanged providers are reused.

System installation and removal are owned by the umbrella's `scripts/install.sh` and
`scripts/uninstall.sh`. The installer rejects unmanaged payload collisions; it does not
adopt or remove legacy Files installations. The uninstaller preserves modified owned files.
Standalone builds and `DESTDIR` staging remain supported. Complete onboarding requires a
published Files revision and an explicit umbrella pin update.

CI runs deps, gives files-test ownership of build/, and runs check in C.UTF-8
and the additional en_US.UTF-8 tests. It prepares the runtime
context inside the build container and builds/runs the runtime image from the
runner using the repository-relative `build/runtime-check-context` marker.
The runtime container has no workspace or Docker-socket mounts and no network;
CI retains verification logs as an artifact.

A manual staged install remains available with
`DESTDIR=/your/stage cmake --install build/release` (prefix /usr). The
application package installs its executable, desktop entry, icon, and
license; it depends on separately installed HoloNight provider libraries and
QML modules. A system installation discovers providers via Qt's normal
module paths. A custom provider prefix needs
QML_IMPORT_PATH=<prefix>/lib/qt6/qml and LD_LIBRARY_PATH=<prefix>/lib. No
source-tree imports are embedded in the binary.

See [contributor workflow](CONTRIBUTING.md), [project brief](docs/PROJECT_BRIEF.md),
[backlog](docs/BACKLOG.md), and
[scaffold verification](docs/sdd/project-scaffold/VERIFICATION.md). Licensed
GPL-3.0-or-later; see LICENSE.

`task run` builds and launches the selected preset directly; it does not
register any desktop entry, and `${XDG_DATA_HOME:-~/.local/share}` is left
untouched. Desktop integration is provided by the installed application.

CI builds the committed checkout with contributor tools, stages Files and
providers under `/usr`, then runs the same checks as
[holonight-viewer](https://github.com/lebedenko/holonight-viewer)'s workflow.
No release has been published and no distribution packages are supplied.


Counts accept ASCII `0`–`9`, saturate at `INT_MAX`, and clamp movement to the listing
bounds. A zero count moves zero rows. Both toggles consume pending counts and `g`
chords. Descending sorting reverses the complete order, including directory grouping.
Functional CI runs under `C.UTF-8` and `en_US.UTF-8` as an unprivileged user; local
root runs explicitly skip permission-denied checks.

Native performance acceptance is opt-in on an active Wayland desktop with Qt's
hardware renderer, installed HoloNight packages, and no competing build/load work.
After `task test`, run the test executable directly (CTest selects offscreen):

```sh
QT_QPA_PLATFORM=wayland QSG_INFO=1 LC_ALL=en_US.UTF-8 \
  QML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml" \
  LD_LIBRARY_PATH="$PWD/build/deps/prefix/lib" \
  FILES_BROWSE_BENCHMARK=1 build/test/tests/files-smoke \
  '--gtest_filter=DirectoryPerformance.*' \
  --gtest_output=xml:build/browse-performance.xml
```

The benchmark increases real fixtures from 12,000 to 48,000 and 192,000 entries.
It requires first rendered populated rows within 150ms, active-scrolling frame gaps
≤100ms, and posted-key cursor latency ≤100ms while loading, with at least ten input
and scrolling-frame samples and 200ms loading time. Insufficient samples are
inconclusive; watcher refresh is timed separately. Record hardware, renderer,
locale, commands, and results with the verification record. Offscreen runs and
`task visual-check` screenshots under `build/visual/` provide regression evidence,
not native rendering acceptance. Provider and container-image pinning are deferred.

Inspection performance acceptance (101 images, each >5 MB, native renderer):

```sh
QT_QPA_PLATFORM=wayland \
  QML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml" \
  LD_LIBRARY_PATH="$PWD/build/deps/prefix/lib" \
  FILES_NATIVE_ACCEPTANCE="$PWD/build/native-acceptance.txt" \
  build/test/tests/files-smoke --gtest_filter=Files.NativeInspectionAcceptance
```

The result records initial/cached/Quick Look latency, stale-image count, movement
rate, process peak RSS and rendered FPS. Qt image decoding remains cooperatively
cancellable between stages; the three-second timeout is a visible deadline, not
hard preemption of a decoder call.

### Executable migration and folder handling

The supported command is `hn-files`; no `holonight-files` alias is installed.
Existing unmanaged installations must be resolved explicitly by their owner before a coordinated install.

The installed desktop entry advertises `inode/directory` and launches
`hn-files -- %f`. Files can be selected in the desktop's default file-manager
chooser; installation does not change your default. After reinstalling, use
`gio mime inode/directory` to check that `org.holonight.Files.desktop` appears.
Folder handling accepts one local folder, including spaces and Unicode, using
the existing startup fallback for invalid folders. Remote URIs and multiple
folders are outside this integration.

See [folder-handler verification](docs/sdd/folder-handler/VERIFICATION.md) for
automated evidence and pending host chooser/display acceptance.

## Architecture and ownership

Private C++ code is organized under `apps/files/{application,browsing,operations,preview,settings,state,places}`.
`DirectoryController` coordinates navigation, editing, command, preview and lifecycle sessions while preserving
its QML API. `presentation` owns Qt Quick integration; `qml` groups listing, places, inspection and status surfaces.
Each UI executable owns a HolonightFiles QML module and calls `initializeFilesEngine` before loading QML.
[Alignment specification and verification](docs/sdd/holonight-alignment/README.md) records the onboarding work.

For isolated runtime checks with verified existing provider artifacts, `scripts/prepare-runtime-check.sh`
accepts `HOLONIGHT_CONFIG_BUILD` and `HOLONIGHT_QT_BUILD`; defaults remain `build/deps/<provider>`.
Verify provider revisions, build options and the exact Qt package versions against the runtime image first.

## Shared raster processing

Build and install `holonight-images` before configuring, or use `task deps` with a sibling checkout
(`HOLONIGHT_IMAGES_SOURCE` overrides its location). `find_package(HolonightImages CONFIG REQUIRED)`
provides `HolonightImages::Images`; custom builds pass its prefix through `CMAKE_PREFIX_PATH`.
The provider owns raster decoding and structured EXIF extraction; presentation and scheduling remain here.
See [migration SDD](docs/sdd/shared-image-architecture/DESIGN.md).
