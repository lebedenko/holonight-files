# Local CI rehearsal

Work package CI-017; baseline dea9ba0c5cdbe4ffdcd7950ca75b5e84c6b744ab.
The approved umbrella plan authorizes this specification, design and implementation.

- [Requirements](SPEC.md)
- [Design](DESIGN.md)
- [Tasks](TASKS.md)

## Verification — 2026-10-04

- Clean container run: `build/ci/20261004T155632Z-pp_5n9kf/`. Full task check,
  additional en_US.UTF-8 tests, both required offline namespace executables and
  REUSE 6.2.0 pass. Both locale runs pass all 21 CTest registrations. The 754-case
  smoke executable retains seven existing opt-in/native/reserved-block skips;
  permission checks run as files-test. Full format/tidy, QML lint/types/import
  policy, desktop staging and three-second installed launch are preserved.
- That full task ci invocation exits 201 because its final pre-existing-process
  fixture exposed missing image-level offscreen settings. Preserve that failed
  report. Restore the original inherited QT_QPA_PLATFORM/QSG_RHI_BACKEND values
  in the CI runtime image; runtime-only rebuild and all nine verifier fixtures
  pass (`build/ci/runtime-environment-followup/`). No unchanged application build
  or namespace check is repeated. `build/ci/final-acceptance.json` links the
  complete evidence without replacing the original lane results.
- Source/development isolation passes for 78,421 files, checking bytes, modes,
  symlinks and modification times. All emitted artifact files are host-owned.
- Native exact-provider task check passes with GCC 16.2.1, Qt 6.11.2 and
  clang-tidy 23.1.1 (`native-workspace/native-check.log`). After extracting the
  deterministic icon fixture, the affected rebuild, all 21 tests in both locales
  and focused tidy pass (`native-theme-build.log`, `native-theme-tests.log`,
  `native-theme-tests-en-us.log`, `native-theme-tidy.log` in that workspace).
- Read-only native clang 23 and pinned clang 22 audits pass all 116 owned
  translation units. The final pinned run additionally covers the extracted
  icon fixture in its complete tidy pass. Preview helper probes compare 61
  baseline/current output hashes exactly; string/character literals are unchanged
  before the intentional shared-theme fixture extraction.
- Eleven launcher/tooling regressions pass, including snapshots with edits,
  deletion/new/ignored/space-containing paths, executable bits/symlinks, real
  HEAD/index, failure propagation, missing runtimes, rootless Podman arguments,
  offline isolation and captured runtime fixture failure. Root invocation fails
  before permission checks can silently skip. Actual Podman is not installed.
- A real negative namespace check removes the seccomp exception: both required
  executables fail immediately, CTest returns nonzero and the launcher propagates
  it (`build/ci/namespace-required-negative/`). SDK MIME ancestry probe passes
  with pinned shared-mime-info 2.5.1-2 and regenerated cache.
- Final documentation REUSE, shell/Python syntax and diff review pass. No pushes,
  remote acceptance claims, host AppArmor/sysctl changes or umbrella pin updates.
