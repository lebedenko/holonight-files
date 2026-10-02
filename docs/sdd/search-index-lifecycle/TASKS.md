# Tasks

| Task | Requirements | State | Evidence |
|---|---|---|---|
| Lifecycle and worker coordination | L1–L3, L6, L9–L10 | Done | PathFinderModel, startup and shutdown wiring |
| Private persistent snapshot codec | L4–L5 | Done | PathIndexStore and policy fingerprint |
| Invalidation sources and generations | L7–L8 | Done | Controller task/watcher/edit wiring |
| Focused regressions and benchmark | L1–L10 | Done | Finder/store tests and --persist benchmark |
| Required checks and isolated acceptance | L1–L10 | Blocked | Clean acceptance 21/21; installed/container runtime pass; full task check blocked by baseline tidy errors in 27 unchanged files; see VERIFICATION.md |
| Native manual acceptance | L1–L10 | Done | User confirmed all four checks pass on 2026-10-02; see VERIFICATION.md |
