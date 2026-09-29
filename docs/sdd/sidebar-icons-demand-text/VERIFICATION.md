# Verification

2026-09-29. Native interaction is never automated.

## Focused regressions

Built with `task build PRESET=test` and formatted with `task format`.
Ran `files-smoke` offscreen with software rendering, project-local QML/library paths,
and filter `*Preview*:*SidebarFallbackContinuity*:*PendingQuickLook*:*ObsoleteTextRequest*:*QuickLook*:*InspectionKeys*`.
The expanded run passed 147 tests and skipped four opt-in performance tests; its three
new fallback fixtures initially failed. After fixing the fixtures' window ownership and
forced-source bindings, `PreviewConsumers.*:*SidebarFallbackContinuity*` passed all seven.
The subsequent complete CTest suite passed all 21 targets. After correcting lint-only
test declarations and tightening shutdown-fixture cleanup, the final focused run
(`PreviewService.*:PreviewIntegration.*:PreviewConsumers.*:*SidebarFallbackContinuity*:*PendingQuickLook*:*ObsoleteTextRequest*`) passed all 65 tests.
Targeted clang-tidy passed for all three affected test files.

Coverage:

- `SidebarFallbackContinuity` verifies matching full chains through five rapid changes
  for theme, bundled and question-mark tiers, then hiding/showing, changed chains and clear.
- `PreviewConsumers` preserves initial delay, refresh, image delivery during metadata work,
  cache reuse, sizing, outgoing fade and interruption behavior.
- `BrowsingNeverDispatchesTextAndReopeningReloadsIt` uses a worker hook to establish zero
  text dispatches and zero lines during `.txt` and `.md` browsing; each reopening loads again.
- `PendingQuickLook` blocks inspection while Space opens and pins the overlay, then resolves
  supported text or unsupported Markdown. Navigation cannot move the listing or unloaded lines.
- `ClosingDuringInspectionPreservesInspectionWithoutLoadingText` and
  `ClosingBlockedTextThenReopeningRejectsOldRequest` exercise separate cancellation lifetimes.
- `TextReopenRevalidatesReplacementAndKeepsSidebarErrorSeparate` replaces the file with binary
  content between inspection and text opening; no lines appear and only Quick Look gets an error.
- `ObsoleteTextRequest` covers refresh, retarget and shutdown while text is blocked.
- Existing text-reader, service, controller and QML tests cover empty/unreadable text,
  100 KiB truncation, UTF-8 handling, line navigation, images, sizing and cancellation.

## Project checks

The first `task check` stopped at a sandbox-denied Unix socket bind in the unrelated
`FileOperationService.SocketAndDeviceCopiesAreRejectedWithoutReading` fixture.
An authorized unrestricted `task check` passed the full CTest suite (21/21), then
reported six readability errors in new test declarations/conditionals. Those were
fixed. The final unrestricted `task check` passed, exit 0: debug/release builds,
all 21 CTest targets, formatting, C++ and QML lint, REUSE licensing, staged
installation, runtime import policy and generated QML metadata.
Log: `/tmp/demand-check-final.log`.

`task isolated-runtime-check`: blocked by the existing runtime environment. The
sandbox attempt staged successfully but could not access the Docker socket. The
authorized unrestricted run built image `48b76ac81417` from base `files-ci`
`51d15f69535e`; `docker run --rm --network none` exited before app startup. A traced
disposable rerun stopped at `test -w /home/files-test`: the ordinary container user
cannot write its home. This matches the blocker in sidebar-thumbnail-fade. No
packaging/base-image changes or acceptance bypasses were made. Fix the environment
and rerun this gate before any authorized publication. Logs:
`/tmp/demand-runtime-unrestricted.log`, `/tmp/demand-runtime-trace.log`.

## Manual native acceptance — passed

The user reported “all checks passed” on 2026-09-29 for the requested checks:

1. Run `build/debug/apps/files/hn-files`; navigate rapidly with j/k across matching icons,
   then different icons and images. Confirm stable matching fallbacks and the existing fade.
2. Press Space during detection and on classified text. Confirm loading and pinned navigation.
3. Close while loading and reopen. Confirm fresh content and first-line initialization.

No publication or umbrella-pin change is part of this cycle.
