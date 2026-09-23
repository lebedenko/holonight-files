# Tasks

Approved scope: supplied implementation plan.

- [x] T1 (REQ-S-001/002/003): implement tier sizing, lookup, safe on-demand writes and focused tests.
- [x] T2 (REQ-S-004/005): integrate single display result, adequate memory reuse and async tests.
- [x] T3 (all): run deps/build/tests/check; record evidence under build/sharp-previews.
- [x] T4 (all): update README/backlog and verification.
- [x] T5 (all): native scaling/navigation/resize/Quick Look, photo sharpness and representative cold/warm latency acceptance.

Spark delegation attempted for T1; configured model unavailable. Main owns all edits.

T5 continuation (2026-09-23): [native acceptance tasks](../native-preview-acceptance/TASKS.md)
and [dated evidence](../native-preview-acceptance/VERIFICATION.md). Passive lab,
photographic pins and a demonstrated fractional window-DPR correction are local;
all required native scale rows must pass before checking T5. Historical records
and the unrelated unknown-dimension runtime fixture remain unchanged.

Current-build continuation: [single-monitor candidate](../native-preview-acceptance/SINGLE-MONITOR.md).
T5 requires all four current-build single-monitor rows. Physical second-monitor
testing awaits hardware and is explicitly outside this iteration’s closure gate.

T5 completed locally — 2026-09-23: the user-approved 1/1.25/1.6/2 matrix passes,
including all timing/startup thresholds, visual checks and 72 matched-reference
comparisons. Original 1.25× restored. See the current candidate report above.
The historical comments describe earlier handoffs; the unknown-dimension runtime
fixture and physical second-monitor qualification remain deferred.
