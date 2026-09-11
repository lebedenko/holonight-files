# System installation and maintenance requirements

Specification-Driven Development cycle for install/uninstall lifecycle hardening and proof that the
installed package carries no development paths.

## Scope

This cycle covers the install/uninstall lifecycle for the v1-complete HoloNight Files application on
Arch Linux with labwc compositor. It does **not** introduce new application features, accessibility
improvements, or performance work — those belong to the separate release-readiness cycle. No distro
packages, Flatpak, or AppImage; only hardening of the existing CMake install workflow and verification
through a disposable container.

## Requirements

### Task interface and build integration

**REQ-F-001**: When `task run` is invoked, Files shall build the selected preset and forward CLI
arguments without registering desktop files in the user's XDG data directory (development builds remain
confined to the build directory).

*EARS template: Ubiquitous*

*Acceptance:* Desktop database is not modified; XDG_DATA_HOME remains unchanged after `task run`.

---

**REQ-F-002**: When `task install` is invoked, Files shall configure and build Release preset in
`build/system-install` against installed `/usr` providers and `/usr/lib/qt6/qml` (not the development
dependency prefix), install all payloads to `/usr` with sudo, and refresh the desktop database.

*EARS template: Event-driven (triggered when `task install` is called)*

*Acceptance:* Executable, desktop entry, icon, and license files are present under `/usr` after install;
build failures prevent installation; `sudo` is prompted; desktop database refresh runs after successful
install.

---

**REQ-F-003**: When `task install` fails during configure or build, no installation shall occur and the
failure shall be reported.

*EARS template: Conditional (if build fails, then halt)*

*Acceptance:* No files are installed when CMake configure or build fails; task exits with non-zero status.

---

**REQ-F-004**: When `task uninstall` is invoked, Files shall remove the installed executable, desktop
entry, icon, and both license files from `/usr`, idempotently refresh the desktop database, and preserve
unrelated files and user configuration directories.

*EARS template: Event-driven (triggered when `task uninstall` is called)*

*Acceptance:* All Files payloads are removed; absent files do not cause error; uninstall succeeds on
second invocation; provider files and user data remain untouched; desktop database is updated.

---

**REQ-F-005**: When removal of a Files payload file fails during uninstall, subsequent steps shall not
run and the failure shall propagate to the caller.

*EARS template: Unwanted-behaviour (prevent partial uninstall on failure)*

*Acceptance:* If file removal fails, directory removal and desktop database refresh are skipped; task
exits with non-zero status; remaining payload files are not removed.

---

**REQ-F-006**: When DESTDIR is supplied to the uninstall helper, it shall operate on that staged `/usr`
tree instead of the real `/usr`, allowing verification in disposable build environments without
modifying the host.

*EARS template: Conditional (when DESTDIR is set)*

*Acceptance:* Uninstall removes files from `$DESTDIR/usr/` when DESTDIR is set; verification uses only
staged trees under `build/`, never host filesystem modifications.

---

**REQ-F-007**: When `task uninstall-check` is invoked, Files shall verify the uninstall operation in
disposable staged filesystems with mocked privileged commands, exercising all code paths (full payload,
partial installations, absent directories, and failure scenarios) without modifying the host.

*EARS template: Event-driven (scheduled during CI)*

*Acceptance:* All test scenarios pass (full removal, absent files, failure propagation); task exits
cleanly; no host files are modified.

---

**REQ-F-008**: An isolated-runtime-check container shall be built FROM the existing CI image
(`packaging/Dockerfile.ci`), populated with only the installed payload (via `cmake --install` with
DESTDIR, copied in — no source tree, no build tree), and shall verify installed correctness without
development context.

*EARS template: State-driven (container exists with installed payloads)*

*Acceptance:* Container image is created; all required installed files are present; no source or build
artifacts are present; container runs verification scripts and exits cleanly.

---

**REQ-F-009**: The isolated runtime verification shall confirm all required installed files are present
and have correct ownership/permissions, the installed binary contains no development RPATH/RUNPATH
references, no development paths appear in runtime environment variables, the installed binary runs
standalone without development dependencies, and the desktop entry is valid and launches correctly.

*EARS template: State-driven (installed binary properties)*

*Acceptance:* `readelf -d` on installed binary and libraries shows no RPATH/RUNPATH with /work, /home,
/build, or /tmp; `holonight-files --version` succeeds in isolated context; desktop-file-validate
passes; verification runs as files-test with a writable home and private runtime directory.
All five payloads are nonempty regular files owned by root:root, executable mode 0755 and
asset/license modes 0644. Desktop launch rejects pre-existing matching processes, discovers
the installed executable within three seconds, and observes the same PID/start time alive
and non-zombie for three additional seconds; diagnostics are retained and only that process
is cleaned up.

---

**REQ-F-010**: The task interface shall expose all development checks (format-check, tidy, qml-lint,
license-check, install-check) and new system-maintenance tasks (install, uninstall, uninstall-check,
isolated-runtime-check) as independently discoverable and runnable tasks with `task <name> --help`.

*EARS template: Ubiquitous (always available)*

*Acceptance:* `task --list` enumerates all tasks; each task has a `desc:` field; tasks are runnable
individually; task names follow Taskfile.yml conventions.

---

**REQ-F-011**: A combined `task check` (with alias `task verify`) shall sequentially run debug build,
release build, tests, format-check, lint (tidy + qml-lint), license-check, install-check, and
uninstall-check, stopping on first failure.

*EARS template: Event-driven (triggered when `task check` is called)*

*Acceptance:* All checks run in order; first failure stops execution and reports error; all prior checks
pass when invoked together; alias `verify` works identically.

---

**REQ-F-012**: When `task clean` is invoked, it shall remove only Files debug, release, test, and
system-install build directories under `build/`, preserving the local provider installation tree
`build/deps/prefix/` and any verification evidence.

*EARS template: Event-driven (triggered when `task clean` is called)*

*Acceptance:* Directories `build/debug`, `build/release`, `build/test`, `build/system-install` are
removed; `build/deps/prefix/` and `build/install-check.*` directories remain; no other files are
deleted.

---

### Installation payloads and delivery

**REQ-F-013**: The installed package shall retain exactly these payloads: the executable `holonight-files`
in `${CMAKE_INSTALL_BINDIR}`, desktop entry `org.holonight.Files.desktop` in
`${CMAKE_INSTALL_DATADIR}/applications/`, icon `org.holonight.Files.svg` in the XDG icon hierarchy,
and two license files (LICENSE and GPL-3.0-or-later.txt) in `${CMAKE_INSTALL_DATADIR}/licenses/`.

*EARS template: Ubiquitous (payload structure is invariant)*

*Acceptance:* All five files are installed via CMake `install()` commands; staged install verification
checks all files are present; no additional Files-specific files are installed.

---

**REQ-F-014**: The installed executable shall contain no hardcoded references to development locations
(/work, /home, /build, /tmp) in its RPATH or RUNPATH and shall be linked only against system libraries
in `/usr/lib/`.

*EARS template: State-driven (linked state of binary)*

*Acceptance:* `readelf -d holonight-files | grep -E "(RPATH|RUNPATH)"` produces no output or only
contains /usr paths; `ldd holonight-files` lists only system libraries; binary runs in isolated
container without LD_LIBRARY_PATH.

---

**REQ-F-015**: The desktop entry shall be valid per the freedesktop.org Desktop Entry specification,
contain no development environment variable overrides or file-argument handling, and shall match the
source file in `packaging/org.holonight.Files.desktop` exactly.

*EARS template: Ubiquitous (entry validity)*

*Acceptance:* `desktop-file-validate` exits cleanly; development registration does not modify the
packaged entry; `Exec` field matches installed file exactly.

---

**REQ-F-016**: When CI runs, it shall run `task deps`, give files-test ownership of build/,
run `task check` as files-test under C.UTF-8, retain en_US.UTF-8 tests,
and prepare the runtime context in the build container. The runner shall build the runtime image and
run it without workspace/socket mounts and with network disabled, preserving failures
and logs under build/. The context marker shall be repository-relative; build/ ownership shall return to the checkout owner on exit so runner access survives differing container and runner UIDs.

*EARS template: Event-driven (when CI runs)*

*Acceptance:* Reproduce the container sequence locally; record hosted CI execution separately.

**REQ-F-017**: When the development desktop tasks are retired, Files shall remove all
`task desktop-*` entry points, their registration/check scripts, CI callers, and their
generated development launchers, icons, and staged desktop-check output. Installed desktop
payloads and installed-runtime verification shall remain available.

*EARS template: Event-driven (when development desktop tasks are retired)*

*Acceptance:* Task listing exposes no desktop-* tasks; active tooling has no references to
the removed scripts; identified generated desktop-check trees and this checkout's user-level
development entry/icon are absent. Historical verification records retain their original evidence.

---

### Non-goals

**REQ-NF-001**: No new v1 application features, accessibility improvements, or performance work shall be
introduced or attempted in this cycle.

*EARS template: Unwanted-behaviour (prevent feature creep)*

*Acceptance:* Scope is limited to install/uninstall/verification; no changes to application logic, UI,
or accessibility features; no performance benchmarking or optimization.

---

**REQ-NF-002**: No i18n/l10n support, Windows/macOS builds, X11/XWayland support, MIME-type file
argument handling (`holonight-files file.txt`), or launcher extensibility shall be introduced.

*EARS template: Unwanted-behaviour (explicit exclusions)*

*Acceptance:* No localization strings appear in code; build targets are Arch Linux + labwc only; no
X11 platform code; Exec field in desktop entry remains `holonight-files` (no `%f` or arguments).

---

**REQ-NF-003**: No packaging systems (distro packages, Flatpak manifests, AppImage payloads, conda
recipes) shall be implemented; only the staged `cmake --install` workflow shall be hardened.

*EARS template: Unwanted-behaviour (packaging scope exclusion)*

*Acceptance:* No .spec, .ebuild, flatpak.yml, AppImageKit recipes, or conda meta.yaml files are
created; changes remain limited to build, maintenance scripts, CI, and current-cycle documentation.

---

### Constraints

**REQ-C-001**: The target platform for system installation is Arch Linux with labwc compositor on
Wayland only; no X11, XWayland, or other distros shall be supported in this cycle.

*EARS template: Constraint (environment)*

*Acceptance:* Dockerfile.ci and CI scripts target archlinux base image; labwc is listed as a dependency;
Wayland-only code paths are assumed in verification scripts.

---

**REQ-C-002**: The installed executable shall be named exclusively `holonight-files`; no legacy naming,
`hn-files` alias, or symbolic links shall exist.

*EARS template: Constraint (naming)*

*Acceptance:* Single executable name in install commands; no rename handling in uninstall; desktop
entry Exec field is `holonight-files`.

---

**REQ-C-003**: Historical verification records from stages 1–4 shall remain unchanged; this cycle does
not remediate or re-validate prior acceptance checks.

*EARS template: Constraint (documentation)*

*Acceptance:* Files in `docs/sdd/{browse,inspect,modal,file-operations}/VERIFICATION.md` are not
edited; this cycle creates only its own `VERIFICATION.md`.

---

**REQ-C-004**: Uninstall shall require explicit sudo invocation and shall **not** read build config,
CMakeLists.txt, or install manifest files; the uninstall script is self-contained and idempotent by
hardcoded file list alone.

*EARS template: Constraint (operational)*

*Acceptance:* `scripts/uninstall.sh` takes only DESTDIR as implicit state; no CMake state is read; task
invokes `sudo env -u DESTDIR bash scripts/uninstall.sh`; uninstall succeeds identically on second run
without source tree.

---

## Documentation expectations

- Taskfile.yml shall document all new and existing tasks with `desc:` fields.
- README or DESIGN document shall explain the install/uninstall/verify workflow for users and
  contributors.
- CI configuration shall run `task deps`, `task check`, and separate isolated runtime verification.
- Uninstall-before-reinstall pattern shall be documented for development workflows.

## Test and verification strategy

1. **Staged install-check**: Uses existing `scripts/check-install.sh`, validating files present and
   binary runs offline.
2. **Uninstall-check**: Python script with mocked sudo/rm/rmdir, exercises full removal, partial
   installations, absent files, and failure scenarios.
3. **Isolated-runtime-check**: Container built from CI image, receives only installed payloads, verifies
   no dev paths, binary runs, desktop entry launches.
4. **Combined check**: Sequential runs of all checks, integrated into CI pipeline.

## Relationship to holonight-viewer precedent

This specification is adapted from `holonight-viewer`'s system-maintenance and executable-uninstall
cycles (docs/sdd/{system-maintenance,executable-uninstall}/SPEC.md). Key adaptations for Files:
- Single executable name `holonight-files` (no rename pair like `hn-viewer`/`holonight-viewer`).
- No MIME-type file-argument handling (Files opens folders, not individual files).
- No desktop-launch qualification tools (Viewer's `/usr/libexec/holonight-viewer/installed-runtime-probe`
  does not apply here).
- Reference implementation scripts and Dockerfile patterns are reused with minimal changes.
