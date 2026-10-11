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
