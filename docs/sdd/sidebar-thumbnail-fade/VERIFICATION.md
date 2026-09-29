# Verification

Date: 2026-09-29. Implementation authorized by the supplied plan. No publication,
umbrella pin changes or native input automation.

## Automated checks

- `task deps`: passed; existing local providers are current.
- `task format` and `task build PRESET=test`: passed.
- Offscreen/software `files-smoke` filter
  `PreviewConsumers.*:PreviewService.*:PreviewIntegration.*:Files.PreviewSidebar*:Files.InspectionImageSplitterAndPixelSizing`:
  **58/58 passed**. Log: `/tmp/sidebar-fade-focused.log`.
- The new presentation regression checks the 120 ms duration, nonzero intermediate
  opacity, uninterrupted animation across rapid navigation, pixel release at completion,
  absence of image resurrection, early replacement at full opacity, clear, decode failure,
  timeout, image-free completion, folder fallback, current filename/metadata, hide/show,
  resize and resolution replacement. Existing tests cover real workers, cached revisits,
  stale delivery suppression, icon timing, and fixed frame geometry.
- Initial focused run: 57/58 passed because the new windowless pane fixture had no parent
  to restore effective visibility after hiding. Adding a host item corrected the fixture;
  the production fade needed no change. The corrected focused suite passed.
- `task check`: the sandbox run stopped at the existing socket-creation regression.
  The rerun outside the sandbox **passed**, exit 0: all 21 CTest targets, debug/release
  builds, formatting, C++ and QML lint, REUSE, staged installation, import policy and
  generated QML metadata checks. Log: `/tmp/sidebar-fade-check-unsandboxed.log`.

These automated checks do not constitute native visual acceptance.

## Native acceptance

Accepted by the user on 2026-09-29: “Works really good. Accepted. Commit”.
The user was directed to the rebuilt regular application:
`./build/debug/apps/files/hn-files ~/Pictures/holonightw`.
This closes the manual native timing acceptance item for the 120 ms fade.

The initial lab launch instruction omitted its required `FILES_NATIVE_EVIDENCE`
path and caused a setup abort. The corrected regular-app command was supplied
before the user's acceptance.

## Pending publication gate

`task isolated-runtime-check`: **blocked**, not passed. Docker access required execution
outside the sandbox. The image built successfully (`b3e4d92d3905`, base `files-ci`
`51d15f69535e`), but its acceptance script exited at `test -w /home/files-test`:
the container's ordinary `files-test` user cannot write its home. A traced disposable,
network-isolated rerun confirmed the failing condition before application startup.
This matches the blocker recorded in the sidebar-icon-delay cycle. Logs:
`/tmp/sidebar-fade-runtime-unsandboxed.log`, `/tmp/sidebar-fade-runtime-trace.log`.
No packaging or base-image changes were made. Fix the runtime environment and rerun
this gate before any authorized publication.
