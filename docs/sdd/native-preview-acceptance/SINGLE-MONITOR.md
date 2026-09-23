# Single-monitor acceptance — 2026-09-23

Status: **Passed locally; sharp-previews T5, N5 and N6 complete.**
The separate Files/Viewer outcomes walkthrough also passed. The authorized
umbrella publication checkpoint is tracked in the [integration record](../../../../docs/initiatives/shared-image-outcomes/SINGLE-MONITOR.md).
The user subsequently authorized **“publish and pin”**. The umbrella ledger
records canonical revisions and final integration; measured artifacts stay unchanged.

## Approved scope and provenance

Files `e8efed169bcea51c1ebdc846525a50331a2e298d`, Images
`3633865d2f39e4f163f0159a0f252f88245379f0`, Qt
`863af4183bdf09ce05199b37e8f5dfb46a311ba1`, Config
`fe69a59e6b73167fd5349223a4d265d75386c139`. Product source and providers are
unchanged. GCC 16.2.1, Qt 6.11.2, Ninja, Release; native Wayland/hardware rendering.
Monitor: eDP-1, Sharp LQ180R1JW01, 2560×1600 at 240.001Hz throughout.
All scale, focus, selection, resize, fullscreen and visual judgments were manual.

The user explicitly approved **1/1.25/1.6/2** after Hyprland rejected 1.5× at
this mode and reported actual 1.6×. Integer logical dimensions at 1.6× are
1600×1000. **1.5× is unavailable at this mode and is not passed.** See
[Hyprland's scale rule](https://wiki.hypr.land/Configuring/Basics/Monitors/).
The historical default matrix remains unchanged; this candidate uses explicit scales.
Second-monitor hardware testing is deferred and is not a gate for this iteration.

## Frozen candidate

Evidence root: ignored `build/shared-images-single-monitor-20260923/` in Files.
Historical [older 1.25× results](VERIFICATION.md) remain in
`build/native-preview-acceptance/` and do not count toward this candidate.

- `release/tests/files-native-preview-lab`: fresh Release build, SHA-256
  `7287129c9edfad533a9bc7c239e596e8955954d06e05122c2fa1829a5b22629b`.
- `deps/prefix`: separate verified provider copy; `deps/provider-revisions.tsv`
  identifies exact source revisions. Installed Images archives match both consumers.
- `fixtures`, `timing`, `startup`: two pinned photographic originals and six freshly
  generated controls; `fixtures.json` and `ATTRIBUTION.json` retain hashes/licenses.
- `frozen.json`: original hashes of 173 binary/cache/provider/fixture/tooling files.
- `native-preview-original.py`, `scale-amendment.json`: preserve the original
  runner and the user-authorized runner amendment. Only supported scale selection
  and report requirements changed; observer, timestamps, timing calculations,
  thresholds, captures, binary and providers stayed frozen. Completed rows were
  revalidated under the amended reporter. Each session retains its actual hashes.
- `native/scale-*/`: manifests, raw JSONL, process logs, samples, 18 captures per
  scale, independent matched-size references, comparison galleries and dated
  `user-acceptance.json` reports. Capture metadata stays unmodified; explicit user
  reports supply the subsequent comparison judgments.

Build commands, from Files:

```sh
bash scripts/prepare-deps.sh
cmake -S . -B build/shared-images-single-monitor-20260923/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix" \
  -DQML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml"
cmake --build build/shared-images-single-monitor-20260923/release \
  --target files-native-preview-lab --parallel 4
```

`configure.log`, `build.log`: passed; complete logs reviewed, no compiler warnings.
The current smoke fixture generator passed. The fresh lab's offscreen observer
check produced 34 valid events and correctly rejected them as native acceptance.

The unchanged [operator protocol](README.md) applies with this candidate root,
its copied provider prefix, and the approved scale substitution. Native launches
removed the inherited `QT_AUTO_SCREEN_SCALE_FACTOR`; no synthetic scale override
or software renderer was used. Background builds/tests did not run during timings.

## Completed matrix

| Actual scale | Timing pairs | Separate startups | Visual/captures/reference comparisons | Result |
| --- | --- | --- | --- | --- |
| 1× | 5/5 | 5 per photo | All eight fixtures; all 18 comparisons pass | Pass |
| 1.25× | 5/5 | 5 per photo | All eight fixtures; all 18 comparisons pass | Pass |
| 1.6× | 5/5 | 5 per photo | All eight fixtures; all 18 comparisons pass | Pass |
| 2× | 5/5 | 5 per photo | All eight fixtures; all 18 comparisons pass | Pass |

Each user walkthrough reported no issues with identity, orientation, transparency,
aspect ratio, photographic detail, rapid navigation/resizing, requests above
1024 physical pixels, Quick Look, fullscreen and restoration. Each scale preserves
eight pane, eight Quick Look and two photographic fullscreen captures (72 total).
The user explicitly passed every matched physical-size reference comparison,
including no obscured captures or unacceptable visible loss. Reference generation
alone was not treated as visual acceptance.

The user restored **1.25×** after 2×; read-only `hyprctl -j monitors` confirmed
1.25× at unchanged mode/refresh. No compositor or window interaction was automated.

## Timing evidence

Strict report command, from Files:

```sh
python3 scripts/native-preview.py report \
  --output build/shared-images-single-monitor-20260923/native --scales 1 1.25 1.6 2
```

**Pass:** 20 cold/disk/memory pairs (40 processes), 40 separate startups,
32 groups of five samples, zero missing samples, zero failures in the accepted
matrix, zero threshold misses, one binary/provider combination. The rejected
attempt below is retained separately. Cached publication <100ms, cold/startup
<500ms. Cold means a fresh Files thumbnail cache, not flushed filesystem caches.
Startup begins at main and excludes OS loader overhead. Times describe application
publication, not physical display latency. Raw logs retain metadata, GUI gaps,
RSS, frame counts and decode attempts. Warm phases performed no original decodes.

| Scale | Fixture | Phase | Min / median / max (ms) |
| --- | --- | --- | --- |
| 1× | Colosseum.jpg | cold | 89.919 / 101.490 / 104.052 |
| 1× | Colosseum.jpg | disk | 11.340 / 11.702 / 15.222 |
| 1× | Colosseum.jpg | memory | 2.669 / 2.874 / 3.045 |
| 1× | Colosseum.jpg | startup | 305.636 / 310.610 / 320.661 |
| 1× | Fronalpstock.jpg | cold | 149.438 / 151.593 / 165.593 |
| 1× | Fronalpstock.jpg | disk | 8.888 / 11.724 / 12.171 |
| 1× | Fronalpstock.jpg | memory | 2.260 / 3.060 / 3.217 |
| 1× | Fronalpstock.jpg | startup | 369.119 / 373.675 / 375.233 |
| 1.25× | Colosseum.jpg | cold | 89.914 / 99.608 / 107.845 |
| 1.25× | Colosseum.jpg | disk | 12.644 / 15.173 / 16.786 |
| 1.25× | Colosseum.jpg | memory | 3.058 / 3.140 / 3.644 |
| 1.25× | Colosseum.jpg | startup | 307.340 / 311.652 / 319.518 |
| 1.25× | Fronalpstock.jpg | cold | 147.739 / 151.204 / 160.827 |
| 1.25× | Fronalpstock.jpg | disk | 9.711 / 11.868 / 12.155 |
| 1.25× | Fronalpstock.jpg | memory | 2.764 / 2.815 / 3.115 |
| 1.25× | Fronalpstock.jpg | startup | 361.159 / 376.880 / 418.914 |
| 1.6× | Colosseum.jpg | cold | 82.021 / 89.275 / 105.292 |
| 1.6× | Colosseum.jpg | disk | 11.834 / 13.118 / 16.278 |
| 1.6× | Colosseum.jpg | memory | 2.422 / 3.260 / 3.599 |
| 1.6× | Colosseum.jpg | startup | 306.406 / 332.130 / 332.511 |
| 1.6× | Fronalpstock.jpg | cold | 131.893 / 151.880 / 152.370 |
| 1.6× | Fronalpstock.jpg | disk | 10.347 / 11.797 / 11.923 |
| 1.6× | Fronalpstock.jpg | memory | 1.895 / 2.786 / 3.097 |
| 1.6× | Fronalpstock.jpg | startup | 363.106 / 366.732 / 376.706 |
| 2× | Colosseum.jpg | cold | 187.010 / 188.911 / 192.016 |
| 2× | Colosseum.jpg | disk | 21.889 / 25.389 / 31.599 |
| 2× | Colosseum.jpg | memory | 1.087 / 3.059 / 3.634 |
| 2× | Colosseum.jpg | startup | 303.948 / 308.795 / 317.595 |
| 2× | Fronalpstock.jpg | cold | 222.935 / 233.669 / 239.274 |
| 2× | Fronalpstock.jpg | disk | 7.962 / 21.469 / 25.715 |
| 2× | Fronalpstock.jpg | memory | 1.039 / 2.633 / 3.571 |
| 2× | Fronalpstock.jpg | startup | 371.977 / 379.461 / 385.269 |

## Quick Look

Retained consumer pixels were usable at the opening event for every recorded
opening, with no 200ms usability misses. Adequate upgrades are reported separately;
the usability threshold is not an adequate-resolution deadline.

| Scale | Openings | Usability misses | Adequate upgrade min/max (ms) | Incomplete upgrades |
| --- | --- | --- | --- | --- |
| 1× | 35 | 0 | 0.000 / 506.026 | 7 |
| 1.25× | 26 | 0 | 0.000 / 493.290 | 0 |
| 1.6× | 22 | 0 | 0.000 / 465.451 | 0 |
| 2× | 18 | 0 | 206.395 / 276.885 | 0 |

Seven 1× openings ended before an adequate upgrade; their null completions
remain in the raw evidence. Every fixture also has a completed adequate opening
and stable accepted captures. These brief openings met retained-pixel usability.

All four completed visual logs validate native environment, image identity
retention and no downgrade. User judgments establish physical appearance;
observer snapshots are not an independent pixel-content oracle.

## Retained failures and amendment verification

- `attempts/scale-1/trial-1-no-selections`: the first 1× cold process exited
  normally after only `00-start`; validator rejected the missing photo sequence.
  The user confirmed early closure and requested retry. No timing was accepted,
  no warm process launched. Raw logs and failed manifest remain intact; replacement
  `native/scale-1/trial-1` passed. No threshold miss was discarded.
- An initial fixture preparation command used the wrong relative directory and
  consequently attempted a blocked download before writing an original. Corrected
  paths reused and verified the pinned originals. This was preparation, not a
  native measurement attempt.
- The scale regression failed before the runner extension, then all 17 validator
  tests passed. Explicit 1.6× evidence cannot satisfy the default 1.5× matrix;
  corrupt raw logs, missing/failed trials, native environment and capture guards
  remain tested. Focused CTest `files-native-preview-runner` also passed.
- Historical failed captures/measurements remain in the older record, never mixed
  into this candidate. Intermediate session notes are preserved in ignored
  `qualification-progress-notes.md`.

No application/provider correction was required. The only code change is the
Files-local Python runner extension plus its regression. Publication is authorized
by the subsequent user request; the umbrella coordinator records canonical
availability and clean pins. Clipboard-service qualification, unrelated
release gates, physical second-monitor testing and the unknown-dimension runtime
fixture remain explicitly deferred, with none reported as passed.

Final integration walkthrough: the user reported **“walkthrough passed”**. Its
corrected fixtures, process manifests and retained first-attempt fixture mistake
are documented in the umbrella record. No native matrix result was invalidated.
Automated logs were moved intact into `automation/` under this candidate root;
`acceptance-audit.json` records the successful final evidence/hash audit.

Final documentation review: all changed local Markdown links resolve; whitespace
checks pass in umbrella, Files and Viewer. REUSE licensing passed in all three
repositories after retrying sandbox worker-socket denials outside the sandbox.
No application/provider implementation changes or gitlink updates are present.

## Publication acceptance — 2026-09-23

After the user authorized **“publish and pin”**,
`CMAKE_BUILD_PARALLEL_LEVEL=4 task check` passed as one complete invocation
outside the sandbox: Debug/Release/test builds, 25/25 CTest entries, formatting,
C++/QML lint, REUSE 348/348, staged installation, import policy and QML metadata.
Complete log reviewed at `automation/publication-task-check.log`; no actionable
warnings, only suppressed external-header/NOLINT diagnostics. The prior clean
Release and isolated installed-runtime evidence remains valid because no
application/provider or install-payload changes were made. The frozen measured
lab was not rebuilt or replaced. Final docs/source diff and links pass review.
