# Shared search adoption — verification

Date: 2026-09-30. Provider baseline: published and pinned `holonight-search` `d12e441` (Qt 6.11.2, Release).

## Focused checks

- `task deps`: installed the exact provider revision in Files' dependency prefix.
- `cmake --build build/test --target files-smoke`: passed.
- `files-smoke --gtest_filter='PathFinderModel.*'` with offscreen/software Qt: 11 tests passed, covering both term orders, cross-field paths, smart case, filename highlights, hidden opt-in, partial results during a paused scan, directory/file filtering, stale query/root cancellation, cache refresh, and navigation.
- `bash tests/shared_provider_revisions_test.sh .`: passed after adding the provider to revision tracking.
- `files-smoke --gtest_filter='FuzzyMatcher.*:VimModeController.*'`: 27 listing-search and modal regression tests passed.
- Debug and Release application builds, `task format-check`, `task qml-import-check`, `task qml-lint`, `task qmltypes-check`, full `task tidy`, `task license-check`, and `task install-check` passed. Focused tidy passed after the final finder correction.
- The final clean `task isolated-runtime-check` passed in Docker; evidence is under `build/container-runtime.xj10by3p`. The complete log has no unused provider flag warnings. holonight-qt reports its established Qt private-header ABI warnings.
- The aggregate `task check` and `task test` were not run locally because their full test suite includes pointer/focus-dependent UI automation prohibited by the umbrella `AGENTS.md`. Focused non-interactive tests and each other applicable check ran separately.

## Two-million-path model benchmark

`build/finder-release/tests/files-finder-benchmark 2000000` used a deterministic synthetic path stream with the same generator as the provider benchmark. It exercised the Files scan worker, batch indexing, queued partial results, query worker, and model publication in Release. On this machine:

| Measure | Result |
|---|---:|
| Indexed paths | 2,000,000 |
| Cold scan and indexing | 2,494 ms |
| First nonempty result while scanning | 30 ms |
| Peak resident memory | 1,772,456 KiB |

Twenty warm samples timed from `setQuery` (final keystroke equivalent) to the model reset with stable results:

| Query | p50 | p95 |
|---|---:|---:|
| `lambda` | 4 ms | 5 ms |
| `lambda audit` | 5 ms | 5 ms |
| `audit lambda` | 5 ms | 5 ms |
| `document 42` | 62 ms | 76 ms |

This satisfies the model-path p95 target. It does not measure QML paint timing or native input delivery. The memory footprint is substantial relative to fzf's one-shot filter; no maximum memory target was defined. Real filesystem traversal speed and the user-performed native finder check remain pending.

## Remaining acceptance

- [x] Complete Debug/Release builds, formatting, lint, license, install, and isolated runtime checks; the only clean-build warnings are holonight-qt's declared Qt private-header warnings.
- [ ] User performs native finder check for popup controls, hidden toggle, both term orders, responsiveness, and navigation.
- [ ] Publish Files only after local acceptance; then pin the published revision and run umbrella integration review.
