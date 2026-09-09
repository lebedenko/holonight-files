# Inspection remediation verification

Date: 2026-09-09. Scope: approved review plan, REQ-R-001–008. Existing uncommitted
Stage 2 implementation was preserved and corrected. Spark implemented the initial
bounded EXIF change; the main agent completed its review/tests after Spark reached
its usage limit. Installed HoloNight packages were consumed; sibling sources were
not modified. Generated fixtures, captures and logs are under `build/`.

## Automated evidence

`task deps`, `task build`, `task test`, `task build PRESET=release`,
`task format-check`, `task qml-lint`, `task license-check`, `task tidy`, and
`task install-check` have passed. The final metadata assertion is additionally
checked with targeted clang-tidy (`build/remediation-tidy-final-smoke.log`).
Logs: `build/remediation-{build,test,release,format-check,qml-lint,license,install,tidy}.log`.
REUSE initially failed because its multiprocessing socket was sandbox-blocked;
the authorized unsandboxed rerun passed.

The latest full CTest run passes 5/5 targets. Its smoke executable runs 87 tests:
85 pass, two opt-in performance tests skip. Inspection's opt-in native test is run
separately below; the other skip belongs to the earlier browse-folder cycle.

| Requirements | Evidence |
| --- | --- |
| R1 | Real QQuickWindow j/k→Space, focused delegate, repeat Space and release, selectable popup text j/k/Escape, fullscreen preservation, focus restoration, Enter/Return/l with intercepted QDesktopServices URLs. No external opening on Space. |
| R2 | Metadata-only FIFO, regular→FIFO before open, subsequent normal preview and shutdown in a child process with 10s hard timeout; replacement after thumbnail still decodes image/EXIF from original descriptor. Regular symlink and non-root permission denial/recovery; explicit text open/read errors. |
| R3 | JPEG/PNG five-field extraction; corrupt/malformed/no-EXIF cases, 2MiB EXIF omitted, cancellation, 256MiB sparse payload skipped with <64KiB reads, 4,096-record bound. |
| R4 | Thumbnail visible while delayed full decode remains busy; timeout removes delivered thumbnail and stays sticky; latest target wins and 100 pending target changes start only active plus latest work. |
| R5 | Full decode reuse, two-entry LRU eviction, 64MiB retention eviction, oversized image display without retention; same-size/restored-mtime changes invalidate both memory and disk results. |
| R6 | Gradual one-pixel resize growth, active/inactive consumer changes; actual mouse splitter drag, aspect preservation, popup required pixel sizing and pane size restoration. |
| R7 | Selected-file edit, chmod, atomic replacement, reattached watch, rename and deletion without cursor movement; empty selection closes popup. |
| R8 | Binary unit formatting with exact bytes and unavailable values; native locale timestamp captures. |

## Native acceptance

Wayland desktop, user `andrii` (uid 1000), installed Qt 6.11 providers. Commands and
captures for English/Ukrainian × dark/light × 1.0/1.5 scale are retained in
`build/native-matrix/run.sh` and `build/native-matrix/`. Eight combinations passed
five checks each: keyboard, fullscreen restoration, selected-file refresh, and
image/splitter pixel sizing. Representative
text and image popup/pane captures were visually inspected: readable themed text,
preserved image aspect ratio and locale-dependent dates. This caught scientific
notation in exact byte counts; the final formatter uses a full integer string and
a QML regression asserts the `1440054` byte value. Static fractional scale is covered; live
cross-monitor DPR migration remains pending because no such move was performed.

`Files.NativeInspectionAcceptance` uses 101 distinct-color BMPs, each 5.76MB, and
checks every delivered image against the selected row while issuing native key
input. A 16ms render timer requests frames during the stress interval; frameSwapped
counts measure delivered frames, not a timer-only estimate. Peak RSS is Linux
VmHWM for this separate process. Initial timing includes QML window creation;
cached/Quick Look timing measures first specialized image delivery.

Final native run (`build/native-acceptance-final.txt`, `build/remediation-native-final.log`);
the earlier passing run is retained in `build/native-acceptance.txt`:

| Metric | Result | Required |
| --- | ---: | ---: |
| Initial/uncached preview | 249ms | <500ms |
| Cached preview | 11ms | <100ms |
| Quick Look update | 1ms | <200ms |
| Movement rate | 10.89/s | >=10/s |
| Stale image deliveries | 0 | 0 |
| Peak RSS | 358,068KiB (349.7MiB) | <500MB |
| Rendered frame rate | 60.02 FPS | >=30 FPS |

## Limits and pending acceptance

- Live cross-monitor DPR changes remain pending; static 1×/1.5× runs and consumer
  pixel-sizing regressions cover the implemented reporting path.
- Qt image decoding cannot be hard-preempted. The 3s deadline clears specialized
  content and cancels subsequent stages, but shutdown may wait for an ongoing Qt
  decoder call. Allocation/header limits, EXIF bounds and cooperative cancellation
  are the approved mitigations; subprocess image decoding remains outside scope.
- Measurements are fixture/hardware-specific, not guarantees for every image
  plugin or pathological file. No distribution release was published.
