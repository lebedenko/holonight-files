# Automatic build parallelism

Status: approved scope through the user's implementation-plan instruction.

## Requirements (EARS)

- R1: When any Files build preset is used without an override, the build shall
  use CMake/Ninja automatic parallelism, with no preset job limit.
- R2: When `JOBS` is nonempty, dependency builds shall pass `--parallel "$JOBS"`;
  otherwise they shall omit the parallel option and honor CMake/Ninja defaults
  and `CMAKE_BUILD_PARALLEL_LEVEL`.
- R3: The workflow shall retain sequential dependency providers, check stages,
  lint stages, and tidy prerequisite builds. Tidy shall retain its existing
  all-detected-CPU worker default.
- R4: Developer documentation shall explain defaults, explicit limits, and the
  independence of tidy workers from compiler job limits; CI shall inherit the
  same build defaults.
- R5: Verification shall validate presets and override command construction,
  run `task deps` followed by `task check`, and retain commands, elapsed times,
  observed resource use and real compilation/tidy evidence under `build/`.

## Scope and non-goals

Automatic means tool-managed scheduling, not an exact CPU-sized worker count.
The intended host has 64 GB RAM; no specific speedup is promised. Dependency
sources, application behavior, test-process parallelism, and concurrent
build/tidy execution are outside scope. No permanent tests are required.

Implementation and verification complete; see [verification](VERIFICATION.md).
