# Tasks

| ID | Requirements | Deliverable | State | Evidence |
|---|---|---|---|---|
| E-T1 | E5–E6 | String lists and typed search section | Done | Implemented; focused tests and changed-file tidy pass; included in the local implementation commit |
| E-T2 | E1–E5, E8 | Compiled exclusions before candidate emission/traversal | Done | Implemented; pruning/root regressions pass; included in the local implementation commit |
| E-T3 | E6–E7 | Reopen reload, retained valid policy, diagnostics and cache invalidation | Done | Implemented; both-cache invalidation, reuse and stale callbacks pass; included in the local implementation commit |
| E-T4 | E1–E8 | Focused regressions and before/after traversal plus warm measurements | Done | 38/38 focused tests; measurements in VERIFICATION.md; included in the local implementation commit |
| E-T5 | E1–E8 | Required checks and isolated runtime | Blocked | CTest 27/27 and remaining checks/runtime pass; task check stops on baseline clang-tidy errors in 28 unchanged files |
| E-T6 | E3–E7 | User native hidden/excluded/root/reload acceptance | Done | User confirmed all checks 2026-10-02: “Verified. Works properly”; recorded in the local implementation commit |

The user authorized publication and pinning on 2026-10-02 after reviewing the recorded baseline lint blocker.
The umbrella records the published revision and CI status. Umbrella reacceptance remains pending.
