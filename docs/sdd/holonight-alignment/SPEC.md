# Specification

- F-01: Files shall preserve layout, keyboard counts/chords, modal transitions, prompt precedence,
  editor focus, cancellation and Quick Look/fullscreen Escape ordering.
- F-02: Navigation shall preserve history cursor identity, watcher refresh, search cancellation,
  and rejection of stale bookmark/restore results.
- F-03: Editing and operations shall preserve rename/create/touch, conflict handling, permission
  failures, cancellation and cross-filesystem behavior. Worker algorithms shall remain unchanged.
- F-04: Preview shall preserve stale-result guards and resources. Every application/test engine
  shall initialize its own icon provider. Shutdown shall finish once after every registered worker finishes.
- F-05: Default and Fusion styles shall start and permit editing; validation shall remain visible.
- C-01: Preserve hn-files, org.holonight.Files, HolonightFiles, CLI, resource aliases, TOML formats and paths.
- C-02: Provider discovery shall honor caller CMAKE_PREFIX_PATH and QML_IMPORT_PATH; development defaults
  belong to presets/tasks. Runtime controls shall be namespaced; no direct style imports.
- C-03: Backend libraries shall be private; application/UI tests shall own their executable QML modules.
- C-04: System installation/removal shall be coordinated by the umbrella with ownership manifests;
  unmanaged collisions shall be rejected and modified files preserved. Standalone staging shall work.
- V-01: Record focused regressions, clean acceptance, staged and isolated runtime checks. Native dark/light,
  fractional scale, minimum window and interaction acceptance requires user participation.
- V-02: Publication and pin updates are separate authorization steps. Never mark integration complete
  before exact published revisions and native ecosystem checks are verified.
