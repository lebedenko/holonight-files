# HoloNight Files

A keyboard-driven, Vim-like HoloNight file manager for native Wayland. Stage 1
("Browse a folder") has its implementation and regression coverage in place;
acceptance status is tracked in [browse-folder verification](docs/sdd/browse-folder/VERIFICATION.md). It provides an asynchronously populated, naturally
sorted directory listing with a fixed places sidebar, live filesystem
watching, and NORMAL-mode keyboard navigation (`j`/`k`, count-prefixed
motions, `gg`/`G`, `h`/`l`/Enter, `.` for hidden files, `s` to reverse filename sorting).
Stage 2 ("Inspect a selection") is also implemented: a docked preview pane
(resizable via a divider next to the listing) shows name/size/date/
permissions/MIME type, an EXIF summary for images, and a text preview with a
truncation notice for large files; press `Space` to open the same preview in
a full-window Quick Look overlay, which live-updates as you move with `j`/`k`
and closes on `Space`/`Escape`. Image thumbnails are cached per the
freedesktop Thumbnail Managing Standard (`$XDG_CACHE_HOME/thumbnails/normal/`).
Space is consumed before delegate button activation; Enter/`l` still opens files.
Previews use one verified regular-file descriptor, progressive thumbnail/full-image
updates, bounded EXIF reads, and a two-entry/64 MiB full-image cache. Selected-file
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
Size columns. See the [layout verification record](docs/sdd/app-window-layout/VERIFICATION.md)
for review fixes and remaining minimum-window limitations.

Requires C++23, Qt 6.11+ (including the Svg component), CMake 3.25+, Ninja,
Task, libexif (via pkg-config), and installed HolonightQt::Core /
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
# Space opens/closes Quick Look for the entry under the cursor
task test
task build PRESET=release
task format-check
task tidy
task qml-lint
task lint                 # tidy + qml-lint
task license-check
task install-check
task uninstall-check      # verifies scripts/uninstall.sh in disposable staged trees; no sudo, no real /usr
task check                # (alias: verify) build×2, test, format-check, lint, license-check, install-check, uninstall-check, in order
task visual-check         # captures under build/visual for inspection
task isolated-runtime-check  # builds Release, verifies the staged payload in a network-isolated Docker container
task clean                   # removes build/{debug,release,test,system-install}; preserves build/deps and check evidence

# System install/uninstall (require sudo and system-wide HoloNight providers under /usr; host-mutating, not run in CI):
task install                 # configures+builds the system-install preset against /usr, installs, refreshes desktop database
task uninstall                 # removes the installed payload from /usr and refreshes the desktop database (uninstall before reinstalling)
```

`--help` and `--version` are supported, along with an optional positional
folder argument (`task run -- ~/Downloads`, or `holonight-files ~/Downloads`
directly): a missing, nonexistent, non-directory, or unreadable path falls
back to the home directory with a status message explaining why. More than
one positional argument is rejected.

Provider defaults are ../holonight-config and ../holonight-qt. Override their
locations with HOLONIGHT_CONFIG_SOURCE and HOLONIGHT_QT_SOURCE for `task deps`.
Builds and the staging prefix live under build/deps. Set JOBS to control
provider parallelism. Override HOLONIGHT_DEPENDENCY_PREFIX and
HOLONIGHT_QML_IMPORT_PATH as Task variables for an existing installation.
Direct CMake users can set those cache variables with `cmake --preset debug
-D...`; CMAKE_PREFIX_PATH can supply additional installed packages. The
debug/release/test presets default to the local prefix. Task run/test set the
installed QML and library search paths.

System installation is `task install`: it configures and builds Release
against installed `/usr` providers (the `system-install` CMake preset, not
the development dependency prefix — HoloNight providers must already be
installed system-wide first), installs the five payload files to `/usr` with
`sudo`, and refreshes the desktop database. `task uninstall` removes that
payload and refreshes the desktop database again; run it before a repeat
`task install` to avoid partial-overwrite states (uninstall-before-reinstall).
Both require `sudo` and mutate the real system, so neither runs in CI — CI
instead exercises the install path via `task install-check` (a staged,
`DESTDIR`-based install with no `sudo`) and the uninstall path via
`task uninstall-check` (mocked privileged commands, no real `/usr` writes).
`task isolated-runtime-check` builds Release, stages its payload plus the
provider builds under `DESTDIR`, and verifies the result as the ordinary files-test user — five regular root:root files with
0755 executable and 0644 asset/license modes, no development RPATH/RUNPATH, `--version`, desktop entry validity, and desktop
launch surviving three seconds after discovery (up to three seconds) — inside a disposable, network-isolated Docker container built `FROM`
the CI image; it requires Docker and `task deps` having already produced
`build/deps/holonight-config`/`build/deps/holonight-qt`. `task clean` removes
only `build/{debug,release,test,system-install}`, preserving
`build/deps/prefix` and check evidence. The full local pipeline is
`task deps` → `task check` (alias `task verify`) → `task isolated-runtime-check`.
After building the runtime image, `python3 scripts/check-runtime-fixtures.py` exercises
healthy, delayed-exit, missing-file, permission, ownership, and pre-existing-process cases
in disposable containers, retaining logs under `build/runtime-fixtures.*`.

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
