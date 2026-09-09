# HoloNight Files

A keyboard-driven, Vim-like HoloNight file manager for native Wayland. Stage 1
("Browse a folder") has its implementation and regression coverage in place;
acceptance status is tracked in [browse-folder verification](docs/sdd/browse-folder/VERIFICATION.md). It provides an asynchronously populated, naturally
sorted directory listing with a fixed places sidebar, live filesystem
watching, and NORMAL-mode keyboard navigation (`j`/`k`, count-prefixed
motions, `gg`/`G`, `h`/`l`/Enter, `.` for hidden files, `s` to reverse filename sorting). File
operations, search, selection, and command mode are not implemented yet. See
[docs/BACKLOG.md](docs/BACKLOG.md) for the planned stages and
[docs/mockups/moc1.png](docs/mockups/moc1.png) for visual direction.

Requires C++23, Qt 6.11+, CMake 3.25+, Ninja, Task, and installed
HolonightQt::Core / HolonightQt::Controls. Tests use Qt Test and GTest. Checks
need clang-format, clang-tidy (run-clang-tidy), REUSE, desktop-file-utils and
Python 3. On Arch, the [CI Dockerfile](packaging/Dockerfile.ci) lists the
packages.

```sh
task deps                 # builds sibling providers locally, without source changes
task build
task run
# f toggles fullscreen, Escape leaves fullscreen (preserving tiling), q quits
task test
task build PRESET=release
task format-check
task tidy
task qml-lint
task license-check
task install-check
task desktop-check        # isolated development/packaged registration checks
task visual-check         # captures under build/visual for inspection
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

Install with `DESTDIR=/your/stage cmake --install build/release` (prefix
/usr). The application package installs its executable, desktop entry, icon,
and license; it depends on separately installed HoloNight provider libraries
and QML modules. A system installation discovers providers via Qt's normal
module paths. A custom provider prefix needs
QML_IMPORT_PATH=<prefix>/lib/qt6/qml and LD_LIBRARY_PATH=<prefix>/lib. No
source-tree imports are embedded in the binary.

See [contributor workflow](CONTRIBUTING.md), [project brief](docs/PROJECT_BRIEF.md),
[backlog](docs/BACKLOG.md), and
[scaffold verification](docs/sdd/project-scaffold/VERIFICATION.md). Licensed
GPL-3.0-or-later; see LICENSE.

`task run` registers the selected development build's desktop entry and icon
under `${XDG_DATA_HOME:-~/.local/share}` before launch, so the host portal can
resolve `org.holonight.Files`. Use `task desktop-install` to register without
opening a window. The entry launches the absolute build executable with its
provider paths; rerun the task after moving the checkout or switching builds.
This user entry takes precedence over a system installation; remove its
applications/org.holonight.Files.desktop file when switching to a system
package.

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
