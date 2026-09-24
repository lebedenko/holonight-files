# Verification

## Local evidence — 2026-09-24

- `PYTHONDONTWRITEBYTECODE=1 python3 scripts/check-compare-performance.py`:
  8 tests passed. The added process-exit/RSS regression failed in all three
  cases before the fix and passed afterwards.
- `cmake --build build/maintenance-acceptance -j 2`: passed, no compiler warnings.
  `ctest --test-dir build/maintenance-acceptance --output-on-failure`: 20/21
  entries passed in the sandbox. The Unix-socket fixture in files-smoke was
  blocked; `ctest --test-dir build/maintenance-acceptance --rerun-failed
  --output-on-failure` passed outside the sandbox. This covers all 21 entries.
  Logs: `build/maintenance-build.log`, `build/maintenance-ctest.log`,
  `build/maintenance-ctest-unrestricted.log`.
- `reuse lint`: passed outside the sandbox (multiprocessing sockets are blocked
  inside it); `git diff --check` passed.

## Fresh paired measurements

```sh
python3 scripts/measure-preview.py build/maintenance-acceptance/tests/files-smoke build/maintenance-baseline
python3 scripts/measure-preview.py build/maintenance-acceptance/tests/files-smoke build/maintenance-candidate
python3 scripts/compare-performance.py build/maintenance-baseline build/maintenance-candidate build/maintenance-comparison
```

All 30 measured processes passed: five trials of cache, resize and pressure in
both datasets. Two additional fixture-generation processes passed. Private D-Bus
sockets required unsandboxed execution. Each trial used separate HOME/XDG paths,
offscreen/software rendering and scale 1. Raw XML, logs, sampled RSS, process
exit/peak-RSS records, fixtures and provenance remain below those build directories.
No benchmark ran concurrently with another benchmark or a build.

The comparison succeeded with no provenance overrides. Production source hashes,
fixture hashes, instrumentation, build settings and installed provider hashes
match; both datasets use binary SHA256
`eb5e639168bee3e67b83221d4cc55fc7f3ecac7d155ac227056bc27d9cf862e7`.
Baseline report SHA256: `42ecc6eecef50f789bae75dc23a8c33e1da90dd6da4d4e865e845b9f2d2fc52a`.
Candidate report SHA256: `560474a00565f8f1a802701974248a0c2b0ddad8ab05152b20d77993efb6cd80`.

Provider artifacts were matched byte-for-byte to their local build outputs;
recorded revisions are Config `fe69a59`, Qt `863af41`, Images `3633865`.
Provider caches use Release, `/usr/bin/c++`, and the same Qt installation as the
consumer. Images' pending fuzz tooling leaves its production sources unchanged.
These are identical-production tooling comparisons, not performance improvements
or native-rendering acceptance. Timing variation has no pass/fail threshold.

## Full acceptance

`task check` passed on 2026-09-24, including Debug/Release builds, all 27 Debug
CTest entries, formatting, full clang-tidy, QML lint, REUSE, staged installation,
QML imports and generated metadata. Complete log:
`build/maintenance-task-check.log` (1,202 lines); no actionable warnings or errors.
No install payload/rule or desktop-entry changes were made, so the additional
Docker isolated-runtime check required for those changes does not apply.
