# Verification — 2026-10-02

Baseline: `holonight-files` b6e4a9680faba9fde74eed0d2cbebe193d5d2aa5. The user authorized a local implementation commit after verification;
no publication or umbrella pin changes were requested. Toolchain: GCC 16.2.1, Qt 6.11.2, Debug test builds.
Existing provider artifacts were checked through `task deps` and `build/deps/provider-revisions.tsv`:
Config 03fa635cedc506e101a148fc54f7eb46d17c6de5, Qt f10e8c8ba57282e953f8ddc4a6b0c1109e05a3bf,
Images ac11f23e9ff2d70b1c142b643bc2abb6dd69f6c1, Thumbnails d27addc044f277686850588147ec825c40c0f252,
System Services 3e2928eb55bbc3de2b1e877e29aa57d47077c05d, Search d12e441c06d1df87450514136c5a2d458d985ed3.
No provider implementation was changed.

## Local evidence

- `task build PRESET=test`: passed. Focused finder/store/exclusion/controller-operation tests: 33/33 passed
  before final lint corrections; further codec-boundary and unavailable-root tests are included in clean acceptance.
- `cmake --preset test -B build/search-lifecycle-acceptance` with the verified local provider/QML prefix,
  followed by `cmake --build build/search-lifecycle-acceptance`: clean acceptance build passed. Complete
  build logs were inspected; no compiler warnings/errors. Corrections were rebuilt in that acceptance directory.
- `ctest --test-dir build/search-lifecycle-acceptance --output-on-failure`: final 21/21 passed (71.91 seconds).
  The main smoke suite passed 747 tests with 7 existing environment/opt-in skips.
  This fresh build uses the preset defaults, with optional accelerated separator tests disabled.
- `task check`: sandbox run stopped at restricted device-node/GPU tests. Approved unrestricted rerun built
  Debug/Release, passed all 27 tests (including previously enabled accelerated tests) and formatting, then
  stopped at clang-tidy. New-code findings were corrected; all 11 changed C++ translation units now pass
  focused clang-tidy with the repository configuration (store tests rechecked after their final correction).
  Full clang-tidy remains blocked by existing errors in 27 unchanged files: predominantly trailing initializer
  commas, plus existing move/complexity diagnostics. These unrelated files were not modified.
- `task qml-lint`, `task qmltypes-check`, `task format-check`, `task qml-import-check`, and `git diff --check`: passed.
- `task license-check`: passed with approved access after the sandbox blocked REUSE multiprocessing sockets;
  463/463 files have copyright/license metadata, no missing licenses or read errors.
- `task install-check`: passed against the corrected Release build; staged installation and desktop launch succeeded.
- `task isolated-runtime-check`: passed with approved Docker access. Offline container build, installed payload,
  MIME/default application checks, and three-second desktop launch succeeded. Evidence:
  `build/container-runtime.phztvez1/verification.log`. Subsequent corrections preserve the installation contract;
  corrected local staged installation passed without repeating unchanged container/provider builds.

Detailed local logs are in `/tmp/files-search-*.log`; clean acceptance artifacts are ignored under
`build/search-lifecycle-acceptance/`. Logs are local evidence, not committed build artifacts.

## Behavioral coverage

Tests cover popup closure/attachment, Home warm-up idempotence and restart restoration of directories,
foreground preemption with resumed Home work, obsolete callbacks, asynchronous shutdown, retained refresh
snapshots, missing-path recreation, injected five-minute freshness, closed-search invalidation, overlapping
roots/component boundaries, debounce, unavailable roots and interval-based refresh failure retry.
Controller tests cover copy/move/trash without watching the searched tree, inline create/rename, and external
watched-directory edits. Store tests cover file/directory round trips, root/policy incompatibility, XDG fallbacks,
interleaved atomic writers, cancellation preserving a previous snapshot, write failure, truncation, corrupted
integrity, trailing bytes, invalid relative paths, and invalid types with a valid digest. Existing exclusion
regressions retain malformed-config behavior and exercise policy invalidation of both cache variants.

## Benchmark

Command: `LD_LIBRARY_PATH=build/deps/prefix/lib build/search-lifecycle-acceptance/tests/files-finder-benchmark
--persist ..`. The benchmark uses temporary cache/config paths and built-in
exclusions on the real ecosystem checkout tree. Final measurements:

| Measurement | Result |
|---|---:|
| Records (files and directories) | 5,927 |
| Cold index readiness | 30 ms |
| Cold query p50 / p95 | <1 / <1 ms |
| Traversal plus streamed persistence | 25 ms |
| Snapshot bytes | 340,083 |
| Validated restored readiness | 14 ms |
| Reconstruction inside restore | 7 ms |
| Restored query | <1 ms, 22 hits |
| Queries alongside refresh p50 / p95 | 1 / 2 ms |
| Process RSS before refresh | 37,608 KiB |
| Process RSS after query samples | 38,956 KiB |
| Process peak/RSS after refresh | 42,396 KiB |

These are warm-filesystem measurements on a small representative source tree, not a full Home scale guarantee.
RSS includes the benchmark's standalone restored index and allocator retention; it is process memory evidence,
not a precise per-snapshot allocation measurement. Query samples run alongside refresh and can extend past its
completion on small trees. Persistence still requires validation and reconstruction before search readiness.

## Native manual acceptance

The user confirmed “All four checks pass” on 2026-10-02 for immediate search after launch, restart/cache reuse,
create/rename while search is open, and closing Files during indexing. Native interaction was performed by the
user; no pointer/focus automation was used. Final lint corrections preserve these flows.

The implementation and acceptance evidence are complete. The full repository `task check` gate remains blocked
by the unrelated baseline clang-tidy errors recorded above; it is not claimed to pass.
