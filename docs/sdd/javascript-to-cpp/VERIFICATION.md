# Verification

2026-09-16. Implementation is complete; native visual acceptance remains pending.
The user approved the supplied implementation plan. Spark delegation was attempted
but its configured model was unavailable; the main agent completed the formatter.

| Requirement | Evidence |
| --- | --- |
| REQ-001 | SizeFormat tests cover signed extremes, zero, unit boundaries, rounding, TB cap, decimal-point formatting under German locale and original translation context/source strings. Rendered metadata test compares listing, sidebar and Quick Look. |
| REQ-002 | IconFallbacks tests cover exact chains, duplicates, shared QML/direct state, engine isolation and nonreactive bindings. Rendered fallback test recreates delegates and confirms an empty theme source avoids repeated provider requests. |
| REQ-003–004 | InspectionKeys tests cover normalization, empty/unhandled text, popup allowlist, modifiers/history precedence, repeats, null controller and release/override acceptance. Rendered regressions cover Space without activation, popup j/k, focus restoration, Escape/fullscreen, VISUAL cancellation, and Ctrl+O/I history. |
| REQ-005 | No project-owned standalone `.js` files or live JS imports/resource entries remain. Existing embedded QML and image-provider cache are retained. |
| REQ-006 | Commands and limitations below. |

- `task deps`: passed; provider sources were only read, built and installed beneath build/.
- `task build`: passed (debug).
- `task test`: initial test compilation caught a scoped-enum test typo, fixed locally.
  Subsequent sandbox run passed migration tests but failed the pre-existing socket
  binding test with permission denied. The expanded-permission `task check` test
  stage passed all 8 CTest targets (361 smoke cases, two opt-in skips).
- `task check`: passed in full with expanded permissions: debug/release builds,
  all 8 CTest targets, formatting, clang-tidy, QML lint, REUSE, staged install and
  uninstall. First lint run identified new-code static-method suggestions, array
  indexing and test style findings; fixed with checked indexing, test cleanup and
  narrowly documented suppressions for instance QML invokables. The complete gate
  was rerun successfully after those fixes.
- `task isolated-runtime-check`: passed with expanded permissions after sandbox
  Docker socket denial. Network-isolated container desktop launch observed for three
  seconds; installed CLI, dependencies and folder-handler registration passed.
- `task visual-check`: passed all 36 rendered cases across dark/light and 1/1.25/1.5
  scales. Inspected dark/light 1.5x populated captures: matching listing/sidebar
  byte sizes, visible file/folder glyphs, readable selected rows and metadata.
- `git diff --check`: passed. `rg --files -g '*.js' -g '!build/**'` found no files;
  live application/build files contain no `.js` helper references. Historical SDD
  records are retained.

Logs: `build/javascript-to-cpp-{deps,build,test,check,runtime,visual,format}.log`.
Captures: `build/visual/`. Generated artifacts remain under build/.

Native/compositor visual inspection and real fractional-display acceptance were
not performed. Offscreen captures do not close that gate. Existing opt-in native
inspection and large rendered-directory performance tests remain skipped in the
standard suite. No sibling source modifications, commits or pushes were made.
