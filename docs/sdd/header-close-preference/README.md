# Live header close preference

## Intent and requirements

- **REQ-F-001:** While the effective close-button preference is false, Files shall hide the application-header close button.
- **REQ-F-002:** When configuration changes, Files shall update the close-button preference without requiring restart.
- **REQ-F-003:** While the close button is hidden, Files shall make its reserved horizontal space available to the remaining header content.

## Decisions and interfaces

`HeaderSettings` declares `[header] show_close_button`, default true, in both the application
and finder registries. The existing TOML parser, diagnostics and setting provenance apply.
`XdgPaths::sessionOverrideCandidates` validates the captured desktop identity; the first
existing candidate wins, including when malformed or unreadable. No later profile is tried.
`AppSettings::load` accepts an explicit selected profile, rebuilding from defaults each time.
Invalid values inherit lower layers; invalid files contribute no values. No configuration is written.

Application-owned `FilesPreferences` is constructed before QML. Its initial snapshot supplies
startup general settings; its read-only notifying `showCloseButton` drives the header through
an optional typed Main property. Harnesses without preferences keep the visible default.
General settings still apply at startup and finder settings still reload on finder opening.

A 100 ms single-shot timer debounces file and directory watcher events. Each reload refreshes
watches on configuration, candidates and every existing ancestor, covering atomic replacement,
missing directory creation and directory recreation. Only effective value changes notify QML.

The close control retains NoFocus and shutdown wiring. Hidden Close is disabled and ignored by
accessibility. The breadcrumb keeps its listing-aligned left edge; its right bound gives the
selector the header right inset when Close is hidden and the existing gap when visible.
Header height and fullscreen policy remain unchanged. Viewer, native decorations, shared
controls, show_title and umbrella dependency pins are outside this change.

## Acceptance

Configuration tests cover defaults, base/profile layering, provenance and invalid input.
Watcher tests cover edits, atomic replacement, higher-priority profile creation, missing
directories, deletion/recreation, invalid reload fallback and notification suppression.
Offscreen QML tests cover startup preference, live toggles, focus, fullscreen/windowed layout
at 420 and 1000 pixels; existing visible Close activation tests retain shutdown coverage.
Run focused regressions, native build/test, format, container check and isolated integration.
Manual labwc acceptance: launch with `[header] show_close_button = false` in the supplied
configuration, toggle it while running, repeat in config-labwc.toml, and verify header space,
keyboard focus and ordinary window closing. Native interaction is performed by the user.
