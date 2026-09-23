# Verification — 2026-09-23

Baseline `06c42f03534cdf57b2fb09f73bbfa69f029d0f9c`; all changes remain local and
uncommitted. Evidence root: `build/native-preview-acceptance/` (ignored).
Native rows remain open until the complete matrix and manual observations pass.

## Environment and provenance

Read-only `hyprctl -j monitors`: eDP-1, Sharp LQ180R1JW01, 2560×1600 at
240.001 Hz, initial compositor scale **1.25×**. No automated input, focus, pointer
movement, fullscreen or compositor-setting changes were used. The inherited
`QT_AUTO_SCREEN_SCALE_FACTOR=1` is explicitly removed for native lab launches.
Native manifests record binary/source/provider artifact hashes, build cache,
provider revisions, fixture hashes, real before/after monitor settings and logs.
Qt 6.11.2, C++23, GCC 16.2.1 20260810 (`/usr/bin/c++`), Release lab, native Wayland/OpenGL.

Reused Release provider prefix `build/deps/prefix`; `task deps` confirmed revisions:

| Provider | Revision |
| --- | --- |
| holonight-config | fe69a59e6b73167fd5349223a4d265d75386c139 |
| holonight-qt | 863af4183bdf09ce05199b37e8f5dfb46a311ba1 |
| holonight-images | 3633865d2f39e4f163f0159a0f252f88245379f0 |

## Demonstrated defect and correction

The first native diagnostic attempt (`native/scale-1.25/trial-1/cold/events.jsonl`)
recorded window DPR 1.25 while Files requested pane width 616px for 308 logical
pixels, and Quick Look requests likewise used 2×. Both consumers bound to
`Screen.devicePixelRatio`, which differs from the fractional window surface DPR.
The Files-local correction binds to the owning window's Qt 6.11
`devicePixelRatio` property and tracks its notifications. Providers are unchanged.
Qt documents this distinction in [QScreen::devicePixelRatio](https://doc.qt.io/qt-6/qscreen.html#devicePixelRatio-prop).

`PreviewConsumers.UseWindowDprWhenScreenDprDiffersAndTrackChanges` uses a redirected
Quick window with controlled effective DPR and a platform DPR-change event,
leaving the screen DPR independent. It exercises real consumer QML and the
existing service test seam. It fails with the old screen binding and passes at
1.25/1.5/2/1 with the correction (`dpr-regression-before.log`,
`dpr-regression-after.log`). Earlier harness-development failures remain in the
artifact directory; they are not acceptance evidence.

The first attempt also exercised Quick Look/fullscreen in a timing session and
had observer events after the end marker. It is **failed**, retained as diagnostic
evidence only. Observer teardown logging now stops after its end marker.

Candidate cold trial 1 at 1.25× shows corrected 385px pane requests and 512px tiers.
Its initial validator rejected normal logical-height rounding (138px for the
panorama) after aspect fit. The parser now permits one logical pixel of frame
rounding propagated through the source aspect ratio, with a deterministic test.
The complete cold evidence was revalidated; `manifest-before-resume.json`
preserves the original rejection, and the final manifest records revalidation
parser hash and previous failure. Only the warm process is resumed against that
same populated cache; no cold measurements are discarded or resampled.

## Automated checks

| Check | Evidence | Result |
| --- | --- | --- |
| Existing fixture generator, six 6000×4000 controls incl. EXIF 2/6/7 | generation.log, fixtures.json | Pass |
| Both original-file version and SHA-256 pins | photos.json, downloaded originals | Pass |
| Clean final Release lab configure/build | final-release-configure.log, final-release-build.log | Pass; complete logs reviewed, no compiler warnings |
| Initial focused preview regressions | focused.log | 53/53 pass |
| DPR regression red/green | dpr-regression-before.log / dpr-regression-after.log | Expected fail / pass |
| Evidence parser rejection cases, complete matrix revalidation, environment guards and capture-state regression | scripts/check-native-preview.py | 16 tests pass |
| Real entry-point observer offscreen | final-release-observer.log; observer-final-tests.log | Pass; 34 valid events; explicitly rejected as native |
| task check | check.log, check-final.log | Initial sandbox socket/GPU failures; final run passed builds, all 25 CTest targets and formatting, then stopped at four observer lint findings; completed below |
| Failed GPU checks outside sandbox | ctest-permissions.log; superseded by check-final.log | Six GPU checks pass; early smoke harness failure corrected before all 25 targets passed |
| Full C++ lint after explicit nullptr checks | tidy-final.log; tidy-observer-final.log | Pass; all 97 translation units plus targeted final observer check |
| Remaining task check stages | format-final.log, qml-lint-final.log, license-final.log, install-final.log, import-final.log, qmltypes-final.log | All pass; REUSE 338/338 files, staged installed payload and QML metadata/imports verified |
| Installed-runtime acceptance after consumer QML correction | runtime-final.log, `task isolated-runtime-check` | Pass; disposable network-isolated container, installed desktop launch |

The offscreen observer and automated scales do not establish native detail,
compositor transitions, physical display latency or manual acceptance.

## Native acceptance matrix

Each row requires both photographs, six controls, pane/Quick Look/fullscreen
captures and annotations, and five complete cold/disk/memory samples per photo.
Startup measurements are separate fresh processes, excluded from navigation.
Threshold misses remain visible, even when event validation succeeds.

| Actual scale | Five timing pairs | Startup samples | Detail/orientation/transparency | Resize/>1024/Quick Look/fullscreen/restore | Captures/references | Result |
| --- | --- | --- | --- | --- | --- | --- |
| 1× | Pending | Pending | Pending | Pending | Pending | Open |
| 1.25× | 5/5 pairs pass | 5/photo pass | User reports no issues; reference comparison pending | User reports no issues; captured evidence in progress | Pane and Quick Look saved; fullscreen pending | Open |
| 1.5× | Pending | Pending | Pending | Pending | Pending | Open |
| 2× | Pending | Pending | Pending | Pending | Pending | Open |

Cold trial 1 application publication: Colosseum 100.156ms, Fronalpstock 162.667ms
(both <500ms). These are individual observations, not five-sample distributions.
No native visual pass is inferred from observer snapshots. T5 remains unchecked.
Read-only monitor inspection at handoff confirms the original **1.25×** scale
is unchanged; no compositor-setting mutation was performed.


### Completed 1.25× measurements

All five pairs and ten separate startup processes validate against their raw logs
under `native-candidate/scale-1.25`. No threshold misses or original decodes in
warm phases. Bounds match across each pair (385×300 request; 308px logical pane
width). Startup is timed from entry to main, excluding OS loader overhead.

| Fixture | Phase | Min / median / max (ms) | Samples | Misses |
| --- | --- | --- | --- | --- |
| Colosseum.jpg | startup | 308.088 / 324.564 / 335.320 | 5 | 0 |
| Fronalpstock.jpg | startup | 365.244 / 370.224 / 394.079 | 5 | 0 |
| Colosseum.jpg | cold | 93.003 / 96.228 / 100.156 | 5 | 0 |
| Fronalpstock.jpg | cold | 134.094 / 152.126 / 162.667 | 5 | 0 |
| Colosseum.jpg | disk | 19.286 / 23.974 / 25.909 | 5 | 0 |
| Fronalpstock.jpg | disk | 11.635 / 11.871 / 11.958 | 5 | 0 |
| Colosseum.jpg | memory | 2.571 / 2.922 / 3.159 | 5 | 0 |
| Fronalpstock.jpg | memory | 2.286 / 2.761 / 3.711 | 5 | 0 |

User's 1.25× walkthrough report: “No any issues observed, worked well in any
conditions.” This answers the requested eight-fixture pane/Quick Look,
orientation/aspect/transparency/detail and resize/fullscreen/restore walkthrough.
It precedes matched-reference inspection, which remains a separate pending gate.
The separate visual log validates identity retention/no downgrade and records
18 Quick Look openings with retained consumer pixels immediately available.
Two openings were closed before an adequate upgrade; their null completion
values are preserved. Every fixture has another completed opening. These events
are application-level usability/publication, not physical display latency.
Background local build/static analysis ran during this separate visual session,
not during the completed timing/startup samples.


## Final handoff and intentionally pending rows

The user explicitly requested: **“Leave the remaining native rows pending.”**
No further interactive testing was launched after that request. The lab windows
were closed by the user and their processes exited successfully.

- Native 1×/1.5×/2×: all timing, startup and visual rows remain pending.
- Native 1.25×: timings/startup pass; user reports no visible issues across the
  eight-fixture walkthrough. Fronalpstock pane and Quick Look window PNGs,
  unscaled crops and independent references are saved under
  `native-candidate/scale-1.25/captures/` with geometry, snapshots and hashes.
- Fullscreen capture: **pending**. The delayed attempt rejected the state and
  the watcher then timed out because it incorrectly required Quick Look together
  with fullscreen. The native log confirms valid fullscreen-pane transitions;
  this was a capture-helper mistake, not a demonstrated Files defect. The helper
  now accepts either fullscreen pane or fullscreen Quick Look, with a regression
  test. No successful fullscreen capture is claimed after this correction.
- Colosseum captures and manual matched-reference comparison remain pending.
  Sixteen independent matched-size pane/Quick Look references for all eight
  fixtures were generated in `native-candidate/scale-1.25/reference-gallery/`;
  generation is not user visual acceptance.
- `native-preview.py report` revalidates raw logs and returns failure for the
  incomplete matrix as intended: 15 missing timing pairs and 30 missing startup
  samples, no failed validated candidate trials and no 1.25× threshold misses.
  The earlier failed diagnostic attempt remains separate under `native/`.

The measured Release lab is preserved at `release/tests/files-native-preview-lab`.
The final clean `final-release/` build includes equivalent explicit nullptr
comparisons needed by lint in the observer. Existing native evidence is reused
for the unchanged production QML and observer behavior; it is not relabeled as
measurement of the final binary. Each manifest retains its actual binary hash.

The aggregate `task check` log retains its earlier lint failure; the full lint
rerun and remaining stages provide passing completion evidence, rather than
claiming the aggregate invocation itself succeeded. Installed-runtime acceptance
passed separately. No application/provider/umbrella publication, CI query,
submodule pin update or commit was performed. T5 and unrelated pending tasks
remain open.

## Current-build continuation — 2026-09-23

The new approved single-monitor plan resumes qualification at Files `e8efed1`.
See [current candidate and protocol](SINGLE-MONITOR.md). All four scales require
new measurements of one frozen binary; historical 1.25× results above are preserved
and do not fill the new matrix. Physical second-monitor testing awaits hardware,
does not block N5/N6 or T5, and is not passed. The subsequent user request
authorizes publication and pinning; the umbrella ledger owns that checkpoint.

Current closure — 2026-09-23: the [new candidate](SINGLE-MONITOR.md) passes the
approved 1/1.25/1.6/2 matrix; N5/N6 and sharp-preview T5 are complete locally.
The historical pending rows above are preserved and do not describe current status.
