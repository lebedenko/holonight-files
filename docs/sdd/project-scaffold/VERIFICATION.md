# Verification results

Verified locally on 2026-09-08, Arch Linux, Qt 6.11.2, GCC 16.2.1,
clang 22.1.8, and REUSE 6.2.0. No sibling or umbrella source changes were made.

| Check | Result | Requirements |
| --- | --- | --- |
| `task deps` | Passed: both sibling providers (holonight-config, holonight-qt) built and installed under build/deps from their working trees; sibling working trees remained clean | R1, R9 |
| `task build` | Passed: debug executable | R1 |
| `task build PRESET=release` | Passed: release executable | R1 |
| `task test` | Passed: 5/5 CTest cases, including 2 GTest smoke cases | R2–R6 |
| `task format-check` | Passed: C++ and QML formatting | R8 |
| `task tidy` | Passed: both C++ translation units, no project diagnostics; external-header diagnostics suppressed by the same policy as holonight-viewer | R8 |
| `task qml-lint` | Passed without diagnostics | R2, R4, R8 |
| `reuse lint` | Passed: 36/36 files with copyright and license information | R7 |
| `task install-check` | Passed: files, desktop validation, version and a 3-second offscreen startup with no error/warning output, using installed provider imports only | R6, R7 |
| `task desktop-check` | Passed: debug and release registration in one isolated XDG data home; generated entries validated, `--version` executed, icon matched, packaged entry unchanged and free of MimeType/`%f` | R7, R8 |
| `task visual-check` | Passed: dark/light captures at 100%, 125% and 150% scale, minimum and initial sizes | R10 |
| `git diff --check` | Passed | R8 |

The smoke test (`Files.WindowAndKeyboard`) loads the production Main.qml,
checks the empty state text and native window flags, checks minimum-size
bounds, sends real f/Escape/q key events, and verifies restoration of normal
or maximized state after leaving fullscreen. `Files.EmbeddedStyleSelection`
clears QT_QUICK_CONTROLS_STYLE, QT_QUICK_CONTROLS_FALLBACK_STYLE and
QT_QUICK_CONTROLS_CONF before application creation, creates a generic
Controls Button, and checks the Holonight style name and its
`foregroundColor` property. CLI tests cover help, version, rejection of any
positional argument, and QML startup failure using an intentionally invalid
provider module (`files-qml-failure`).

Visual inspection of `build/visual/dark-1-large.png` and
`build/visual/light-1.25-small.png` found a centered, readable "No folder
open" empty state and legible bottom hints ("F fullscreen", "Q quit") with no
clipping in either theme or scale. All twelve captures under `build/visual`
are reproducible with `task visual-check`.

Direct runs of `holonight-files --version` and `--help` under
`QT_QPA_PLATFORM=offscreen` produced the expected output and exit 0. A
timeout-bounded run with no arguments produced no error, warning, or
"not found" output before being killed (exit 124), confirming a clean
QML/style startup outside the test harness.

## Deviations from an exact holonight-viewer copy

These are intentional, requirement-driven differences, not omissions:

- No CI execution: no Docker/Podman runtime is available in this environment,
  and the workflow file is untracked pending review. `.github/workflows/build.yml`
  does not yet pin holonight-config/holonight-qt revisions (holonight-viewer's
  current workflow does); pinning specific inspected revisions and adding an
  isolated no-workspace-mount runtime check (`packaging/Dockerfile.runtime-check`)
  are deferred to this project's own release-readiness cycle, matching when
  holonight-viewer itself added them.
- The desktop entry declares no `MimeType` and no `%f` argument, and
  `scripts/register-desktop.py` / `scripts/check-desktop.py` drop the
  `-- %f` suffix and GIO file-substitution exercise that holonight-viewer's
  equivalents added for its later image-opening stage: opening a folder is a
  non-goal of this cycle (R7).
- `apps/files` currently has no header files, so `Taskfile.yml`'s
  format/format-check commands list only `apps/files/*.cpp` (not `*.h`); the
  `*.h` glob should be restored once headers exist.
- Live-compositor inspection of server-side titlebar buttons was not
  performed in this environment (no stacking compositor available); the
  offscreen tests verify native window flags and behavior but not
  compositor-owned decoration pixels. No custom titlebar or frameless flag is
  set. This mirrors holonight-viewer's own unresolved scaffold gap.
- A committed clean-checkout build (configuring and testing straight from a
  fresh `git clone` of this commit) was not separately re-verified after the
  final desktop-file edit; the working-tree build/test/check sequence above
  was re-run in full after that edit and passed.

The scaffold is implemented and all locally-runnable automated checks pass.
CI execution and live-compositor decoration inspection remain open, as noted
above.
