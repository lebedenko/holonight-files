# Verification: Consistent key hints

Date: 2026-09-20. Provider: `8fe24ff83f8108631c2b7b0351994f7e513acfcd`
(published and pinned before consumer implementation). Host Qt: 6.11.2.

## Local checks

- Fresh Release acceptance: `cmake --preset release -B /tmp/key-hints-files-clean` followed by `cmake --build /tmp/key-hints-files-clean -j2`: passed. Build logs contain no
  compiler warnings; Shell emits the expected Qt private-module ABI notices.

- Debug, Release and test builds from `task check`: passed, using the verified
  Release provider artifacts built by Viewer and installed privately here.
- Focused Quick Look, mode-status and editing regressions: **17/17 passed**.
- All **9 CTest entries passed** across acceptance and the affected retry.
  The sandbox blocked a Unix socket fixture in files-smoke; elevated
  `ctest --test-dir build/test --rerun-failed --output-on-failure` passed.
- `QMLFORMAT=/usr/lib/qt6/bin/qmlformat task format-check`, `task lint`,
  `task qml-import-check`, `task qmltypes-check`, `reuse lint`: passed.
- `task install-check`: passed staged packaging, installed startup and desktop
  launch. `task isolated-runtime-check` used the accepted Release providers and
  matching Qt 6.11.2 container; desktop launch passed without network or workspace
  mounts.
- Offscreen captures at scales 1, 1.25 and 1.5 cover the footer, insert mode and
  Quick Look. Reviewed [insert](insert-1.25.png) and [Quick Look](quicklook-1.25.png):
  shared badges fit, Close/Confirm/Cancel labels align and error presentation
  remains covered by the validation regression.

## Review and handoff

The shared provider's verification covers all symbol mappings, punctuation, legacy
text, wrapping, accessibility, disabled state and the 8/12/18 pt multi-font matrix.
Consumer changes preserve their authoritative shortcut bindings.

Native ecosystem interaction remains a user-performed integration check. This
repository has not been published or pinned by this consumer work.
