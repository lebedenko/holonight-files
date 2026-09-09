# Scaffold design

This design mirrors holonight-viewer's project-scaffold cycle: the same build
system, task runner, tooling configuration, packaging shape, and CI structure,
adapted from an image viewer's identity to a file manager's.

apps/files contains a small executable and the HolonightFiles QML module.
main.cpp sets identity org.holonight.Files, handles CLI options, rejects
positional arguments (no folder-opening support yet), and reports QML creation
failure through a queued nonzero exit. QML owns only shell presentation and
shortcuts; there is no document/model type at this stage.

Main.qml uses HnApplicationWindow's native-decoration default, a 1000×700
initial canvas with a 420×280 minimum, centered HnEmptyState, and bottom
HnLabel captions with shared caption typography, muted palette color, and
HnMetrics spacing. No directory state or mock controls are created. Fullscreen
remembers maximized state, matching holonight-viewer's toggle behavior.

qtquickcontrols2.conf is embedded at the resource root, reused unchanged from
holonight-viewer since style selection is a shared, project-agnostic concern.
Installed provider QML modules are dynamically discovered; interface CMake
targets do not bundle them. Import scanning for static plugin linkage is
disabled because providers are installed shared modules. Development tasks
supply installed-prefix runtime paths and CMake supplies lint imports. System
packages use normal Qt module lookup. Custom prefixes require explicit runtime
search paths; the executable embeds no development paths.

Provider preparation reuses holonight-viewer's scripts/prepare-deps.sh
unchanged: it builds config then qt into project-local build/deps directories,
using ../holonight-config and ../holonight-qt by default. Unlike
holonight-viewer's current CI, no provider revision is pinned yet — this
scaffold's CI checks out each provider's default branch; pinning specific
inspected revisions is deferred to this project's own release-readiness cycle.

Qt/GTest smoke tests instantiate the real window QML and exercise actual
shortcuts. A separate Controls Button verifies the embedded style selection
with environment overrides cleared. CLI tests check help/version/rejection.
Install checks start the staged binary using only installed provider imports.
Visual acceptance remains a separate check for actual compositor decorations
and appearance/scale, reusing holonight-viewer's dark/light appearance
fixtures unchanged since they are project-agnostic HoloNight theme files.

The desktop entry does not declare MimeType or a `%f` argument, unlike
holonight-viewer's current entry: opening a folder is out of scope for this
cycle, so scripts/register-desktop.py and scripts/check-desktop.py drop the
`-- %f` suffix and the GIO file-substitution exercise that holonight-viewer's
equivalents added for its later image-opening stage. Development launch still
registers the desktop entry and icon in the user's XDG data home before
opening the window, for the same host-portal-resolution reason as
holonight-viewer.

`task desktop-check` registers debug then release in one isolated XDG data
home under build/, validates generated entries and launch arguments, executes
their version commands, and checks packaged-entry separation — the same shape
as holonight-viewer's check, minus the MIME/`%f` assertions that do not apply
here yet.
