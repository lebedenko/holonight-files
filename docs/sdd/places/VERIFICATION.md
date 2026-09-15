# Places verification

Date: 2026-09-15. Implementation complete; native visual acceptance pending.
All generated logs, fixtures and captures are under `build/`.

## Requirement evidence

| Requirements | Evidence |
| --- | --- |
| P-001..003 | `PlacesModel.OrderedTranslatedLocationsIconsAndStartupProjects`: deterministic provider, translated labels, ordered paths and icon chains; Projects absent, regular file, directory and unchanged snapshot. `EmptyLocationsAreOmittedBeforeCleaning`: empty and missing paths. |
| P-004 | `PlacesWindow.FallbackIconsAndShortWindow`: token inset, keyboard scroll to fully visible last row at 800x220; offscreen captures in both themes at 1x, 1.25x, 1.5x. |
| P-005..007 | `KeyboardMouseHistorySelectionAndFocus`: actual Tab traversal, Up/Down, Enter/Space, mouse activation, exact-path highlight independent of keyboard row, back history and listing focus. |
| P-006 | `GuardsBlockActivation`: VISUAL, SEARCH, INSERT, task confirmation and Quick Look suppress sidebar clicks. `MissingPlaceUsesDirectoryError`: existing directory error remains visible and place stays selected. |
| P-003 | `FallbackIconsAndShortWindow`: uncached missing icon chain loads packaged SVG to Image.Ready. Existing provider tests cover candidate-chain lookup. |
| P-007 | Shared HnListDelegate retains `Accessible.name: title`; source review confirms translated title binding and ignored decorative focus rectangle/icons. |

## Commands and outcomes

- `task deps`: passed (`build/places-deps.log`); provider build/install output stays
  under build/deps, no sibling sources edited. Files consumes installed packages.
- `task build`: passed (`build/places-build.log`).
- `task test`: initial sandbox run exposed the replaced four-place assertion,
  a cached-icon fixture assumption, and Unix socket bind denial. First two fixed;
  sandbox denial resolved by authorized check outside the sandbox.
- First `task check`: full suite passed all 8 CTest targets (35 seconds for
  files-smoke); lint then flagged style issues in the new files. Fixed designated
  initializers, annotations, braces and pointer comparisons. Targeted clang-tidy
  passed all three affected translation units (`build/places-tidy-final.log`).
  QML lint identified an untyped currentItem call; a typed PlaceRow cast fixes it.
  Final `task check` exited 0 (`build/places-check-final.log`): debug/release
  builds, all 8 CTest targets, formatting, all 52 clang-tidy translation units,
  QML lint without warnings, REUSE (171/171 files), staged installation/launch
  and uninstall checks passed.
- Focused `files-smoke --gtest_filter='Places*'`: all 6 tests passed, including the
  final capture synchronization/last-row assertion (`build/places-focused.log`).
- `task format`, `git diff --check`: passed.

## Visual inspection

Ran `Files.PopulatedWindowKeyboardAndInlineError` and
`PlacesWindow.FallbackIconsAndShortWindow` with `QT_QPA_PLATFORM=offscreen`,
`QSG_RHI_BACKEND=software`, `HOLONIGHT_APPEARANCE_FILE=tests/fixtures/{dark,light}.toml`,
`QT_SCALE_FACTOR={1,1.25,1.5}`, and
`FILES_CAPTURE_PREFIX=build/visual/places-<theme>-<scale>`.

Inspected full-window captures for both themes at all three scales: muted heading,
centered recognizable theme icons, readable labels, consistent insets and unchanged
sidebar/separator. Inspected the final light 1.5x and dark 1.25x short-window captures: Projects is
fully visible with a separate focus outline; earlier rows scroll behind the fixed
heading. Theme-provided symbolic icons retain their theme colors.

## Limitations / pending acceptance

- No mockup was attached to this fresh context, so direct mockup comparison remains
  pending. Styling follows the approved description and installed shared tokens.
- Native Wayland light/dark interaction, assistive-technology testing and native
  fractional-scale acceptance remain pending; offscreen inspection is not native
  acceptance. No sibling control changes are included.
- Spark delegation was attempted for the isolated model implementation, but the
  tool rejected its configured model (`Unknown model gpt-5.3-codex-spark`). The main
  agent implemented and reviewed the change locally.
