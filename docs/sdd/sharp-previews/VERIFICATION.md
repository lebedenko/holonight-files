# Verification — 2026-09-15

Implementation is ready for review; native acceptance remains pending.
Generated evidence is under `build/sharp-previews/` (not committed).

| Requirements | Evidence | Result |
| --- | --- | --- |
| REQ-S-001 | ThumbnailService tier boundaries, physical aspect fit at 1/1.5/2, small originals; consumer resize tests | Pass |
| REQ-S-002/003 | Selected-only disk writes, larger-tier lookup, strict URI/time/size/revision rejection, undersized/corrupt cache, write failure, fresh-service disk reuse and memory reuse | Pass |
| REQ-S-004 | Delayed adequate decode, stable image cache key through EXIF completion, retained pixels during Quick Look upgrade, revision changes | Pass |
| REQ-S-005 | Existing cancellation/timeout/descriptor/metadata/text tests; revised native-sized large fixtures for entry/byte eviction and >1024 direct decode | Pass |
| All | `task deps`, `task build` — deps.log, build.log | Pass; providers installed under build/deps, no sibling sources modified |
| All | Focused `ThumbnailService.*:PreviewService.*:PreviewDecodeLimits.*` — focused.log | 30/30 pass |
| All | `task test` — test-final.log | All 8 CTest targets pass; native-only checks remain skipped |
| All | `task check` — check-final.log | Debug/release, tests and formatting pass; stopped at new lint findings, all corrected and verified below |
| All | Targeted clang-tidy rerun of thumbnail service and all changed test files — tidy-final.log | Pass; remaining translation units passed the full tidy invocation |
| All | `task format-check`, `task qml-lint` — format-final.log, qml-lint.log | Pass |
| All | `task license-check`, `task install-check`, `task uninstall-check` — license-final.log, install.log, uninstall.log | Pass; completes the remaining required check stages |
| REQ-S-001/004 | Offscreen splitter and Quick Look at QT_SCALE_FACTOR=1,1.5,2 — scale-*.log and scale-*-image-{pane,popup}.png | Pass; checkerboard captures reviewed for layout/aspect ratio and visible detail |

Initial test/check logs retain the one-pixel test rounding mismatch (corrected to
Qt integer aspect-fit rounding) and sandbox-denied Unix socket fixture. The suite
passes with permission to run outside the sandbox. REUSE also required this
permission for its multiprocessing socket. No installed payload/rules changed,
so the conditional isolated-runtime check does not apply. Quick Look acceptance now caps
required pixels to source resolution instead of expecting source upscaling.

## Limits and pending acceptance

- Native compositor navigation, splitter resize, Quick Look/fullscreen transitions
  at 1x, 1.5x and 2x, fine photographic detail comparison and representative cold/warm
  latency distributions remain pending. Offscreen captures cannot establish these.
- The deterministic warm-cache test proves zero additional original-image decodes
  after fresh-service disk reuse and smaller-request memory reuse. The local 2400x1600 synthetic JPEG sample recorded 20ms cold and 10ms
  warm (latency.log; 10ms polling granularity), with one cumulative original
  decode across both requests. This is not a production latency guarantee.
- Unknown-dimension sources take bounded direct decode without disk tier creation;
  this branch has code review coverage, but no installed format fixture with an
  unavailable size header was identified. Such a runtime fixture remains pending.
- Existing hard-preemption limits remain: Qt image decoding is blocking; cancellation
  is cooperative with the existing three-second visible timeout and allocation cap.
- Broader other-app thumbnail compatibility, cache migration and deletion are deferred.
- Spark delegation was attempted; its configured gpt-5.3-codex-spark model was unavailable.

After the full suite passed, the visual fixture changed from solid color to an
8px checkerboard and latency logging switched to stdout. The affected visual
test passed again at all three scales, the warm-cache test passed, and
`task format-check` passed (format-final.log). Fractional resampling shows
uneven checker edges/last-row stripes in the 1.5x pane capture; native rendering
and photographic sharpness acceptance remain pending rather than inferred
from this high-frequency synthetic pattern.

Final integration: all eight CTest targets passed again after lint fixes
(test-final.log, 35.54s); targeted tidy passed including the final checkerboard
loop naming/parentheses cleanup. That last cleanup preserves the fixture pixels.
All required check stages have passing evidence; the aggregate command itself
retains its earlier lint failure in check-final.log rather than being relabeled
as a successful run. T5 and the unknown-dimension runtime fixture remain open.

## Native continuation — 2026-09-23

See [native-preview-acceptance](../native-preview-acceptance/VERIFICATION.md) for
passive evidence, the demonstrated fractional screen/window DPR mismatch and its
Files-local correction. The new matrix includes the original 1.25× scale as well
as 1×/1.5×/2×. T5 remains open; this continuation does not replace historical
records or close the unknown-dimension runtime fixture.

Current closure — 2026-09-23: [single-monitor acceptance](../native-preview-acceptance/SINGLE-MONITOR.md)
passes the approved 1/1.25/1.6/2 matrix and closes T5 locally. Historical evidence
above is unchanged. Unknown-dimension runtime and second-monitor hardware work
remain deferred, not passed.

### Unknown-dimension follow-up — 2026-09-23

The runtime-fixture deferral is now closed by
[synthetic runtime acceptance](../unknown-dimension-acceptance/VERIFICATION.md).
The installed shared provider rejects unavailable dimensions as Damaged before
codec pixel decoding, with no cache publication; cancellation and valid-selection
recovery pass. This supersedes earlier unknown-size bounded-decode expectations,
not historical test results, and adds no format support. Native single-monitor
acceptance stays complete; physical mixed-monitor qualification stays deferred.
