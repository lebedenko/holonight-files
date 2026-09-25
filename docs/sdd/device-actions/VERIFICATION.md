# Device Actions — Verification and Review

## Status

The 2026-09-25 review found the T-015 and T-016 gaps. They were fixed on 2026-09-26. Focused
regressions and the full `task check` gate pass. Native acceptance remains manual.

## Focused checks

| Check | Result |
|---|---|
| `QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software build/test/tests/files-smoke '--gtest_filter=StoragePolicy.*:FilesStorage.*'` | Passed: 42 tests. These tests use the existing build and do not cover T-015's unsafe capability combinations or T-016's overlapping IDs. |
| Same filter after T-015 and T-016, using the rebuilt test binary (2026-09-26) | Passed: 47 tests, including reader, enclosure, empty-optical, stale-row and overlapping-ID regressions. |
| `task deps`, `task build PRESET=test`, `task format` (2026-09-26) | Passed. |
| `task check` outside the sandbox (2026-09-26) | Passed: debug and release builds, 21 CTest targets, format, C++ and QML lint, license, staged install, import policy, and QML metadata. |
| `rg -n '[[:blank:]]+$' docs/sdd/device-actions/{SPEC,DESIGN,TASKS,VERIFICATION}.md` | No trailing whitespace found. |
| Requirement count (`rg -c '^#### REQ-' docs/sdd/device-actions/SPEC.md`) | 64. |

No native UI interaction was automated. The historical manual acceptance mark in TASKS.md predates
this review; its observations are not attached here. The first `task check` attempt on 2026-09-26
passed builds but failed in the sandbox because the unrelated Unix-socket test could not `bind()`.
The unrestricted rerun passed all 21 CTest targets, then found a `readability-use-anyofallof`
diagnostic in the new scope helper. That diagnostic was fixed; targeted tidy and the final full
gate passed.

## Implementation findings and fixes

1. `storage_policy.h::removalVerb()` selected PowerOff for a medium-removable drive with
   `canEject = false`, `canPowerOff = true`. The original
   `StoragePolicy.MultiSlotReaderNeverYieldsPowerOff` fixture used `canEject = true` for every slot, so it
   missed this case. The policy now falls back to Unmount or no verb and the test covers both
   `canEject` states (T-015).
2. `DevicesModel::volumeRow()` and `appendEmptyDrives()` exposed that verb without a scope guard.
   A self-removable drive in a multi-drive enclosure could also expose PowerOff for sibling drives.
   `DevicesModel::remove()` trusted the cached row verb. The controller checks for a scope change,
   but an unchanged sibling-wide scope was not rejected by the Files policy. `safeRemovalVerb()`
   now downgrades it in the row, and `remove()` checks scope again before dispatch (T-015).
3. `appendEmptyDrives()` called the same fallback policy. An empty optical drive with
   `canEject = false`, `canPowerOff = true` previously offered PowerOff. It now stays visible with
   no removal verb (T-015).
4. Capacity refresh is attempted every 30 s for mounted rows. The timer is not tied to sidebar
   visibility, and a previous value remains visible while a new query is in flight. SPEC now
   states this actual cadence rather than a maximum age for displayed data.
5. `DevicesModel::refresh()` sorted by `(classRank, driveId + targetId)`. Concatenation can interleave
   rows for drives with overlapping IDs, invalidating the grouping pass's contiguous-run
   assumption. The sort now uses `(classRank, driveId, targetId)` and a regression covers the
   overlapping-ID case (T-016).

## Pending acceptance

- Run `task isolated-runtime-check` before publication.
- Ask the user to perform any remaining native visual checks manually and record the result.
