# Native preview acceptance lab

Status: **current single-monitor matrix passed locally; sharp-previews T5 is complete.**
See the [current acceptance report](SINGLE-MONITOR.md) for the user-approved
1/1.25/1.6/2 matrix and exact candidate. Historical 1.25× measurements remain
separate in [verification](VERIFICATION.md). The protocol below retains the original
1/1.25/1.5/2 default; use `--scales 1 1.25 1.6 2` when reporting this candidate.
See also [requirements](SPEC.md), [design](DESIGN.md), and [tasks](TASKS.md).
Original tooling baseline: `06c42f03534cdf57b2fb09f73bbfa69f029d0f9c`.

The user performs all scale, activation, navigation, resize, fullscreen and visual
inspection actions. Do not run historical native tests that drive focus/input.
The observer is passive and does not install a diagnostic interface into hn-files.
All times describe application publication, not physical display latency.

## Build and fixtures

From the Files root:

```sh
task deps
cmake -S . -B build/native-preview-acceptance/release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix" \
  -DQML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml"
cmake --build build/native-preview-acceptance/release --target files-native-preview-lab -j 4
python3 scripts/prepare-native-preview.py --output build/native-preview-acceptance \
  --smoke-binary build/test/tests/files-smoke --provider-prefix build/deps/prefix
```

The fixture generator requires the existing test binary (`task test` builds it).
It reuses PreviewPerformance.GenerateFixtures: PNG 0/1/2 (transparency and distinct
quadrants), JPEG 3/4/5 (EXIF 2/6/7). `fixtures.json` records generated hashes;
`photos.json` pins original photo versions, SHA-256, author and licenses. Downloads,
captures, caches and references stay under ignored `build/`. Photos are unchanged;
reference derivatives are auto-oriented/resized and retain the original attribution.
Fronalpstock: Hannes Röst, CC BY-SA 3.0. Colosseum: DAVID ILIFF, CC BY-SA 3.0
(the author's attribution permission on the linked Commons page permits this).
Source and license links are in [photos.json](photos.json).

## Manual timing protocol

For each actual compositor scale **1, 1.25, 1.5, 2**, manually change Hyprland's
monitor scale. Preserve the same output, display mode and refresh rate. Keep the
window on that output. Do not set QT_SCALE_FACTOR, a software backend, or other
Qt scaling overrides. Use native Wayland. The runner reads monitor settings and
rejects mismatched Qt DPR, scale or rendering backend. Its child inherits the
real XDG_RUNTIME_DIR/Wayland socket, while XDG config/data/state/cache are isolated.
It never deletes user caches or flushes host filesystem caches. “Cold” means a
fresh Files thumbnail cache; it does not mean cold kernel page cache.

Run trial 1 (replace scale and output path as appropriate), then trials 2–5:

```sh
python3 scripts/native-preview.py run \
  --binary build/native-preview-acceptance/release/tests/files-native-preview-lab \
  --provider-prefix build/deps/prefix \
  --fixtures build/native-preview-acceptance/timing \
  --output build/native-preview-acceptance/native/scale-1.25/trial-1 \
  --expected-scale 1.25 --trial 1
```

1. The cold process opens with the `00-start` sentinel directory selected. Activate
   it manually. Set the desired normal-window/pane geometry **before selecting a
   photograph**. Use the same geometry for both processes. Avoid huge pane sizes
   in timing trials: their fitted image must fit a disk tier (<=1024px).
2. Select **Colosseum.jpg**, wait for pixels and metadata, then **Fronalpstock.jpg**,
   wait again. Close normally. No other selections, resizing, Quick Look or captures
   belong in this timing session. The sentinel is excluded from navigation times.
3. A fresh process launches using the populated pair cache. Activate and restore
   exactly the same pane/window geometry before selecting photos. Select
   **Colosseum, Fronalpstock, Colosseum, Fronalpstock**, waiting each time.
   The first two selections test disk reuse, the second two memory reuse within
   the two-entry capacity. Close normally.
4. Inspect `manifest.json`, `samples.json` and both process logs. A failed session
   remains failed; use a new output directory to rerun and retain the failed
   attempt. Do not omit failures from the handoff.
   After a parser-only correction, `--resume-disk` can revalidate a complete cold
   log and launch the missing warm process using its unchanged cache. It checks
   binary/provider/fixture hashes, preserves `manifest-before-resume.json`, and
   records the prior rejection and the revalidation parser hash. Failed processes,
   incomplete cold logs or existing warm attempts cannot be resumed this way. The strict aggregator reports
   duplicates, so keep retries in a separate attempts directory and document the
   reason and replacement link in VERIFICATION.md.
5. Run a separate cold **startup** process per photograph, scale and trial with
   `--mode startup --fixtures build/native-preview-acceptance/startup/Fronalpstock`
   (or `Colosseum`), and a new `startup-<photo>-<trial>` output path alongside the timing-pair directories. Do not
   navigate or resize it. Close after preview and metadata finish. Startup is timed
   from entry to main, before QGuiApplication; it excludes OS exec/loader overhead.
   No startup selection is counted as a navigation sample.

The strict report requires five pair samples per photo/phase/scale and five
separate startup samples per photo/scale. Keep both beneath its output root:

```sh
python3 scripts/native-preview.py report --output build/native-preview-acceptance/native
```

Data validity is separate from acceptance: every threshold miss is retained.
Targets are cached <100ms, uncached <500ms and startup <500ms. Report median/min/max
per fixture, phase and scale, including every miss. Raw JSONL records metadata
completion, timer gaps, sampled RSS/kernel peak RSS and Qt frame counts. Observer
and file logging overhead is included. Frame counts are not display timestamps.

## Manual visual protocol (separate from timed trials)

Use `run --mode visual` with `--fixtures build/native-preview-acceptance/fixtures`,
a fresh output path, scale and trial number. Inspect **both photos and all six
controls**, in the pane and Quick Look. For every scale record dated observations:

- Fine landscape/building detail, correct identity, orientation, aspect ratio and
  transparency, compared to independent references at matching physical bounds.
- Narrow/wide panes and rapid splitter changes; selections immediately clear old
  pixels; same-image upgrades retain pixels and completed upgrades never downgrade.
- Quick Look opening (retained pixels may be usable before the larger upgrade),
  requests exceeding 1024 physical pixels, fullscreen entry/exit, and restoration
  to the small pane. Record usability <200ms separately from adequate upgrade.
- Rapid navigation ends at the correct identity; no stale final image. Record any
  provider/compositor defect with reproduction and leave the row open.

Quick Look open/usability/upgrade observations are in `result.json`; a retained
consumer image is only application-level usability evidence. Use manual inspection
for detail/physical presentation. Snapshots report current QImage cache identity,
not an independent pixel-content oracle. The six quadrant/orientation controls
and reference comparisons supply that independent visual check.

For captures, arm the passive helper before switching to Files; it waits for the
requested state, so the operator does not have to reply from fullscreen:

```sh
python3 scripts/capture-native-preview.py --wait-for-state 60 \
  --binary build/native-preview-acceptance/release/tests/files-native-preview-lab \
  --events <visual-session>/visual/events.jsonl --output <new-capture-directory> \
  --state fullscreen --fixture Fronalpstock.jpg
```

The user then activates Files and enters fullscreen. The helper only observes,
reads explicit compositor geometry and captures it; fullscreen accepts either
pane or Quick Look. It also preserves an unscaled consumer crop, creates the
matched-size ImageMagick reference and records hashes/geometry. Inspect the
capture for occlusion before accepting it. No mouse, focus or input is generated.
A timeout is recorded as failure. `--delay SECONDS` is also available.

Alternatively, after positioning the window manually, read `hyprctl -j clients` and choose its
explicit geometry. Capture stable pane, Quick Look and fullscreen states with
`grim -g 'X,Y WIDTHxHEIGHT' capture.png` (never `slurp`, focus changes or input
injection). Preserve the original PNG and record geometry, monitor settings,
SHA-256, matching event timestamp, fixture, requested/decoded bounds and result.
Keep unscaled crops for comparison; record their exact physical crop rectangles.
A screenshot establishes rendered pixels, not physical display latency.

Generate independent matched-size references (physical image/consumer bounds from
the stable snapshot, not the whole window):

```sh
python3 scripts/prepare-native-preview.py --reference build/native-preview-acceptance/fixtures/Fronalpstock.jpg \
  --width 1200 --height 600 --output build/native-preview-acceptance/references/scale-1.25-pane
```

Repeat for each fixture and each captured display size. The command records
ImageMagick version, exact arguments and input/output hashes; it auto-orients before
aspect-fitting. Inspect references at 1:1 physical pixels, manually. Different
resampling implementations are not expected to be pixel-identical. Record visible
loss rather than asserting cross-renderer pixel equality.

Restore the original **1.25×** compositor scale after the matrix. T5 may close only
when all four scales, timing samples, startup checks and manual observations pass.
Missing captures/annotations or unresolved failures leave their rows pending.


## Preserved handoff artifacts

`build/native-preview-acceptance/release/` contains the measured Release lab;
`final-release/` is the clean final-source build after equivalent explicit null
comparisons required by lint in the observer. Production QML/behavior is identical.
The original measured binary and its native manifests are preserved. Do not
silently combine different binaries in a timing matrix: the report rejects mixed
binary/provider signatures. Read VERIFICATION.md before resuming native work.
The 1.25× reference gallery is generated, but user comparison remains pending.
