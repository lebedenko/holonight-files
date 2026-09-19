# Design

DirectoryController remains the QML-facing application coordinator with its existing properties and invokables.
NavigationSession owns listing models, watcher, current directory, cursor/history and delayed navigation guards.
EditingSession owns placeholders, commit handling, search restoration and deferred model updates.
FileCommandRouter owns count/chord state and translates keys plus modal/prompt context into typed commands.
The coordinator executes those commands. PreviewSelection owns selection-to-preview synchronization.
SessionLifecycle owns persistence and an explicit set of outstanding workers with idempotent completion.
Window event interception belongs to presentation. Private sessions cooperate through the coordinator;
their internal state is not exported to QML. Existing service workers and filesystem algorithms remain intact.

Private backend code is compiled once. QML-facing registrations and presentation code belong to each executable's
HolonightFiles module. `presentation/qml_types.h` uses foreign registrations for backend QObjects;
`qt_extract_metatypes(files-private)` supplies their full properties and methods to the QML registrar.
The metadata check requires exported types, so unresolved foreign stubs cannot pass.
One engine initialization function installs image://icon idempotently per engine.
QML lives under qml/{listing,places,inspection,status}, retaining resource aliases.
Runtime Controls qualification permits style overrides. A guarded Binding supplies the optional style error
property while the status bar continues to display validation messages under any style.

Application settings remain local TOML; holonight-config provides shared appearance only.
Installation uses the umbrella's existing collision/ownership mechanism without adopting legacy files.
No provider API change or third-party dependency is required.
