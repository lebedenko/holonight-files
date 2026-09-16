# Info sidebar redesign verification

Date: 2026-09-16. Implementation and review remediation verified automatically;
native visual and real-camera acceptance pending. Generated logs and review
probes stay under `build/`. License metadata is covered by the repository's
GPL-3.0-or-later annotation in `REUSE.toml`.

## Requirement evidence

| Requirements | Evidence |
| --- | --- |
| REQ-F-003, REQ-F-023..025, REQ-NF-003/004 | `ExifReader` and `PreviewService` tests cover lens/aperture extraction, partial and absent tags, zero denominator, locale-dependent MIME descriptions and clearing on retarget. Synthetic fixtures do not replace real-camera acceptance. |
| REQ-F-005/030 | `Files.PreviewSidebarSizeMatchesListingAndDirectoriesShowDir` compares listing/sidebar strings and checks directory size text. |
| REQ-F-001/002/008/012/019/022, REQ-NF-001/002/005 | `Files.PreviewSidebarRowsHideWrapAndStayFreeOfBindingLoops` covers frame sizing, empty rows, long values, errors and binding-loop warnings. |
| REQ-NF-002/005 | `Files.PreviewSidebarScrollsToWrappedExifInShortWindows` uses a 1000×500 window, 220 px pane, portrait image and long lens value; wheel events reach the entire final row, and selecting text resets the scroll position. |
| REQ-F-026..029, REQ-C-006 | Quick Look and text backend source files are unchanged. Existing Quick Look, permissions and watcher tests remain in the suite. The watcher mutation check is pending separately below. |

## Commands and results

- Initial review: `task deps`, `task build`, `task format-check` passed. `task test`
  passed all eight CTest targets outside the sandbox after its socket-bind test
  was denied inside the sandbox. Logs: `build/review-sidebar-{deps,build,format}.log`
  and `build/review-sidebar-test-unrestricted.log`.
- Initial review's standalone overflow probe reproduced the final EXIF row at
  y=554 in a 417 px pane. The new regression test exercises this short-window case
  through wheel input. The standalone rounded-edge alpha probe passed; it is not
  native fractional-scale visual acceptance.
- Remediation: `task deps` and `task build` passed (`build/sidebar-fix-deps.log`,
  `build/sidebar-fix-build.log`). The initial test run exposed a layout-polish race
  in the prior wrapping test and missing timestamps in synthetic wheel events;
  the tests now await the final geometry and timestamp each wheel step. Its
  unrelated socket-bind denial required an unrestricted verification run.
- The focused new scroll test passed (`build/sidebar-fix-focused.log`). Final
  `task check` exited 0 (`build/sidebar-fix-check-final.log`): debug/release builds,
  all eight CTest targets, formatting, C++ lint, QML lint, REUSE license checks,
  staged installation/launch and uninstall checks passed. The full test run took
  38.47 seconds. Native opt-in tests remain outside this automated acceptance.
- `git diff --check` passed after the final documentation updates. No install
  rules or desktop-entry changes were made during remediation; no push or
  isolated container runtime check was performed.

## Pending acceptance

- T-011: the watcher test passes normally, but no recorded run demonstrates that
  disabling watch reattachment makes it fail. Keep this task partially complete.
- T-015: native Hyprland at 1.5× needs recorded confirmation of corners, column
  alignment, image reflow, all fallback tiers, narrow-pane scrolling/wrapping and
  Quick Look text. Automated offscreen tests do not establish these observations.
- T-016: record camera manufacturers/fixture provenance and outcomes for at least
  two manufacturers, including MakerNote-only lens metadata. Existing generated
  JPEG fixtures do not satisfy this check.

No native-display confirmation or real-camera evidence was available during this
remediation; their former checked boxes have been corrected to pending.

## Mockup table-alignment correction (2026-09-16)

The approved alignment-only plan corrects REQ-F-004's former right-alignment
criterion. Both tables now left-align labels and values while retaining the
shared translated-label measurement, compact spacing and top-aligned wrapping.
No public API, typography, color, thumbnail-size or responsive-width changes.

- `task deps` and `task build` passed; logs are
  `build/sidebar-alignment-{deps,build}.log`.
- `Files.PreviewSidebarTablesShareLeftAlignedColumns` checks full EXIF (nine
  visible rows) and partial EXIF without lens/aperture (seven rows) at the
  default 320 px and minimum 220 px widths. It asserts text alignment, shared
  scene-coordinate left edges across both tables, stable label widths, equal
  spacing, matching label/value top edges, hidden pairs and unclipped values.
- The focused geometry and existing wheel-scrolling regression both passed;
  see `build/sidebar-alignment-focused.log`. The first geometry run exposed
  the test helper's forward-only navigation; the test now returns to the first
  entry before checking the second width.
- Visual inspection compared `docs/mockups/moc1.png` with all four offscreen
  captures, `build/sidebar-alignment-{01-full,02-partial}.jpg-{320,220}.png`.
  Labels share a left edge, values start at one common position across both
  tables, and wrapped camera values stay top-aligned at 220 px. Typography,
  colors and thumbnail sizing retain the application's existing presentation.
  This verifies the requested table alignment, not native fractional-scale
  rendering; T-015 and the other earlier pending acceptance checks remain open.
- The first `task test` also hit the existing local socket-bind sandbox denial.
  Full verification is rerun outside the sandbox for that test.
- Final `task check` exited 0 (`build/sidebar-alignment-check.log`): debug and
  release builds, all eight CTest targets (39.50 seconds), formatting, C++/QML
  lint, license validation, staged installation/launch and uninstall passed.
- Final `git diff --check` passed. Spark delegation was attempted for the QML
  change but the configured spark model was unavailable; implementation and
  review were completed locally, preserving the existing worktree changes.
