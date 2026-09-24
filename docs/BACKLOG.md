# Backlog

The [shared image outcomes cycle](sdd/shared-image-outcomes/SPEC.md) preserves raster categories
and quiet metadata status. Local automated verification is recorded in its
[verification report](sdd/shared-image-outcomes/VERIFICATION.md); local automated/native acceptance passed, including T5. The umbrella ledger
records the authorized publication and integration checkpoint for the runner extension and records.

Each numbered stage is a separate SDD cycle: approve requirements, approve
design/tasks, implement, verify, document. Do not mark a stage complete while
acceptance checks remain pending.

| Stage | Deliverable | Acceptance gate |
| --- | --- | --- |
| 0. Project scaffold | Themed native-decoration shell, CLI, packaging, CI, checks | Clean-checkout build and installed launch; all scaffold checks closed |
| 1. Browse a folder | Directory model, sortable listing, places sidebar, keyboard navigation | Responsive large directories; natural filename order; unreadable/missing entries handled |
| 2. Inspect a selection | Metadata/EXIF preview pane, quick look | Predictable resize/fullscreen behavior, fractional scaling |
| 3. Modal editing | NORMAL/VISUAL/SEARCH/INSERT; inline synchronous mkdir/touch/rename | Stable editing, safe creation, literal search highlighting; native acceptance pending |
| 4. File operations | mkdir, touch, rename, delete (with trash), copy/move | Asynchronous operations; confirmations and recoverable failures |
| 5. Release readiness | Desktop integration, accessibility, performance, packaging | Packaged workflow on supported desktops without development paths |

Stage 0 is the approved [scaffold cycle](sdd/project-scaffold/SPEC.md); see its
[verification record](sdd/project-scaffold/VERIFICATION.md) for evidence and
remaining acceptance checks.

Keep loading/model state in C++, presentation/input feedback in QML, and
introduce the directory/file model at stage 1. Maintain fixtures for empty,
large, permission-denied, and Unicode-named directories. Check responsiveness,
appearance, scaling, keyboard use and installed runtime at each relevant stage.

Adaptive CSD/SSD remains a shared holonight-qt initiative and must not block
core browsing. No compositor-name heuristics or Files-specific decoration
policy. Image editing, albums, tagging, photo databases, and a standalone image
viewer are outside this roadmap — see the sibling HoloNight Viewer.

## v1 scope (stages 1-4)

v1 is stages 1-4: browse, inspect, modal commands, file operations, on the
local filesystem only. No SFTP/SMB/MTP, archive-as-directory, plugins,
embedded terminal, Git UI, or multi-register (`"a`) / mark (`ma`) vim features
— those are v2+ if ever. Each stage still runs its own SDD cycle (idea grill,
EARS requirements, design, tasks, implementation, verification); this section
seeds that work, it does not replace it.

Layering across stages 1-4:

```text
UI (QML)
Navigation:  DirectoryController · SelectionController · VimModeController · HistoryController
Models:      DirectoryModel · PlacesModel · SearchModel (in-process FuzzyMatcher)
Filesystem:  FileOperationService · MimeService · ThumbnailService (freedesktop-compat) · FileWatcher · MountService
Tasks:       TaskManager (progress · cancellation · conflicts · queue)
Preview:     ImagePreview · TextPreview · GenericMetadataPreview
```

`TaskManager` is required from stage 4 onward, not bolted on later: recursive
copy/move, name collisions, permission errors, cancellation, and progress are
the hard part, not listing filenames. `FuzzyMatcher` and `ThumbnailService`
start in-tree; only extract them into a shared HoloNight library once a second
consuming application (e.g. holonight-viewer, a future launcher) needs them.

- **Stage 1 — Browse a folder**: `DirectoryModel` (async populate, natural
  filename sort, size/mtime columns), `PlacesModel` (standard places and optional startup `~/Projects`), `FileWatcher` for live updates.
  NORMAL-mode movement only: `j/k`, `gg`/`G`, count-prefixed motions (`5j`),
  `h` parent, `l`/Enter open, sort toggles, hidden-files toggle. Fixtures:
  empty, large (10k+ entries), permission-denied, Unicode-named directories.
- **Stage 2 — Inspect a selection** (implemented; review remediation and native
  acceptance recorded in [verification](sdd/inspect-selection/VERIFICATION.md)): metadata/EXIF preview pane and a Quick
  Look overlay (`Space`) sharing the same preview backends — image and text
  first; PDF/video/audio thumbnailing deferred. `ThumbnailService` v1 covers
  images only (already being decoded), reading/writing the freedesktop
  Thumbnail Managing Standard cache (`$XDG_CACHE_HOME/thumbnails/`,
  MD5-of-URI keyed, mtime/size-validated) instead of a Files-private cache.
  The [info sidebar redesign verification](sdd/info-sidebar-redesign/VERIFICATION.md)
  tracks the mockup-aligned metadata tables, short-window scrolling, and pending native-display
  and real-camera acceptance.
- **Stage 3 — Modal editing**: `VimModeController` implements NORMAL/VISUAL/SEARCH/INSERT,
  inline rename/create and synchronous mkdir/touch; `/` fuzzy jump with literal match
  highlighting and NORMAL `n`/`N` repetition; `v`/`V` range selection without filesystem
  consumers. COMMAND mode and the `:` palette are explicitly deferred to a future
  approved cycle. See [verification](sdd/vim-modal-editing/VERIFICATION.md) for checks
  and pending native acceptance.
- **Stage 4 — File operations** (safety remediation implemented; acceptance tracked in
  [verification](sdd/file-operations/VERIFICATION.md)): `FileOperationService`
  (safe copy/move and validated trash) driven entirely through `TaskManager` —
  asynchronous, cancellable, progress-reporting, conflict-resolving from the
  start. `yy`/`dd`/`p`/`D` over a single active clipboard register. Full SDD
  docs at `docs/sdd/file-operations/{SPEC,DESIGN,TASKS,VERIFICATION}.md`.
  Mkdir/touch/rename remain Stage 3's synchronous implementation, unchanged.

Stage 5 (release readiness) follows once stages 1-4 land; its scope is
packaging/accessibility/performance hardening, not new v1 features.

Stage 4 safety revision removes permanent deletion, retains complete sources on
incomplete moves, and captures prompts ahead of editors. Remaining combined E2E,
performance and native acceptance checks stay open in its verification record.


System-maintenance hardening covers ordered uninstall failure handling, installed
permissions and desktop startup observation, and CI maintenance checks. See the
[current verification record](sdd/system-maintenance/VERIFICATION.md) for local
evidence and pending hosted CI, real-host install/uninstall, and native acceptance.
Development desktop-registration tasks and their generated launchers are retired;
installed desktop validation remains part of maintenance checks. This work adds no
application features.

Application-window layout review fixes preserve filenames in narrow listing panes
and align breadcrumb/header text with the data columns. See the
[layout verification record](sdd/app-window-layout/VERIFICATION.md). The existing
420px sidebar/preview width conflict and native visual acceptance remain pending.

Navigation history is complete: Ctrl+O/Ctrl+I and header buttons traverse an
in-memory jump list and restore the cursor on return. See the
[navigation history verification record](sdd/navigation-history/VERIFICATION.md).

Sharp previews use on-demand freedesktop 128/256/512/1024px tiers, adequate
memory/disk reuse and direct decoding above 1024px. Native scaling and latency
acceptance remain tracked in [verification](sdd/sharp-previews/VERIFICATION.md).
Broader reuse of other applications' thumbnails remains deferred; strict Files
revision validation is retained. No eager cache migration or deletion is needed.

The compact Places sidebar adds themed icons, keyboard activation and optional
`~/Projects`; acceptance evidence is in [Places verification](sdd/places/VERIFICATION.md).
Bookmark management, Trash, Devices and Network sidebar sections remain deferred.

Places sources now read XDG user directories and manually maintained bookmarks.
Async delivery review fixes and pending native acceptance are tracked in the
[Places sources verification](sdd/places-sources/VERIFICATION.md). The fixed
`~/Projects` fallback is replaced by `XDG_PROJECTS_DIR` or an explicit bookmark;
in-app bookmark management remains deferred.

Build parallelism uses CMake/Ninja automatic defaults with explicit compiler-job
overrides; provider and quality-check stages remain sequential. See the
[build parallelism verification](sdd/automatic-parallelism/VERIFICATION.md).

Executable and folder-handler integration introduces `hn-files` and registers
`inode/directory` without selecting a default application. This approved cycle
supersedes the scaffold restriction on folder-handler registration; historical
scaffold records remain unchanged. Migration uses `task uninstall` then
`task install`. See [verification](sdd/folder-handler/VERIFICATION.md) for
automated evidence and pending host chooser and native folder-display checks.

Quick Look now uses a centered, captioned preview card and retains the previous
layout while loading, refitting it when the window resizes. Review remediation
and pending native 1.5× rendering/latency acceptance are tracked in
[Quick Look verification](sdd/quick-look-redesign/VERIFICATION.md).

Quick Look is now a pinned text/image viewer: `Space` opens it only for images and
`text/plain`, `j`/`k`/arrows move a current line in a 100 KiB-capped line viewer, and the
previewed file no longer follows the listing cursor. This supersedes the live-update-on-`j`/`k`
behavior recorded in the two Quick Look verification documents above. Spec, design and
tasks: [quick-look-text-viewer](sdd/quick-look-text-viewer/SPEC.md). The user confirmed native
wheel/drag scrolling, focus restoration, copy behavior and responsiveness on 2026-09-21
with no issues. Isolated runtime verification remains pending before publication.

Quick Look geometry, classification and decode-size coordination now belong to a
C++ presentation model. QML retains measurement, styling, captions and input
handling. Automated evidence and pending native 1.5× acceptance are tracked in
[C++ presentation verification](sdd/quick-look-cpp-presentation/VERIFICATION.md).

Standalone JavaScript helpers have moved to C++ singletons without changing browsing
behavior. Automated checks and pending native visual acceptance are tracked in
[helper migration verification](sdd/javascript-to-cpp/VERIFICATION.md).

Application configuration review fixes preserve pending location tracking across refresh and
navigation, and save after shutdown workers finish. Checks are recorded in the
[configuration verification record](sdd/app-configuration/VERIFICATION.md).

The footer mode badge identifies NORMAL/VISUAL/SEARCH/INSERT without taking focus.
Native visual and accessibility acceptance remain pending in the
[mode badge verification record](sdd/mode-status-badge/VERIFICATION.md).

Inline editing now avoids echoing controller text updates back into its binding.
The reproduced warning and regression evidence are recorded in
[modal editing verification](sdd/vim-modal-editing/VERIFICATION.md).

Image orientation correction is implemented with EXIF 1–8 coverage and lazy thumbnail
migration. Acceptance and manual comparison status: [verification](sdd/image-orientation/VERIFICATION.md).

## Preview performance instrumentation

The [preview performance cycle](sdd/preview-performance/README.md) adds reproducible,
opt-in offscreen cache/resize/selection measurements. Its
[verification record](sdd/preview-performance/VERIFICATION.md) tracks acceptance and
findings. Native sharp-preview acceptance is separately recorded in the [completed current matrix](sdd/native-preview-acceptance/SINGLE-MONITOR.md).

Native sharp-preview T5 now has a [passive acceptance lab](sdd/native-preview-acceptance/README.md)
and [dated results](sdd/native-preview-acceptance/VERIFICATION.md). Native 1.25×
diagnostics demonstrated screen/window DPR mismatch; Files consumers now use
window DPR, protected by an offscreen binding regression. The [current single-monitor candidate](sdd/native-preview-acceptance/SINGLE-MONITOR.md)
tracks a new coherent four-scale matrix; historical measurements are not mixed
with current-build results. Physical second-monitor testing awaits hardware and
does not block this iteration.

## Preview cancellation

Files now propagates each preview request's cancellation token through thumbnail
inspection, decoding and cache publication. Deterministic regressions cover cache
boundaries, request replacement, resize and shutdown; cancelled writes preserve
existing entries. Automated acceptance and the five-process baseline/candidate
comparison are recorded in [verification](sdd/preview-cancellation/VERIFICATION.md).
Cancellation remains cooperative. Native sharp-preview T5 is complete in the
[current matrix](sdd/native-preview-acceptance/SINGLE-MONITOR.md); physical second-monitor
qualification and other deferred gates remain open.

Current single-monitor qualification is complete: T5 passed at actual
1/1.25/1.6/2 with 20 pairs, 40 startups and 72 accepted reference comparisons.
1.5× was unavailable at the preserved mode; the user approved actual 1.6×.
The user authorized publication and pinning; the umbrella ledger records the final checkpoint.

## Shared SVG previews

Implementation and focused coverage are in place for explicit self-contained SVG previews, vector-aware DPR sizing,
resource validation and policy-marked thumbnails. Full and native acceptance status is tracked in
[shared SVG tasks](sdd/shared-svg-support/TASKS.md).
