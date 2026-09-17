# Places sources verification

## Review remediation — 2026-09-17

The user authorized fixing the implementation review findings. This record does
not reconstruct earlier approval or native-display verification history.

- REQ-C-006 / REQ-NF-003: the generation guard now rejects superseded startup
  results as well as superseded rechecks. The regression test delivers an older,
  contradictory startup observation after a completed recheck and verifies no
  status change or signal, for both Available and Unavailable outcomes.
- REQ-F-022 / REQ-F-037: the controller checks NORMAL mode, task prompts and Quick
  Look at dispatch and completion. A result still updates the model but cannot
  navigate, cancel an editor, or replace the status message while a guard is active.
  The regression test covers VISUAL, SEARCH, INSERT, Quick Look and a trash prompt,
  for both available and unavailable bookmark results, retaining unfinished text.
- Task commands now select Google Test suites inside `files-smoke`. For example,
  `ctest --test-dir build/test -N -R XdgPaths` selects zero tests and is not evidence
  that the XdgPaths suite passed.

## Reproduction and evidence

Dependencies: existing installed packages in `build/deps/prefix`. The remediation
does not rebuild or modify sibling sources. Generated logs remain under `build/`.

```sh
cmake --build --preset test
QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software \
  QML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml" \
  LD_LIBRARY_PATH="$PWD/build/deps/prefix/lib" \
  build/test/tests/files-smoke \
  --gtest_filter='PlacesModel.*:PlacesWindow.*:UserDirsParser.*:BookmarkStore.*:PlaceAvailabilityChecker.*:XdgPaths.*:TomlDocument.*:DirectoryController.*Bookmark*'
task check
```

- Test build: passed (`build/places-sources-fix-build.log`).
- Focused suites: 63/63 passed (`build/places-sources-fix-focused.log`).
- Initial sandboxed `task check`: stopped at the existing Unix-socket fixture's
  `bind()` with permission denied (`build/places-sources-fix-check.log`).
- Unrestricted `task check`: all eight CTest tests passed; format checks passed.
  The main smoke executable passed 477 tests and skipped two opt-in checks:
  `Files.NativeInspectionAcceptance` and
  `DirectoryPerformance.RenderedRowsAndInteractionWhileLoading`.
  The complete command exited 0, including debug/release builds, C++/QML lint,
  license checks, staged installation and uninstall verification
  (`build/places-sources-fix-check-unrestricted.log`).

The earlier review run passed 61 focused tests but did not cover the two delivery
races.

## Pending acceptance

- Native Hyprland at 1.5 scale: warning badge, muted rows, spacing, expected XDG
  rows, and unavailable activation without navigation (T-014). No confirmation
  is available in this review session.
- Checked implementation tasks reflect implemented work and test coverage, not
  independent evidence for every assertion listed in their original acceptance
  text. In particular, automated offscreen tests do not certify native rendering.
