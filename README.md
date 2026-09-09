# HoloNight Files

A keyboard-driven, Vim-like HoloNight file manager for native Wayland. This
scaffold stage provides only a themed native-decoration shell: no directory
listing, navigation, or file operations yet. See [docs/BACKLOG.md](docs/BACKLOG.md)
for the planned stages and [docs/mockups/moc1.png](docs/mockups/moc1.png) for
visual direction.

Requires C++23, Qt 6.11+, CMake 3.25+, Ninja, Task, and installed
HolonightQt::Core / HolonightQt::Controls. Tests use Qt Test and GTest. Checks
need clang-format, clang-tidy (run-clang-tidy), REUSE, desktop-file-utils and
Python 3. On Arch, the [CI Dockerfile](packaging/Dockerfile.ci) lists the
packages.

```sh
task deps                 # builds sibling providers locally, without source changes
task build
task run
# f toggles fullscreen, Escape leaves fullscreen, q quits
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

`--help` and `--version` are supported. This build does not yet accept a folder
argument; any positional argument is rejected.

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
