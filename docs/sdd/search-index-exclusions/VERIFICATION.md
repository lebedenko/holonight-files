# Verification — 2026-10-02

Files baseline: `2b8da3fccc0829a2b9c33b79e56304e9861f7b97`, plus the locally committed exclusion implementation.
Search provider: unchanged `d12e441c06d1df87450514136c5a2d458d985ed3`.
All provider artifact revisions in `build/deps/provider-revisions.tsv` matched the umbrella gitlinks.
Host: GCC, Qt 6.11.2; clang-tidy LLVM 23.1.1. Raw evidence is retained under
`build/search-exclusions-verification/` and `build/container-runtime.wy7rtzce/` (ignored build artifacts).

## Focused regressions

Build: `cmake --build build/test --target files-smoke files-finder-benchmark -j 4` — passed.

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
QML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml" \
build/test/tests/files-smoke \
  --gtest_filter='SearchExclusions.*:PathFinderModel.*:TomlDocument.*:SettingsRegistry.*:AppSettings.*'
```

Final result: 38/38 passed, 749 ms. Coverage includes homogeneous/empty/mixed list validation, rule unions,
case-sensitive wildcard/literal matching, spaces/Unicode/nonexistent paths, component boundaries, XDG fallbacks,
hidden opt-in, pruning before emission, excluded roots and descendants with child exclusions, both hidden/visible
cache invalidation, equivalent-policy reuse, stale scan callbacks, malformed first load, unreadable configuration,
last-valid-policy retention, visible diagnostics, config removal and read-only behavior. Existing finder regressions
cover cancellation, stale queries, unavailable roots, file/directory modes and symlink traversal behavior.

Changed C++ translation units pass the repository's unmodified clang-tidy configuration:
`clang-tidy -removed-arg=-mno-direct-extern-access -p=build/test --config-file=.clang-tidy <file>`.
All ten changed/new `.cpp` files were checked; the new test file was rechecked after its final tidy correction.
The touched source files also received trailing-comma fixes required by the installed clang-tidy version.

## Cold index traversal and warm reuse

A fixed tree at `/tmp/holonight-exclusion-measurement` contains 2,000 `needle-N.txt` files in each of:
`.git/objects`, `.venv/lib`, `build-debug/objects`, `node_modules/modules`, `__pycache__`, `.codex/logs`,
`.config/settings`, `.local/share/documents`, and `src` (18,000 files total).

`build/test/tests/files-finder-benchmark --traverse /tmp/holonight-exclusion-measurement` compares the same
scanner with no exclusions (the former traversal policy) and built-ins, with hidden mode on. Measurements are
cold-index traversal, not a claim of uncached disk I/O; filesystem caches were not forcibly dropped.

| Policy | Candidates | Traversal |
|---|---:|---:|
| Former unpruned traversal | 18,017 | 43.169 ms |
| Built-in exclusions | 6,006 | 12.876 ms |

Candidate count fell 66.7%; traversal time fell 70.2% on this fixture. Finder reopening reused the completed
6,006-record index. Twenty warm `needle`/`needle ` queries gave p50 0 ms and p95 1 ms. This fixture measurement
is separate from historical two-million-path provider/index evidence and does not replace it.

## Required acceptance

- `task check` was run. The sandbox first blocked a Unix-socket fixture and accelerated rendering.
  Rerunning outside the sandbox passed Debug/Release builds and all 27 CTest entries, including six accelerated
  cases (72.12 seconds). The main smoke suite passed 730 tests and skipped seven opt-in/environment scenarios.
- Full `task check` then stopped in clang-tidy: the unchanged baseline has errors in 28 untouched files,
  including trailing-comma diagnostics in `storage/capacity_probe.cpp` and move diagnostics in
  `browsing/name_validator.cpp`. Changed-file findings were corrected and rechecked. The raw baseline error list
  is in `build/search-exclusions-verification/baseline-tidy-errors.txt`. Full task check is **not claimed passed**.
- After the final corrections, the affected build/focused tests, changed-file tidy, formatting, and runtime
  checks were rerun. Unaffected expensive tests were not repeated automatically.
- `task qml-lint`, `task license-check`, `task install-check`, `task qmltypes-check`,
  `python3 scripts/format-sources.py --check`, `python3 scripts/check-qml-import-policy.py`, and
  both repository/umbrella `git diff --check` passed. Umbrella-only `reuse --no-multiprocessing lint`
  and new/updated documentation relative-link checks also passed. REUSE covered 456/456 Files files. Licensing was rerun
  outside the sandbox after its multiprocessing socket was denied.
- `task isolated-runtime-check` passed a fresh Release provider/application build and installed desktop launch
  without runtime networking. Final source corrections reused that environment's compatible provider artifacts,
  rebuilt the affected Files application and staged payload, then passed a new runtime image's desktop launch
  (observed for three seconds). Build logs were inspected; no actionable compiler/QML build warnings remained.
  Verified CI image: `sha256:73d3f25e93d315608e9ee031bac1364a86e1a1b7747f56990f0e54bee6a4ffe5`.
  Final runtime image: `sha256:34bb2b4f6bc52eb8bcafce9f46429530b378085426c1f22acf2e079ac8d64952`.
  Docker FROM requires a local image tag; the final build tagged the verified CI image before using it.

## Native acceptance and remaining handoff

The user verified the updated Debug Files application: Include hidden finds `.config` content; built-in/user
exclusions remove results; explicit excluded roots and descendants allow contents but retain child exclusions;
edited `[search]` rules reload on reopening. Reply on 2026-10-02: “Verified. Works properly”.
No pointer or focus automation was used.

At local verification, implementation was committed locally and unpublished; no Files or search-provider pin
had changed. The user subsequently authorized publication and pinning on 2026-10-02 after reviewing the
recorded baseline lint blocker. The umbrella records the exact published revision, pin and latest CI status.
Required full lint remains blocked by baseline findings. Shared Search stays Accepted: final reacceptance
requires compatible published pins and umbrella integration evidence. UDisks2 discovery
review remains an independent open task; configuration interoperability is Draft and does not block exclusions.
