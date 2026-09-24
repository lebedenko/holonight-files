# Design

Add versioned measurement metadata and strict five-trial JSON/Markdown comparisons, then run fresh identical-production baseline/candidate datasets.

Extend the existing runner without changing measurement defaults. Record definitions, fixtures, source and instrumentation hashes, effective build settings, compiler/Qt, provider artifact hashes and rendering. Recompute median/min/max from validated raw trials. Reject failed/incomplete/non-finite/schema-incompatible data and workload changes. Require explicit named provenance fields and reasons for intentional differences. Preserve original paths while normalizing checkout/build roots for compatibility. No significance claims or timing gates.

Retain each process exit status and wait4 peak RSS independently of test XML, so a
shutdown failure cannot be certified by a passing test record. Comparison regressions
cover this boundary as well as schema, provenance, evidence corruption and arithmetic.

Acceptance collects cache, resize and pressure in both datasets: 30 measured
processes plus two separate fixture-generation processes.
