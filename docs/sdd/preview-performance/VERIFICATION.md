# Verification and findings

Date: 2026-09-23. Automated acceptance complete; all changes remain local and uncommitted.
Production baseline: `84d4023818978aa3d8dd72a1276d6fcee75cc3eb`.
The candidate changes one full-decode call in Files to use the original bounds.
There are no public API, QML, provider-source, cache-policy or packaging changes.

Provider revisions were checked against the local installed prefix. `task deps`
refreshed the Images ledger to its current tests-and-documentation successor and confirmed
no rebuild was necessary. All providers use Release and the same GCC/Qt installation.
Complete configure/build/check logs are retained under `build/preview-verification/`.

The [sharp-preview native task T5](../sharp-previews/TASKS.md) remains open. Historical native acceptance records
are untouched. The generated-image/offscreen matrix cannot establish native rendering,
physical scaling, compositor interaction or representative photographic performance.

## Investigation

The initial instrumented run exposed a full-preview adequacy failure. At 2300×2300,
Files pre-fitted the 6000×4000 source to 2300×1533; the provider fitted that integer
rectangle again to 2299×1533. The service repeatedly decoded an inadequate result.
The 4096×4096 pressure workload similarly returned 4095×2730. The independent
1100×1100 PNG/EXIF regression failed on baseline PNG and orientation 2, while rotated
orientations 6/7 already passed. All four passed after the original-bound correction.

The first draft also exposed a measurement artifact: `QTest::qWaitFor` alternates
`processEvents` with a ten-millisecond sleep. The final workload uses a normal Qt
event loop and signal timestamps; a one-millisecond timer only checks completion.
Earlier exploratory outputs in `build/preview-baseline` and `build/preview-baseline-v2`
are retained but excluded from final latency comparison. Lint-only generator changes
were completed before repeating both sides with identical final instrumentation.

No memory/cache-policy redesign is justified by this result. Further optimization
requires separate evidence after the repeated decoding is removed.

## Build and comparison provenance

- GCC: `16.2.1 20260810`; Qt: `6.11.2`.
- Build: fresh `build/preview-performance`, Release, `BUILD_TESTING=ON`, `-O3 -DNDEBUG`;
  after the scoped correction, affected targets were rebuilt.
- Config provider: `fe69a59e6b73167fd5349223a4d265d75386c139`.
- Qt provider: `863af4183bdf09ce05199b37e8f5dfb46a311ba1`.
- Images provider: `3633865d2f39e4f163f0159a0f252f88245379f0`.
- Baseline: `build/preview-baseline-final`; candidate: `build/preview-candidate-final`.
  Both commands used `python3 scripts/measure-preview.py
  build/preview-performance/tests/files-smoke <output> --scenario all`.
- The original production file was restored from `git show HEAD:apps/files/preview/preview_service.cpp`
  and rebuilt for baseline, then the candidate was restored and rebuilt. Final
  instrumentation, fixture hashes, installed-library hashes, compiler/Qt, provider
  revisions, CMake cache hash and configuration compare identical. Only
  `apps/files/preview/preview_service.cpp` differs in the production hash inventory.
- The final baseline executable is retained as
  `build/preview-performance/tests/files-smoke-baseline`; exact binary hashes and
  complete provenance are in each output's `environment.json`.

No competing build or acceptance job ran during the sequential measurement trials.

## Final measurements

All five cache trials passed on both sides. Baseline resize and pressure each failed
all five trials because pixels never became adequate; their invalid/missing latency
samples are deliberately excluded from summaries. Candidate: **15/15 trials passed**,
including every content/orientation/retention check, rapid replacement and shutdown.

| Measurement | Baseline | Candidate |
| --- | --- | --- |
| Cold adequate pixels, ms | 152.92 (151.83–155.68) | 152.88 (150.70–159.54) |
| Disk adequate pixels, ms | 3.73 (3.43–4.06) | 3.72 (3.48–3.75) |
| Memory adequate pixels, ms | 0.277 (0.230–0.299) | 0.289 (0.215–0.325) |
| Cold / disk / memory attempts per trial | 6 / 0 / 0 | 6 / 0 / 0 |
| First PNG rapid 2300 upgrade | Inadequate after ~10 s; 37–39 attempts | 268.29 ms (261.28–273.24); one attempt |
| First PNG 4096 selection | Inadequate after ~10 s; 36–37 attempts | 115.80 ms (113.02–128.74); one attempt |

Cache rows report the median (min–max) of each trial's mean across six fixtures.
Upgrade/selection rows report median (min–max) across five trials. Full raw timings,
including separate metadata completion, are retained in XML/JSON. The small memory-hit
variation overlaps across runs; the unchanged cache paths show no reproducible adverse
tradeoff. The correction is retained because it removes repeated decoding and makes
both previously failing scenarios complete with adequate pixels.

Candidate pressure selections have per-trial median latency 109.30–110.23 ms. Each
trial records 33 total decode **attempts**, including cancellation/shutdown work.
The 4096 images retain about 42.7 MiB each, so two cannot fit in the 64 MiB cache.
RSS after the first cycle is 93,388–93,920 KiB and increases by only 8–16 KiB over the
next two cycles; no sustained growth is demonstrated in this workload. Whole-process
peak RSS is 323,384–324,316 KiB, including temporary decoding allocations, Qt and the
allocator, rather than just cache entries. Maximum GUI timer gaps are 5.69–5.99 ms;
shutdown with pending work is 107.70–109.74 ms. These observations are not timing gates
or proof of long-duration memory behavior.

## Acceptance checks

- Clean Release configure/build with testing enabled: passed; complete logs reviewed,
  no compiler warnings. Affected targets rebuilt after the correction.
- Baseline rounded-bounds regression: failed PNG and EXIF 2 as expected; EXIF 6/7 passed.
- Candidate focused `--gtest_filter=*Preview*:*Thumbnail*:*Exif*`: **114 passed**;
  four performance tests skipped because the opt-in environment was absent.
- `python3 scripts/check-measure-preview.py`: **6 tests passed**, covering malformed,
  missing, duplicate, negative, unexpected, skipped and failed measurements; process
  failure/timeouts; cache/timestamp semantics and summary consistency.
- Final performance matrix: baseline correctly exited 1 with ten rejected trials;
  candidate exited 0 with all fifteen accepted. No benchmark scenario was skipped.
- Focused clang-tidy: passed after fixture-generator style corrections.
- `CMAKE_BUILD_PARALLEL_LEVEL=6 task check`: **passed** outside the sandbox,
  including all **23 CTest checks**, formatting, full C++/QML lint, REUSE licensing,
  staged installation, import policy and QML metadata. The initial sandboxed run
  failed an existing Unix socket bind and aborted offscreen OpenGL tests; the
  unrestricted rerun passed those checks without disabling or changing them.
- Isolated installed-runtime acceptance: **passed** using
  `bash scripts/prepare-runtime-check.sh`,
  `docker build -t holonight-files-runtime-check build/runtime-check.1XYDHA`, and
  `docker run --rm --network none holonight-files-runtime-check`.
  Docker required unrestricted socket access. The ordinary-user container checked
  installed permissions, runtime paths, desktop registration and a three-second
  offscreen desktop launch. Image ID:
  `sha256:da8192f9aae4c36266ddb316ff3b209c615d0dad837c670f9baa9e92a6b2949b`.
- Local documentation links (63), formatting, licensing and final diff reviewed.
  No actionable compiler/lint warnings remain; third-party header diagnostics are
  suppressed by the existing clang-tidy configuration.

## Changed files and evidence

- `apps/files/preview/preview_service.cpp`: original bounds for full decoding.
- `tests/preview_service_test.cpp`: deterministic rounded-bound regressions.
- `tests/preview_performance_test.cpp`: generation and measured workloads.
- `tests/preview_service_test_access.h`: observe pending resize debounce.
- `tests/smoke.cpp`, `tests/CMakeLists.txt`: opt-in cache isolation, exercise registration
  and deterministic runner checks in CTest.
- `scripts/measure-preview.py`, `scripts/check-measure-preview.py`: local runner,
  provenance/evidence capture and failure-path tests.
- `README.md`, `docs/BACKLOG.md`, this SDD: reproducible commands, scope and findings.

Local output directories are ignored build artifacts, intentionally uncommitted:
`build/preview-baseline-final`, `build/preview-candidate-final`, and
`build/preview-verification`. They retain fixtures and hashes, per-trial XML/logs,
raw metrics, sampled/peak RSS, summaries and complete acceptance logs. Failed baseline
samples remain failures; they are not presented as latency results. No publication,
CI queries, umbrella changes or native acceptance closure was performed.
