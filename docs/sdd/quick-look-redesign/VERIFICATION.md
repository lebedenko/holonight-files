# Quick Look redesign verification

Date: 2026-09-16. Review remediation covers pending-window resizing and blocked-worker
cleanup. Native rendering and latency acceptance remain pending. Generated logs and
review probes are under `build/`; repository REUSE annotations cover license metadata.

## Requirement evidence

| Requirements | Evidence |
| --- | --- |
| REQ-F-001–005, REQ-C-004 | Card bounds, centering, filename elision, hint colors and modal backdrop tests in `tests/smoke.cpp`. |
| REQ-F-006–012 | Image aspect-fit and metadata, text scrolling/monospace/truncation, compact directory/error/MIME tests. |
| REQ-F-013–015 | `QuickLookRetainsSettledGeometryWhilePending` holds the decode worker and checks fixed-window geometry, immediate captions, spinner and absence of stale images. |
| REQ-F-001/005/013 | `QuickLookPendingResizeKeepsCaptionInsideCard` shrinks 1280×800 to 640×420 while decoding is blocked, checks the frame and close hint stay inside the card/window, then restores the original bounds and geometry. |
| REQ-F-016–018 | Reopen geometry and requested-size stability tests. |
| REQ-NF-001, REQ-C-001/002 | Binding-loop and focus/navigation regression tests preserve key routing and object names. |
| REQ-C-003/005 | No decode service or sibling-source edits in this remediation; no new QML file requires format-list registration. |

## Review findings and remediation

- The original review probe placed the close hint at y=747 after shrinking to a
  420-pixel-high window. `settle()` now retains the settled entry kind and source
  dimensions and refits them using current bounds, including while pending.
- Both original blocked-worker tests now declare the release guard after their
  harness. On fatal assertion exits the guard runs before `PreviewService` joins
  the worker thread. The new resize regression uses the same safe ordering and
  deliberately leaves the worker blocked until scope cleanup.
- T-019 and T-020 were previously checked without recorded native evidence. They
  are pending again. Test references in TASKS.md now name the actual consolidated
  tests rather than nonexistent standalone assertions.

## Commands and results

- `task deps` and `task build` passed; logs: `build/quick-look-fix-deps.log` and
  `build/quick-look-fix-build.log`.
- `task test` passed all eight CTest targets in 42.93 seconds outside the sandbox
  (the socket-bind fixture requires it); log: `build/quick-look-fix-test.log`.
- Final `task check` exited 0: debug/release builds, all eight CTest targets
  (43.13 seconds), formatting, C++ and QML lint, REUSE license validation, staged
  installation/launch and uninstall checks passed. Log: `build/quick-look-fix-check.log`.
- `git diff --check` passed after documentation updates. No install rules or desktop
  entries were changed by this remediation; no push or isolated runtime check was needed.
- Spark delegation was attempted for the isolated guard-order fix, but the configured
  spark model was unavailable. Changes and integration review were completed locally.

## Pending native acceptance

- T-019 / REQ-NF-002: record user confirmation on Hyprland at scale 1.5 that card
  and image corners and caption text are crisp, captions centered, and the listing
  dimmed. Offscreen geometry tests do not establish native visual acceptance.
- T-020 / REQ-NF-003: run the opt-in `Files.NativeInspectionAcceptance` test on a
  native display and record its Quick Look opening time below 200 ms. This test
  is skipped by the normal automated suite.
