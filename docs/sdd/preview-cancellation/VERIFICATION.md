# Verification

2026-09-23: Automated acceptance complete. Changes remain local and uncommitted. Logs and raw benchmark evidence are retained in
`build/cancellation-verification`, `build/cancellation-baseline` and
`build/cancellation-candidate`. No native acceptance is claimed.

## Regression evidence

- With only the per-request stage seam and preview tests added, the existing
  implementation failed all eight `ThumbnailBoundaries/*` cases. Each cancelled
  request still committed its obsolete large thumbnail; cancellation at original
  decode completion also entered the later pre-commit stage. `red-build.log` and
  `red.log` preserve the failing run.
- Token propagation made those eight cases pass. `green.log` records 81 passing
  preview/thumbnail/orientation tests, including existing timeout, revision and
  memory-cache regressions.
- Final expanded cancellation coverage has 16 cases: eight real preview jobs
  (clear, rapid selection, resize debounce, shutdown at decode completion and
  pre-commit), six direct thumbnail boundaries, pre-cancelled descriptor operations
  and uncached original-decode cancellation. `final-focused.log` records all 16
  passing. After extracting the single-entry cache reader to satisfy complexity
  lint, `refactor-green.log` records all 27 thumbnail/cancellation tests passing.
- Tests generate real images, isolate XDG cache paths, compare existing PNG bytes,
  assert no fallback stages or obsolete image/error signals, and revisit cleared
  requests to verify cancelled images were not retained in memory. Worker gates
  have bounded waits and scope-guard releases before service destruction.
- Focused clang-tidy passed after the extraction (`focused-tidy-final.log`);
  the initial actionable complexity finding is retained in `focused-tidy.log`.

Commands (from Files):

```sh
cmake --build build/test --target files-smoke -j 4
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software build/test/tests/files-smoke \
  --gtest_filter='ThumbnailBoundaries/*'
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software build/test/tests/files-smoke \
  --gtest_filter='ThumbnailBoundaries/*:Stages/ThumbnailCancellation.*:ThumbnailService.*:PreviewService.*:ExifValues/*:*PreviewDecode*'
run-clang-tidy -p build/test -removed-arg=-mno-direct-extern-access \
  -config-file=.clang-tidy -j 2 \
  'apps/files/preview/(thumbnail_service|preview_service).cpp|tests/(thumbnail_service_test|preview_service_test).cpp'
```

The final focused rerun used
`ThumbnailBoundaries/*:Stages/ThumbnailCancellation.*:ThumbnailService.*`.

## Clean build

A fresh `build/cancellation-release` configured and built successfully. Complete
configure/build logs were reviewed; no actionable warnings were emitted.

```sh
cmake -S . -B build/cancellation-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix" \
  -DQML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml"
cmake --build build/cancellation-release -j 4
```

## Limits

Cancellation is cooperative at stage boundaries and within the existing provider
contract. A codec/filesystem call or commit already in progress may finish; Files
never deletes a valid committed entry to simulate atomic cancellation. Tests do
not establish interruptible codecs, native rendering, mixed-monitor behavior or
long-duration memory stability. Native sharp-preview T5 and deferred gates stay open.

## Performance comparison

Baseline and candidate both passed **15/15 trials**: all three scenarios, five
fresh processes each. The runner performed these sequentially with no competing
build or acceptance jobs. A candidate build was interrupted before measurement to
resolve the lint finding; only the final candidate run is included here.

```sh
cmake --build build/preview-performance --target files-smoke -j 4
python3 scripts/measure-preview.py build/preview-performance/tests/files-smoke \
  build/cancellation-baseline --scenario all
# After implementation and focused verification, rebuild the same Release target:
cmake --build build/preview-performance --target files-smoke -j 4
python3 scripts/measure-preview.py build/preview-performance/tests/files-smoke \
  build/cancellation-candidate --scenario all
```

Every baseline production-file hash was also checked against the requested
`8ba2a6bdec2c99ecb7c34ade107328204e2e180f` commit.

Both builds use GCC 16.2.1, Qt 6.11.2, Release with testing enabled. The runner
validated clean provider checkouts against the installed ledger and build caches:

- Config: `fe69a59e6b73167fd5349223a4d265d75386c139`.
- Qt: `863af4183bdf09ce05199b37e8f5dfb46a311ba1`.
- Images: `3633865d2f39e4f163f0159a0f252f88245379f0`.

Provider configurations/revisions, installed library hashes, compiler, Qt, Files
CMake configuration/cache hashes and generated fixture hashes compare identical.
The only instrumentation-file difference is the two new friend accessors in
`preview_service_test_access.h`; the performance runner and workloads are unchanged.
Production differences are limited to the two preview and two thumbnail source/header
files. Raw environment JSON, XML, trial logs, RSS samples and summaries remain in
`build/cancellation-baseline` and `build/cancellation-candidate`.

Values below are median (minimum–maximum) across five trials. Latency rows aggregate
each trial's mean over the corresponding phase; attempt counts are totals per trial.
Pressure GUI gap, shutdown and RSS rows are individual trial observations.

| Measurement | Baseline | Candidate |
| --- | --- | --- |
| cold pixels, ms | 154.911 (152.361–156.470) | 155.127 (151.580–156.438) |
| cold metadata, ms | 154.935 (152.429–156.510) | 155.175 (151.610–156.480) |
| cold attempts per trial | 6.000 (6.000–6.000) | 6.000 (6.000–6.000) |
| disk pixels, ms | 3.604 (3.532–3.866) | 3.562 (3.367–3.684) |
| disk metadata, ms | 3.631 (3.554–3.895) | 3.592 (3.402–3.711) |
| disk attempts per trial | 0.000 (0.000–0.000) | 0.000 (0.000–0.000) |
| memory pixels, ms | 0.283 (0.249–0.385) | 0.294 (0.187–0.339) |
| memory metadata, ms | 0.306 (0.272–0.402) | 0.319 (0.208–0.366) |
| memory attempts per trial | 0.000 (0.000–0.000) | 0.000 (0.000–0.000) |
| resize pixels, ms | 289.351 (288.447–289.917) | 288.323 (287.243–289.244) |
| resize attempts per trial | 30.000 (30.000–30.000) | 30.000 (30.000–30.000) |
| rapid pixels, ms | 264.664 (259.035–268.557) | 260.571 (255.544–265.705) |
| rapid attempts per trial | 6.000 (6.000–6.000) | 6.000 (6.000–6.000) |
| return pixels, ms | 0.043 (0.033–0.090) | 0.030 (0.027–0.040) |
| return attempts per trial | 0.000 (0.000–0.000) | 0.000 (0.000–0.000) |
| Pressure selection pixels, ms | 107.887 (107.297–108.469) | 107.892 (107.367–108.290) |
| Pressure attempts per trial | 33.000 (33.000–33.000) | 33.000 (33.000–33.000) |
| Maximum GUI timer gap, ms | 5.958 (5.818–6.039) | 5.886 (5.708–6.148) |
| Shutdown, ms | 109.015 (107.422–109.845) | 108.308 (105.553–111.314) |
| Peak RSS, MiB | 316.531 (316.168–316.625) | 316.344 (315.883–316.570) |
| RSS after cycle 1, MiB | 91.574 (91.496–91.613) | 91.848 (91.566–92.098) |
| RSS after cycle 3, MiB | 91.582 (91.504–91.621) | 91.859 (91.570–92.102) |
| RSS after shutdown, MiB | 91.879 (91.824–91.902) | 92.160 (91.848–92.367) |

Successful cache behavior and decode-attempt counts are preserved. Timing ranges
largely overlap; these measurements support no speedup claim or timing threshold.
The pressure workload's 33 attempts and shutdown time do not measure the newly
synchronized thumbnail boundaries; the deterministic regressions establish that fix.
RSS remains broadly comparable, with only 4–12 KiB growth between the first and
third candidate cycles. This is a bounded workload, not a long-duration leak test.

## Acceptance execution

```sh
CMAKE_BUILD_PARALLEL_LEVEL=4 JOBS=4 task check
CMAKE_BUILD_PARALLEL_LEVEL=4 JOBS=4 task isolated-runtime-check
```

The sandboxed `task check` first stopped at unrelated socket-binding permission
and accelerated-context restrictions. Its logs are retained in `task-check.log`.
The elevated rerun passed all 25 CTest entries in 58.07 seconds; `files-smoke`
passed 576 tests with six expected opt-in skips (native inspection, directory
performance, and four preview-performance entries). Preview performance was run
separately above. The complete elevated `task check` passed, including full C++
and QML lint, formatting, REUSE, staged installation, QML import policy and
application metadata checks (`task-check-approved.log`). Complete build/check logs
were reviewed; no actionable warnings remain.

The isolated installed-runtime check passed after elevated Docker socket access
(`isolated-runtime-approved.log`), using the existing network-isolated container
check. It verified version, file-manager association and installed desktop launch
observed for three seconds. The first sandbox-denied attempt remains recorded in
`isolated-runtime.log`. No source correction was needed for either environment
restriction.


Final review confirmed only Files source/tests/documentation changed. Files HEAD and
Images HEAD remain at the requested baselines; no commits, publication or umbrella
pin updates were performed. Local SDD/backlog links and `git diff --check` passed.
Documentation-only closure did not repeat the unchanged application acceptance.
The final documentation closure resolved all 32 local links in the SDD/backlog,
matched final production hashes to the measured candidate, and passed REUSE again
(`final-license.log`). This last REUSE invocation required elevated access because
its Python worker pool's local socket is blocked by the sandbox.
