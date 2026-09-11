# Backlog

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
  filename sort, size/mtime columns), `PlacesModel` (fixed standard places only), `FileWatcher` for live updates.
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
