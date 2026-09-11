# SDD Tasks — system-maintenance

- [x] T-001: Add CMake `system-install` preset
  - REQs: REQ-F-002, REQ-F-008
  - Check: CMakePresets.json contains a `system-install` preset with `CMAKE_PREFIX_PATH=/usr`, `HOLONIGHT_DEPENDENCY_PREFIX=/usr`, and `binaryDir` set to `${sourceDir}/build/system-install`.

- [x] T-002: Decouple `task run` from `desktop-install`
  - REQs: REQ-F-001
  - Check: `task run` no longer depends on `desktop-install`; invoking `task run` does not modify XDG_DATA_HOME or generate desktop registration files.

- [x] T-003: Add `desc:` fields to all pre-existing tasks
  - REQs: REQ-F-010
  - Check: All pre-existing tasks (`deps`, `configure`, `build`, `test`, `format`, `format-check`, `tidy`, `qml-lint`, `license-check`, `install-check`) have non-empty `desc:` fields; `task --list` enumerates all tasks with descriptions.

- [x] T-004: Create `scripts/uninstall.sh`
  - REQs: REQ-F-004, REQ-F-005, REQ-F-006, REQ-C-004
  - Check: Script is executable, removes exactly five hardcoded payload paths from `${DESTDIR:-}/usr`, supports DESTDIR environment variable, propagates errors on first failure, and succeeds idempotently on second invocation.

- [x] T-005: Create `scripts/prepare-runtime-check.sh`
  - REQs: REQ-F-008
  - Check: Script creates a disposable build context under `build/runtime-check-*` containing `payload/usr/` with staged installs and `check/scripts/isolated-runtime.sh`, but no source tree or build tree artifacts.

- [x] T-006: Create `scripts/isolated-runtime.sh`
  - REQs: REQ-F-009
  - Check: Script verifies regular root:root files with modes 0755/0644 as files-test, audits RPATH/RUNPATH, runs --version and desktop validation, discovers launch within three seconds and observes the same non-zombie process for three more seconds; exits nonzero on failure.

- [x] T-007: Create `packaging/Dockerfile.runtime-check`
  - REQs: REQ-F-008
  - Check: Dockerfile builds FROM the existing `files-ci` image, copies only the staged payload and check scripts, and sets CMD to `bash scripts/isolated-runtime.sh`.

- [x] T-008: Create `scripts/check-uninstall.py`
  - REQs: REQ-F-007
  - Check: Script exercises `scripts/uninstall.sh` in disposable staged filesystems under `build/` with mocked `sudo`/`rm`/`rmdir`/`update-desktop-database`, exercises all scenarios (full removal, absent files, failures), and exits cleanly without modifying the host.

- [x] T-009: Add `task install` to Taskfile.yml
  - REQs: REQ-F-002, REQ-F-003
  - Check: `task install` configures and builds the `system-install` preset against `/usr`, installs payload files to `/usr` via `sudo`, refreshes the desktop database, and fails immediately if configure or build fails without installing.

- [x] T-010: Add `task uninstall` to Taskfile.yml
  - REQs: REQ-F-004, REQ-F-005, REQ-F-006
  - Check: `task uninstall` invokes `sudo env -u DESTDIR bash scripts/uninstall.sh` to remove payload files and refresh the desktop database; running it twice succeeds both times.

- [x] T-011: Add `task uninstall-check` to Taskfile.yml
  - REQs: REQ-F-007
  - Check: `task uninstall-check` invokes `python3 scripts/check-uninstall.py`, exercises all verification scenarios, and exits cleanly without modifying the host filesystem.

- [x] T-012: Add `task lint` to Taskfile.yml
  - REQs: REQ-F-010, REQ-F-011
  - Check: `task lint` runs `tidy` and `qml-lint` sequentially, stops on first failure, and is independently invocable with `task lint --help`.

- [x] T-013: Add `task isolated-runtime-check` to Taskfile.yml
  - REQs: REQ-F-008, REQ-F-009
  - Check: `task isolated-runtime-check` builds the `release` preset, runs `prepare-runtime-check.sh`, builds a Docker image with `docker build`, runs the image with `--network none`, and propagates container exit status to the task.

- [x] T-014: Add `task check` (alias `task verify`) to Taskfile.yml
  - REQs: REQ-F-011
  - Check: `task check` runs `build`, `build` (PRESET=release), `test`, `format-check`, `lint`, `license-check`, `install-check`, and `uninstall-check` in order; first failure stops execution; `task verify` runs identically.

- [x] T-015: Add `task clean` to Taskfile.yml
  - REQs: REQ-F-012
  - Check: `task clean` removes only `build/debug`, `build/release`, `build/test`, and `build/system-install`; `build/deps/prefix/` and `build/install-check.*` directories are preserved.

- [x] T-016: Update README or CONTRIBUTING with installation workflow documentation
  - REQs: SPEC "Documentation expectations"
  - Check: README or CONTRIBUTING documents `task install` (requires sudo, targets `/usr`, requires providers already installed system-wide), `task uninstall` (uninstall-before-reinstall pattern), `task uninstall-check`/`task isolated-runtime-check` as local verification commands, and `task clean` scope; CI documentation notes `task deps` → `task check` → `task isolated-runtime-check` as the full pipeline.

- [ ] T-017: Create `docs/sdd/system-maintenance/VERIFICATION.md` and verify end-to-end acceptance
  - REQs: REQ-F-001 through REQ-F-016, REQ-NF-001 through REQ-NF-003, REQ-C-001 through REQ-C-004
  - Check: Run `task check` and `task isolated-runtime-check` end-to-end in a clean build environment; all checks pass; VERIFICATION.md documents acceptance evidence for each REQ (REQ-F-001: XDG_DATA_HOME unchanged after `task run`; REQ-F-002: `/usr` install with providers; ...; REQ-F-013–REQ-F-015: payload verification; REQ-C-001: Arch+labwc target; REQ-C-004: uninstall works without source tree).


- [x] T-018: Integrate maintenance verification into CI
  - REQs: REQ-F-016
  - Check: deps, ordinary-user check in C.UTF-8, en_US.UTF-8 tests; relative context prepared inside build container and consumed by runner; runtime has no mounts/network; logs preserve status under build/.
- [x] T-019: Exercise runtime failure fixtures
  - REQs: REQ-F-009, REQ-F-013, REQ-F-014
  - Check: `python3 scripts/check-runtime-fixtures.py`; healthy launch survives three-second observation after discovery; delayed exit, missing/root-only executable, unreadable desktop asset, wrong ownership fail in disposable containers using the actual verifier.
- [ ] T-020: Real host install/uninstall acceptance
  - REQs: REQ-F-002, REQ-F-004
  - Check: Pending; no host /usr mutations authorized by this remediation.


- [x] T-021: Retire development desktop tasks and generated artifacts
  - REQs: REQ-F-017
  - Check: Remove desktop-* tasks, dedicated scripts and CI call; remove identified generated development entry/icon and desktop-check trees/log; task listing, focused reference checks and required quality checks pass. Preserve installed payloads and historical records.
