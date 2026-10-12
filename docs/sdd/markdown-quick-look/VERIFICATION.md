# Verification

Date: 2026-10-11. Implementation authorized by the user-supplied plan. Shared provider source: explicitly supplied `QMARKDOWN_SOURCE`, revision `595ca4fea32155766c2eafb3b670995d2ea75a2f` (1.0.0), built with the same local Qt/toolchain as Files. No publication or umbrella changes.

## Focused checks

- `QMARKDOWN_SOURCE=<local checkout> task deps`: passed; installed the shared library, QML plugin, metadata and MIT/cmark notices into Files' local provider prefix.
- `task build PRESET=test`: passed after fixing the descriptor-based file's missing filename; canonical image roots are resolved from the selected path on the worker.
- Offscreen `files-smoke` with `TextPreviewService.*`, `PreviewService.*`, `QuickLookPresentation*.*`, `Invalidation/*`, `PreviewIntegration.*`, and `Files.*QuickLook*`: 115 tests passed before the final scroll refinement. Includes unchanged plain-text/image previews, BOM/trailing whitespace/newlines, the 100 KiB UTF-8 boundary, binary rejection, uppercase/empty Markdown, lifecycle and stale results.
- Final targeted offscreen Markdown, controller eligibility, pending classification and blocked-close/reopen regressions: 8 tests passed. Refresh now retains the previous rendered document while reading; scroll targets remain stable across intermediate layout changes and are captured again from the actual clamped position on the next refresh.
- `bash tests/shared_provider_revisions_test.sh "$PWD"`: passed, including qmarkdown configuration options and source-revision invalidation.
- `python3 scripts/ci/test_launcher.py`: 8 tests passed. `python3 scripts/ci/test_tooling_tidy.py`: 3 tests passed.
- QML import policy and focused QML lint: passed. `git diff --check`: passed.

## Rendering and resources

Rendered tests verify heading/emphasis/strong/inline-code projection, wrapping after resize, theme rebinding, j/k and arrow scrolling, wheel scrolling, pinned selection, inert link reporting, refresh preservation/clamping and destruction/recreation on close/reopen. Real PNG decoding succeeds within the authorized parent; traversal, symlink escape, HTTPS and qrc destinations retain descriptions.

Offscreen captures were inspected in dark mode at 1.25 scale and light mode at 1.5 scale (`build/markdown-visual/`). Both rendered scrolling regressions passed. These are isolated Qt test windows, with no native pointer/focus automation.

The user reported **Passed** for manual native dark/light themes, fractional scaling, keyboard/wheel scrolling, refresh and close/reopen during this implementation session.

## Complete gates

- `QMARKDOWN_SOURCE=<local checkout> task check`: **passed completely**. Debug/Release builds, all 31 CTest checks, formatting, full C++ lint, QML lint, license compliance, staged installation, import policy and generated QML metadata passed. Final log: `build/markdown-check-complete.log`. The first run exposed obsolete Markdown-unsupported expectations, a refresh layout timing issue and a sandbox-denied Unix socket `bind()` in an existing filesystem test. Expectations and refresh behavior were corrected; the socket check passed outside the sandbox with approval. C++ lint required explicit parentheses around multiplication in the scroll sum; focused and complete lint passed after that behavior-preserving correction.
- `QMARKDOWN_SOURCE=<local checkout> task isolated-runtime-check`: passed for the final implementation snapshot. Evidence: `build/container-runtime.di117wdj/verification.log`. The subsequent C++ parentheses correction changes no behavior.
- `python3 scripts/check-runtime-fixtures.py`: all 13 disposable-container cases passed, including missing qmarkdown library/plugin/metadata/license. The first missing-plugin fixture caught an `errexit` suppression in an AND-list; independent readability/size checks now fail immediately. Evidence: `build/runtime-fixtures.oc1tlgdz`.

All required acceptance gates passed. Native acceptance was reported by the user. Changes remain local to Files; no publication or umbrella pin update was performed.

## Changed files

- Preview source/loading/classification: `apps/files/preview/{text_preview_service,preview_service}.{h,cpp}`.
- Rendering and bounded scrolling: `apps/files/qml/inspection/QuickLookOverlay.qml`, `apps/files/presentation/quick_look_presentation_model.{h,cpp}`.
- Shared provider integration: root/app `CMakeLists.txt`, `tooling/{module.json,dependencies.cmake,workflow.py}` and `scripts/ci/lane.sh`.
- Runtime snapshots/payloads: `scripts/{check-container-runtime.py,prepare-runtime-check.sh,isolated-runtime.sh,check-runtime-fixtures.py}`.
- Regressions: `tests/{text_preview_service_test.cpp,preview_service_test.cpp,quick_look_presentation_model_test.cpp,preview_integration_test.cpp,directory_controller_test.cpp,smoke.cpp,shared_provider_revisions_test.sh}`.
- Documentation: `README.md`, `docs/BACKLOG.md` and this SDD cycle.

## Source override persistence follow-up

A native `task run` invocation exposed that dependency preflight discarded the source overrides used by the preceding successful setup. The workflow now reuses source paths from `build/deps/provider-state.json` when no explicit environment override is supplied. Explicit overrides still take precedence and are validated before any provider builds. README documents this behavior.

- The shared-provider fixture passed with all source overrides unset, verified invalid explicit overrides fail before building, and rebuilt a changed remembered Markdown provider. Log: `build/markdown-source-reuse-test.log`.
- `env -u QMARKDOWN_SOURCE task build` passed; this is the build step invoked by `task run`. Log: `build/markdown-source-reuse-build.log`.
- `env -u QMARKDOWN_SOURCE task check` passed completely: Debug/Release builds, all 31 tests, formatting, C++/QML lint, licenses, staged installation, QML import policy and metadata. Log: `build/markdown-source-reuse-check.log`.

## QMarkdown 1.1 Polished adoption (2026-10-12)

Authorized by the supplied implementation plan. Development/acceptance provider is the clean 1.1.0 release tree archived from `b71740c7e0f26b0709f5e929a79e8d01dba56071`, installed through `QMARKDOWN_SOURCE=/tmp/files-qmarkdown-1.1 task deps`. Release Git metadata was subsequently retained with that exact clean tree so dependency revision checks can reuse it. The prior CMake provider build directory was retained as `build/deps/qmarkdown-before-polished` because its cache pointed at a different source directory. The sibling provider working tree was left untouched.

- `task build PRESET=test`: passed against the clean release.
- Offscreen/software `files-smoke` filtered to `Files.Markdown*`, `TextPreviewService.*`, `PreviewService.*Markdown*`, `QuickLookPresentationModel.Markdown*` and the Markdown eligibility controller test: all 31 tests passed (`build/polished-focused.log`). Covers wrapping, keyboard/wheel scrolling, refresh preservation/clamping, lifecycle, source decoding and resource restrictions.
- Extended live theme regression passed dark → light → dark → HoloNight Latte and typography changes with the same preview instance; verifies preset selection, palette bindings, H1/H2 separator visibility, code backgrounds, borders, radii and 12-pixel spacing/padding.
- Dark/light offscreen captures at scales 1 and 1.25 passed and were visually inspected, including narrow layouts (`build/polished-visual/`). H1/H2 separators, fenced/indented code surfaces and inline backgrounds spanning wrapped lines render correctly. The initial separator assertion counted disabled zero-height items; it was corrected to count active separators. No native pointer/focus automation was used.
- Provider revision fixture, QML import policy, source formatting and `git diff --check`: passed.
- `task check`: the sandbox run reached the tests and failed only the existing Unix-socket bind test with permission denied (`build/polished-check.log`). The approved rerun outside the sandbox passed all 31 CTest checks; C++ lint then caught the required trailing comma in the new multiline theme initializer. The comma was corrected; lint resumes from `tests/smoke.cpp` through the remaining owned sources, followed by format-check, qml-lint, license-check, install-check, qml-import-check and qmltypes-check (`build/polished-check-remaining.log`). No behavior changed. All remaining C++ lint, formatting and QML lint passed. REUSE then hit its multiprocessing socket sandbox restriction; the approved final continuation passed license-check, install-check, qml-import-check and qmltypes-check (`build/polished-check-final.log`). Together these runs complete every sequential `task check` gate.
- `QMARKDOWN_SOURCE=/tmp/files-qmarkdown-runtime-1.1 task isolated-runtime-check`: passed against a clean release checkout, including complete staged provider payload and isolated desktop launch without networking. Docker socket access required approval; fresh image package downloads were slow but completed. Evidence: `build/polished-runtime.log`, `build/container-runtime.f46susj7/verification.log`.
- While the fresh image downloads were stalled, an additional isolated run used existing immutable Qt 6.12 CI image `sha256:aa92ed1425f097e6bb625ee8d9a4ce2034831e2d9ed57dd815991c355d3b0f9d`, rebuilding current sources and clean QMarkdown 1.1.0 without networking. It also passed (`build/polished-runtime-offline.log`, `build/container-runtime.df74dpop/verification.log`). The temporary fallback runner was outside repository sources. Both checks use disposable container workspaces.
- Native theme-switching and keyboard/wheel acceptance: the user reported **Passed** during this implementation session.

Changed files: root CMake requirement; CI provider pin; QuickLookOverlay styling; smoke regressions; README dependency documentation and backlog status; Markdown SDD specification, design, tasks and this verification record. No publication or umbrella pin changes.

All Polished acceptance gates passed. The only source correction after the full CTest run was the required trailing comma in test data; focused/full behavior results remain applicable. No publication or umbrella pin update was performed.

## Inline-code contrast follow-up (2026-10-12)

User requested trying `surfaceRaised` after observing that the inline-code background was too subtle. Updated the overlay binding, live theme regression expectation and design; code blocks retain `surfaceElevated`. Focused `files-smoke` build and all four offscreen Markdown regressions passed (`build/inline-code-raised-{build,tests}.log`). Source formatting and `git diff --check` passed. Broad gates from the preceding implementation were not repeated for this color-only follow-up.
