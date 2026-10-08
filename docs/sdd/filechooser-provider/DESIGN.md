# Design

Keep DirectoryModel and its Files-specific refresh/edit/restore coordination private. Extract its walker
into Core, together with DirectoryEntry, role IDs, standard place parsing, icon-name metadata and
DirectoryProxyModel. Files' wrapper headers preserve its include conventions. DirectoryProxyModel retains
existing editing/placeholder extension hooks so Files can preserve its behavior.

Add a small DirectoryReader for independent consumers: one worker per reader, generation-based stale
result rejection, shared cancellation and at most two queued batch deliveries. Loading replaces contents;
watchers, navigation, selection, fuzzy matching and persistence remain consumer-owned. Native byte input
is available through loadNative/nativeDirectoryPath, and each entry has a nativePath role. The walker
stats paths assembled directly from readdir bytes; display QString values remain separate. A backend can
therefore construct encoded URIs without losing non-UTF-8 Linux filenames. Worker callbacks from the
private enumeration function run on the worker; readers apply batches on their owning thread.

shutdown() cancels deliveries and quits asynchronously. Consumers should retain the reader through
shutdownFinished before destruction. Like Files' existing model, destruction joins the worker; cancellation
cannot interrupt an already blocked kernel filesystem call. No unsafe thread termination is introduced.

Holonight.FileBrowser DirectoryListing is a passive ListView with a default themed delegate. Model and
currentIndex stay externally owned; selectedPaths is a presentation input. Files supplies its existing
editing delegate, gutter layout and key handling. No Files controller is exported. Quick exports
initializeEngine(QQmlEngine&) for per-engine icon-provider registration. The optional iconProvider URL
prefix displays Core's candidate chains. Consumers can replace the delegate to present selection using
native identity when different byte filenames have the same decoded display text.

Build options: BUILD_FILES_APP (default ON), BUILD_FILE_BROWSER_QUICK (default ON). Core requires only Qt
Core; Quick additionally requires Qt Gui/Quick/QML/Controls and HolonightQt. Export separate Core and Quick
targets so Core-only packages remain usable without Quick. Quick-only headers are installed only with Quick.
Install QML under lib/qt6/qml/Holonight/FileBrowser, matching existing provider conventions.

Acceptance found two pre-existing lint issues at the assigned baseline: FileCommandRouter::handleKey
exceeded the complexity threshold, and Places/Devices panels used internal IDs that shadowed their list
aliases. Isolate the identical timer-expiry code in a private helper and rename those internal IDs while
preserving public aliases. Keep this maintenance prerequisite in a separate local commit. Verify existing
keyboard/sidebar behavior after these changes; do not weaken lint rules.

Verification: provider tests, existing Files regressions, independent Core-only installation, installed
external C++/QML/plugin consumers, formatting/import/metadata/lint checks and Files build/runtime staging.
