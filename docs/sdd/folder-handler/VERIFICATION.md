# Verification

Date: 2026-09-16. Authorized by the supplied implementation plan.
Implementation and automated verification complete; host acceptance pending.

## Evidence

Generated logs are under `build/folder-handler/`; generated staged installations,
uninstall fixtures and runtime contexts use their existing `build/` directories.
No sibling provider sources were modified. New material inherits the repository's
GPL-3.0-or-later REUSE annotation.

- R1/R5: `task deps` and `task build` passed (`deps.log`, `build.log`).
  Debug, release and test configurations linked `apps/files/hn-files`.
  Application identity, project name, icon, desktop ID and license path are retained.
- R4: Initial sandboxed `task test` passed seven CTest entries but failed
  files-smoke's Unix-socket fixture with permission denied (`test.log`).
  The approved unsandboxed rerun passed all eight entries in 36.75 seconds
  (`test-unsandboxed.log`), including help, version, extra-argument rejection,
  QML startup failure and existing initial-directory fallback coverage.
  Two opt-in native smoke cases are skipped by the default suite.
- R2: `task uninstall-check` passed. Fixtures cover legacy-only, new-only and
  mixed payloads, repeated execution, blocked new/legacy binaries and metadata,
  later-payload preservation, privileged command ordering and failure propagation.
- R1–R6: Final `task check` passed end to end (`check-final.log`):
  debug/release builds, all eight CTest entries (36.70 seconds), formatting,
  52 clang-tidy analyses, QML lint, REUSE, staged installation and uninstall.
  The first full run (`check.log`) reached the staged MIME check, where GIO
  omitted the launcher because the staged executable was not on PATH.
  The verifier now prepends the staged bin directory. Focused install/uninstall
  checks then passed (`install-check.log`, `uninstall-check.log`) before the
  passing final full run.
- R1/R3: Fresh stage `build/install-check.tNnLey/usr` contains the five
  expected payload files and no legacy executable or symlink. Desktop validation
  and exact Exec/MimeType assertions pass. Its isolated XDG database lists Files
  as registered/recommended for inode/directory. GIO also reports Files as the
  default in this otherwise empty database; no default was explicitly selected
  and no mimeapps.list was written. Host defaults were not modified.
- R3/R4: `task isolated-runtime-check` passed
  (`isolated-runtime-unsandboxed.log`) with installed providers, an ordinary
  container user and no network/workspace mount. The process ran for three
  seconds, its executable resolved to /usr/bin/hn-files, and its NUL-separated
  arguments were exactly the end-of-options marker plus the single temporary
  folder path containing spaces and Україна. The initial sandboxed attempt
  could not access the Docker socket (`isolated-runtime.log`); the approved
  unsandboxed rerun succeeded.
- R1/R4: `python3 scripts/check-runtime-fixtures.py` passed all eight cases
  (`runtime-fixtures.log`, detailed `build/runtime-fixtures.p02m1y7p/`):
  healthy, delayed exit, missing folder argument, missing executable, root-only
  executable, unreadable desktop metadata, incorrect ownership and pre-existing
  process. Docker access required approved execution outside the sandbox.
- Shell syntax checks and `git diff --check` passed.
- Spark delegation was attempted for uninstall implementation; spawning failed
  because its configured `gpt-5.3-codex-spark` model was unavailable. Main
  implemented and verified the isolated change locally.

## Pending acceptance

- After a separate host `task uninstall` then `task install`, confirm
  `gio mime inode/directory` includes `org.holonight.Files.desktop` and the
  native default-file-manager chooser offers Files.
- Launch a local folder containing spaces and Unicode through the native desktop
  and confirm that the displayed folder matches. Automated argument observation
  does not substitute for this visual acceptance.
- Host installation and default-app selection were not performed by this cycle.
