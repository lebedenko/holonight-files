# File-type icons verification

Implementation run, 2026-09-12 (T-001 through T-023).

Review remediation, 2026-09-13 (T-024): the bundled-SVG failure path now has a
visible placeholder. `task check` passed outside the filesystem sandbox (8/8
CTest entries; build, format, lint, license, install and uninstall checks).

## Automated checks

- `ctest --preset test`: all eight CTest entries passed, including the new
  `IconNameResolver.*` (11), `IconImageProvider.*` (5),
  `DirectoryModel.IconNameRoleCarriesTheWorkerDerivedCandidateChain`,
  `DirectoryModel.RefreshEmitsDataChangedWhenAnEntrysIconChanges`,
  `PreviewService.IconNameIsTheListingChainVerbatimAndClearsWithTheTarget` and
  `Files.IconColumnUsesThemeIconsAndFallsBackToBundledGlyphs`.
- `task format-check`, `cmake --build build/test --target qml-lint`,
  `cmake --build build/test --target tidy`, `reuse lint`, `task install-check`,
  and the `debug` preset build: passed.
- `scripts/check-visual.sh`: all `Files.*Window*` tests passed in dark/light at
  scales 1, 1.25 and 1.5; captures in `build/visual` show the icon column, the
  header Name label aligned with row names, and the 128 px preview icon above
  the text preview. Offscreen has no icon theme for most names, so these
  captures mostly show the tinted bundled glyphs (palette-coloured in both
  appearances). Sharpness is not claimed from them (REQ-NF-002/REQ-F-019).

## Deliberate test changes (REQ-NF-002)

- `Files.WindowColumnAlignmentAndNarrowNames`: the breadcrumb alignment
  assertion now compares against `iconColumnField` instead of `nameColumnField`,
  because the row's content now begins with the icon cell. The breadcrumb itself
  is unchanged. The test also gains 420/700/1000/1600 px checks for the 20 px
  icon cell and header/row Name alignment (1 px tolerance).
- `ModalEditingWindowKeyboardAndHighlighting` and
  `PopulatedWindowKeyboardAndInlineError` pass unmodified (T-022).

## Decisions and divergences found during implementation

- **Plugin registration (DESIGN.md §5.4) kept, with one addition.** The spike
  showed a hand-written `NO_GENERATE_PLUGIN_SOURCE` plugin builds and loads, but
  Qt silently skipped `initializeEngine()` because qmldir marked the plugin
  `optional` and the module's types were already registered. `NO_PLUGIN_OPTIONAL`
  fixes it; the new window test asserts `engine.imageProvider("icon")` is set on
  every engine without the test registering it.
- **`application/octet-stream` is skipped in the ancestor walk.** Every MIME type
  implicitly descends from it and its generic icon is `application-x-generic`,
  so walking it (breadth-first, before `text/plain`) ended chains early. An
  unregistered extension therefore yields only `application-x-generic`.
- **`.py` chain differs from SPEC.md's illustrative example.** shared-mime-info
  2.5.1 declares `application/x-executable` and `text/x-cython` as parents of
  `text/x-python`, so the pinned chain is `text-x-python, text-x-generic,
  application-x-executable, text-x-cython, text-plain, application-x-generic`.
- **Per-row Qt warnings (REQ-F-022).** With no icon theme, Qt Quick does not cache
  failed provider images and logged "Failed to get image from provider" once per
  visible row (32 lines for one screen). `IconFallbacks.js` records failed chains
  per engine so later rows and the preview pane skip the request and show the
  glyph directly. The provider's redundant warning has been removed. Qt Quick
  can still emit more than one image-load warning for a chain while listing and
  preview requests settle, so the strict one-message criterion remains open.
  The count does not grow with rows. A theme installed while the app runs is
  only picked up after a restart.
- **Missing bundled SVG (REQ-F-022).** Both icon slots now show a visible
  question-mark marker when the packaged fallback icon itself fails. The focused
  window test forces invalid resource URLs for both slots and observes the marker.

## REQ-F-021 performance

See `baseline_pre_icons.txt` and `baseline_post_icons.txt`: all native benchmark
medians and the full-suite wall-clock stay within 10% of the pre-icon baseline;
`files-memory-benchmark` stays within its threshold. PASS.

## Remaining acceptance

- **REQ-F-022 warning count:** Qt Quick may still log more than one image-load
  warning per distinct chain during concurrent listing/preview requests. This
  does not blank icons or affect navigation. The strict acceptance check is open.

- **T-020 / REQ-F-019**: fractional-scale sharpness verified on the real Hyprland
  1.5x display (HDMI-A-1, 3840x2160) by the user on 2026-09-13, native window,
  listing and preview-pane icons.
