# Native preview acceptance requirements

Approved by the supplied implementation plan, 2026-09-23. Baseline:
`06c42f03534cdf57b2fb09f73bbfa69f029d0f9c`. Files-local, uncommitted work only.

- REQ-N-001: The testing-only lab shall reuse the application entry point, controller,
  QML and renderer, attaching passive diagnostics through a compile-time hook.
  Normal builds shall have no diagnostic interface or dependency.
- REQ-N-002: Evidence shall record monotonic publication times, selection identity,
  consumer logical/physical bounds, DPR, oriented source/decoded dimensions,
  image identity, metadata completion, original decode attempts, GUI timer gaps,
  process RSS/peak RSS and frame activity. These are not physical display latency.
- REQ-N-003: Native acceptance shall use actual Hyprland scales 1/1.25/1.5/2,
  hardware rendering and native Wayland, with no artificial Qt scale override.
  Every scale, focus, selection, resize and fullscreen action shall be manual.
- REQ-N-004: Five cold/disk/memory trial pairs per scale and photograph shall
  preserve pane geometry, isolate XDG config/data/state/cache, exclude startup
  selection from navigation timing, and report all failures and threshold misses.
  Cached publication <100 ms, uncached <500 ms, startup <500 ms; Quick Look
  usable retained pixels <200 ms, measured separately from adequate upgrade.
- REQ-N-005: Two pinned photographic originals and six existing generated controls
  shall support native identity/orientation/aspect/transparency/detail checks,
  narrow/wide/rapid resizing, >1024px requests, fullscreen and restoration.
  Matched physical-size ImageMagick references support visual, not pixel-equality,
  comparisons. Captures occur separately from timed trials using explicit geometry.
- REQ-N-006: Missing or malformed evidence, failed processes, identity/bounds
  mismatches and incomplete trials shall fail validation. Offscreen observer
  checks shall never count as native acceptance. Unresolved rows keep T5 open.
