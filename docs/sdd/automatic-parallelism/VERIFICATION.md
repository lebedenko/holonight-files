# Verification

Date: 2026-09-15. Scope authorized by the supplied implementation plan.
Existing Places work was present in the checkout and is preserved.

## Focused checks

- R1: `cmake --list-presets=all` passed; JSON inspection confirms all four build
  presets have no `jobs` property.
- R2: `bash -n scripts/prepare-deps.sh` passed. Disposable command-capture probes
  passed for unset JOBS, empty JOBS, JOBS=2, CMAKE_BUILD_PARALLEL_LEVEL=2,
  and JOBS=2 with CMAKE_BUILD_PARALLEL_LEVEL=3. They check both providers and
  configure/build/install ordering. Real CMake with a capture-only Ninja
  confirmed no default job argument, environment `-j 2`, and explicit
  `--parallel 2` overriding environment value 3.
- R3/R4: Taskfile, CMake tidy target and CI workflow are unchanged. Local
  run-clang-tidy uses multiprocessing.cpu_count() when its default `-j 0` is
  selected. README documents compiler limits and independent tidy workers.
- Probe script and raw captures: `build/build-parallelism/probe.py`, `probe.log`,
  and `*.jsonl`. These are disposable evidence, not permanent tests.

## Build and quality checks

The host reports 16 logical CPUs and 65,744,084 KiB total RAM (about 62.7 GiB).
Commands run with JOBS and CMAKE_BUILD_PARALLEL_LEVEL unset using the disposable
`build/build-parallelism/run.py` wrapper. It records monotonic elapsed time,
child user/system CPU time, and RUSAGE_CHILDREN maximum RSS. RSS is the largest
child process observation, **not** simultaneous aggregate memory use.

The initial `task deps` passed in 0.61s but did no compilation. To exercise real
work, all three Files debug/release/test generated outputs were cleaned using
`cmake --build --preset <preset> --target clean` (`clean.log`). One provider
object was moved into the evidence directory (`dependency-object.txt` records
its path); sibling sources were not modified. The measured `task deps` rerun
passed in 3.03s, rebuilding the theme-generator object and relinking. Largest
child RSS was 373,248 KiB. Details: `deps.log`, `deps.json`.

The first sandboxed `task check` rebuilt Debug, Release and test binaries
(44, 44 and 81 C++ compilation steps respectively; `compilation-counts.json`).
It stopped at `FileOperationService.SocketAndDeviceCopiesAreRejectedWithoutReading`:
Unix socket bind returned permission denied. The other 314 smoke tests passed,
two opt-in native tests skipped, and all seven other CTest entries passed.
Elapsed time was 104.74s; child CPU time 643.75s user + 16.12s system, largest
child RSS 741,956 KiB (`check.log`, `check.json`).

The authorized unsandboxed `task check` rerun passed in 223.66s. All eight
CTest entries passed (37.90s), followed by format-check, 52 actual clang-tidy
analyses, QML lint, REUSE, staged install and uninstall checks. Child CPU time
was 1051.59s user + 18.83s system; largest child RSS was 777,764 KiB.
See `check-unsandboxed.log` and `check-unsandboxed.json`. This rerun reused the
compiled outputs; the first run supplies the clean compilation evidence.
These separate runs are not a clean-build end-to-end benchmark.

R1–R5 acceptance checks are complete. Hosted CI was not run; its existing
`task deps` and `task check` commands inherit these defaults. The two opt-in
native smoke checks remain outside this build-only cycle. The supplementary
process snapshot saw only its own sandbox namespace and is not used as resource
evidence; resource observations above come from the command wrapper.

GNU /usr/bin/time was unavailable; the Python wrapper supplies equivalent
required timing/resource observations. An initial disposable Ninja probe omitted
its capture environment during generation; corrected before the passing run.
No UI behavior, install payload or install rules changed, so native visual and
isolated-runtime checks are outside this cycle. No performance speedup is claimed.
