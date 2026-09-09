# Backlog

Each numbered stage is a separate SDD cycle: approve requirements, approve
design/tasks, implement, verify, document. Do not mark a stage complete while
acceptance checks remain pending.

| Stage | Deliverable | Acceptance gate |
| --- | --- | --- |
| 0. Project scaffold | Themed native-decoration shell, CLI, packaging, CI, checks | Clean-checkout build and installed launch; all scaffold checks closed |
| 1. Browse a folder | Directory model, sortable listing, places sidebar, keyboard navigation | Responsive large directories; natural filename order; unreadable/missing entries handled |
| 2. Inspect a selection | Metadata/EXIF preview pane, quick look | Predictable resize/fullscreen behavior, fractional scaling |
| 3. Modal command mode | NORMAL mode, `:`-prefixed command palette, visual/multi-selection | Discoverable, composable commands; escape returns to NORMAL |
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
