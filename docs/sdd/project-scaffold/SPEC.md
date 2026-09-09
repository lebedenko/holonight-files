# Project scaffold requirements

Status: requirements and design authorized by the user's direct instruction to
mimic the sibling holonight-viewer project's infrastructure, stack, and workflow.

| ID | EARS requirement |
| --- | --- |
| R1 | The system shall build a standalone C++23 / Qt 6.11+ executable holonight-files with CMake 3.25+ and installed HoloNight Core and Controls packages. |
| R2 | When launched without arguments, the system shall show a native-decorated HnApplicationWindow with a spacious canvas and centered “No folder open” HnEmptyState. |
| R3 | While the window is active, when f is pressed, the system shall toggle fullscreen; when Escape is pressed in fullscreen, it shall restore the preceding normal or maximized state, including compositor-managed tiling; when q is pressed, it shall quit. |
| R4 | The system shall display compact bottom hints for available fullscreen and quit actions using shared palette, typography, and geometry tokens. |
| R5 | When --help or --version is requested, the system shall report the requested information and exit successfully. If any positional argument is supplied or QML startup fails, the system shall exit nonzero. |
| R6 | The system shall select the HoloNight Quick Controls style from an embedded configuration without requiring style-selection environment variables. |
| R7 | When staged installation is requested, the system shall install the executable, desktop entry, SVG icon, and GPL license under GNU destinations, without advertising file MIME handling or accepting a file argument. |
| R8 | The repository shall provide debug/release/test presets, dependency preparation, build/run/test/format/tidy/QML/REUSE/install/desktop checks, and CI using the same tooling as holonight-viewer. |
| R9 | The repository shall preserve the mockup and record the SDD contributor sequence in tracked documentation and shall leave sibling and umbrella sources unchanged. |
| R10 | When resized, rendered in dark/light appearance, or displayed at fractional scale, the shell shall retain readable, unclipped content and ordinary window decorations. |

Non-goals: directory listing, navigation, selection, search, command mode,
file operations, previews/metadata, places/devices sidebar, auto-hiding
overlays, adaptive CSD/SSD, publishing, and umbrella registration. The mockup's
controls for these features are not implemented by this cycle.
