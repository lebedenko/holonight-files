# Design

Status: approved by the supplied implementation plan; no scope changes.

Remove `jobs: 2` from the four build presets (R1). In prepare-deps.sh, construct
a Bash array containing `--parallel` and the quoted JOBS value only when JOBS
is nonempty (R2). An empty array adds no arguments. Explicit JOBS takes
precedence over CMAKE_BUILD_PARALLEL_LEVEL; otherwise CMake handles that variable.

Keep the provider loop, Taskfile and run-clang-tidy invocation unchanged (R3).
Update README defaults and examples and link this cycle from the backlog (R4).
Use disposable command-capture probes and build logs under build/build-parallelism
(R5). Ensure at least one real compilation per Files configuration and real tidy
analysis; record whether dependency builds compile or reuse existing output.
Use process-tree timing observations without claiming whole-machine peak memory
or a benchmark speedup. No UI inspection or isolated install-runtime check is
needed for this build configuration change.

Implementation and verification complete; see [verification](VERIFICATION.md).
