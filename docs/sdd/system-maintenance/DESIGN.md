# Design: system-maintenance

Architecture for the install/uninstall lifecycle and installed-package verification described in
`docs/sdd/system-maintenance/SPEC.md`. Scope, non-goals, and constraints are inherited from that
document and are not repeated here except where they drive a specific design choice.

This cycle adapts the pattern already implemented twice in `holonight-viewer`
(`docs/sdd/system-maintenance/` and `docs/sdd/executable-uninstall/`), simplified for Files' single
executable name and folder-only launch model (no legacy alias, no MIME/file-argument handling, no
installed-runtime probe binary).

## 1. Components

### 1.1 New files

| Path | Purpose |
|---|---|
| `scripts/uninstall.sh` | Removes the five hardcoded Files payload paths from `${DESTDIR:-}/usr`, refreshes the desktop database. Sole implementation of REQ-F-004–REQ-F-006 and REQ-C-004. |
| `scripts/check-runtime-fixtures.py` | Runs healthy and failure variants of the actual runtime verifier in disposable network-isolated containers; logs under build/. |
| `scripts/check-uninstall.py` | Exercises `scripts/uninstall.sh` (directly and through `task uninstall`) against disposable staged trees under `build/`, with `rm`/`rmdir`/`update-desktop-database`/`sudo` shadowed by mock executables on `PATH`. Implementation of REQ-F-007. |
| `scripts/prepare-runtime-check.sh` | Builds a disposable container-build context under `build/`: installs the sibling providers and the Release build's payload into a staged `payload/` tree via `DESTDIR`, copies `scripts/isolated-runtime.sh` and `packaging/Dockerfile.runtime-check` into the context, and records the context path. No source or build tree is copied in. |
| `scripts/isolated-runtime.sh` | Runs *inside* the runtime-check container as its `CMD`. Performs every REQ-F-009 check: payload presence, RPATH/RUNPATH audit, `--version`, `desktop-file-validate`, `gio launch`. |
| `packaging/Dockerfile.runtime-check` | Builds `FROM` the existing `files-ci` image (per REQ-F-008, `packaging/Dockerfile.ci` is the base — not a fresh `archlinux:base`, so the container already has Qt, `desktop-file-utils`, `glib2`/`gio`, and `binutils`/`readelf` available), `COPY`s only the staged payload and check scripts, and runs `isolated-runtime.sh`. |

### 1.2 Modified files

| Path | Change |
|---|---|
| `.github/workflows/build.yml` | Runs ordinary-user combined checks and separate runtime build/run, retaining logs. |
| `Taskfile.yml` | Add `install`, `uninstall`, `uninstall-check`, `isolated-runtime-check`, `lint`, `check` (alias `verify`), `clean`. Add missing `desc:` fields to every existing task. Keep `run` independent of desktop registration and retire the desktop-* tasks (see §6.1). |
| `CMakePresets.json` | Add a `system-install` configure preset and matching build preset (see §3). No changes to `debug`/`release`/`test`/`testPresets`. |
| `CMakeLists.txt`, `apps/files/CMakeLists.txt` | No changes required — install payloads already match REQ-F-013 exactly, and no explicit RPATH settings exist to remove (CMake's install-time RPATH is empty by default; see §7.4). Verified, not modified. |
| `README.md` / `CONTRIBUTING.md` | Document the install/uninstall/verify workflow and the uninstall-before-reinstall pattern (Documentation expectations in SPEC). Exact file TBD by whichever of the two already carries contributor workflow docs; content described in §9. |

`packaging/Dockerfile.ci` supplies tomlplusplus and procps-ng in addition to `desktop-file-utils`, `glib2`, `dbus`,
`binutils`, and a non-root `files-test` user, all of which the runtime-check image inherits by building
`FROM files-ci`.

## 2. Task dependency graph

```
deps ──────────────────────────────────────────────────────────────┐
                                                                     │
configure(PRESET) ── build(PRESET) ── test / run / install-check    │
                                                                     │
install ── (own system-install preset, independent of deps/build)  │
uninstall ── (independent; sudo + scripts/uninstall.sh)             │
uninstall-check ── (independent; no sudo, no /usr)                  │
isolated-runtime-check ── build(release) + deps ── prepare-runtime-check.sh ── docker build/run
                                                                     │
check/verify ── build(debug) → build(release) → test → format-check │
              → lint → license-check → install-check → uninstall-check
clean ── (independent; rm -rf four build/ subdirectories)
```

`install` intentionally does **not** depend on `deps`/`build`/`configure`: it drives its own
`system-install` preset against `/usr`, which must never touch `build/deps/prefix`. `check` does not
include `isolated-runtime-check` (container build time is unbounded and the container needs a
Release payload staged with `DESTDIR`, which `check`'s own `build/release` does not produce) — this
mirrors the viewer precedent, where Docker qualification is explicitly kept out of `check`.

## 3. CMake `system-install` preset

`CMakePresets.json` currently defines `debug`, `release`, and `test` — all three configure into
`build/<name>`, set `CMAKE_INSTALL_PREFIX=/usr` (never actually installed to from these presets; the
prefix is just carried for consistency), and point `HOLONIGHT_DEPENDENCY_PREFIX`/`QML_IMPORT_PATH` at
the *development* prefix `build/deps/prefix` via each preset's `environment` block and (indirectly,
through the Taskfile `configure` task) a `-DHOLONIGHT_DEPENDENCY_PREFIX=...` cache flag.

REQ-F-002 requires a distinct configure+build step, in its own `build/system-install` directory, that
resolves `HolonightQt` and QML imports from installed `/usr` providers instead. The natural extension
of the existing preset family is a fourth preset:

```jsonc
{
  "name": "system-install",
  "generator": "Ninja",
  "binaryDir": "${sourceDir}/build/system-install",
  "cacheVariables": {
    "CMAKE_BUILD_TYPE": "Release",
    "BUILD_TESTING": "OFF",
    "CMAKE_INSTALL_PREFIX": "/usr",
    "CMAKE_INSTALL_LIBDIR": "lib",
    "CMAKE_PREFIX_PATH": "/usr",
    "HOLONIGHT_DEPENDENCY_PREFIX": "/usr",
    "HOLONIGHT_QML_IMPORT_PATH": "/usr/lib/qt6/qml",
    "HolonightQt_DIR": "/usr/lib/cmake/HolonightQt"
  },
  "environment": {
    "QML_IMPORT_PATH": "/usr/lib/qt6/qml",
    "QML2_IMPORT_PATH": "/usr/lib/qt6/qml",
    "LD_LIBRARY_PATH": "/usr/lib"
  }
}
```

paired with a `system-install` entry in `buildPresets` (`configurePreset: system-install, jobs: 2`),
mirroring the other three. `HOLONIGHT_DEPENDENCY_PREFIX=/usr` alone would make
`CMakeLists.txt`'s own `list(PREPEND CMAKE_PREFIX_PATH "${HOLONIGHT_DEPENDENCY_PREFIX}")` sufficient
for `find_package(HolonightQt)` to resolve under `/usr`; `CMAKE_PREFIX_PATH` and `HolonightQt_DIR` are
set redundantly, matching the belt-and-suspenders approach `holonight-viewer`'s raw install command
uses, as a guard against ordering surprises if more than one Qt/HolonightQt config is ever reachable.

The `install` Task then reduces to a plain `cmake --preset system-install` / `cmake --build --preset
system-install` / `sudo cmake --install build/system-install` / `sudo update-desktop-database` sequence
— see §7.1 for why this preset-based approach was chosen over `holonight-viewer`'s inline raw-flags
shell block.

Testing note: `system-install`'s `testPresets` entry is intentionally omitted — `BUILD_TESTING=OFF`
matches `release`, and no `ctest --preset system-install` is ever invoked.

## 4. Data flow

### 4.1 `task install`

```
task install
  → cmake --preset system-install            (configure build/system-install against /usr)
  → cmake --build --preset system-install    (Release build; REQ-F-003: stop here on failure, nothing below runs)
  → sudo cmake --install build/system-install (installs 5 payload files to real /usr; REQ-F-002)
  → sudo update-desktop-database /usr/share/applications
```

Task's `set -e`-equivalent (each `cmds:` entry runs in its own shell, and Task stops the task on the
first non-zero exit) satisfies REQ-F-003 without extra scripting: a configure or build failure means
the two `sudo` lines never execute, so nothing is installed. The two `sudo` invocations are separate
commands (not a single `sudo sh -c '...'`), so a user who declines/fails authentication before
`cmake --install` never reaches `update-desktop-database`.

### 4.2 `task uninstall`

```
task uninstall
  → sudo env -u DESTDIR bash scripts/uninstall.sh
       reads DESTDIR (undefined here → "" → real /usr)
       rm -f the 5 hardcoded payload paths        (REQ-F-004; fails loudly & stops on first error → REQ-F-005)
       rmdir --ignore-fail-on-non-empty the license dir (only removed when empty)
       [ -d applications dir ] && update-desktop-database
```

`env -u DESTDIR` is defensive: it guarantees a developer's shell-exported `DESTDIR` (e.g. left over from
manually staging a check) cannot silently redirect a real uninstall away from `/usr`, and conversely
that `task uninstall` can never accidentally target a stage. `sudo` is the privilege boundary; the
script itself never elevates privileges or re-execs.

### 4.3 `task uninstall-check`

Entirely inside `build/`, no `sudo`, no real `/usr` touched (REQ-F-007):

```
python3 scripts/check-uninstall.py
  mkdir build/uninstall-check.<random>/
  mkdir .../commands/  → write mock rm, rmdir, update-desktop-database, sudo (chmod +x)
  PATH=.../commands:$PATH  (shadows the real binaries for subprocess calls this script makes)
  for each scenario (full payload, absent files, partial subset, failure via *_STATUS env vars,
                      a directory occupying a payload-file path, the real `task uninstall` entry
                      point with a faked `sudo` that redirects to a disposable tree):
    seed a staged "$stage/usr/..." tree
    run scripts/uninstall.sh (or `task uninstall`) with DESTDIR=$stage and PATH pointing at the mocks
    assert: exit code, mock invocation order/log, which files were removed vs preserved
```

The mocked-command pattern is the same one `holonight-viewer`'s `check-uninstall.py` uses: real file
removal is exercised (mocks `exec` into the real `rm`/`rmdir` unless a `*_STATUS` override forces
failure), so the test is checking the actual filesystem behavior of the script, while `sudo` is fully
faked (never actually elevating in CI) and asserts the exact command line the Task invokes it with.

### 4.4 `task isolated-runtime-check`

```
task isolated-runtime-check
  → task build PRESET=release                       (produces build/release payload to stage)
  → bash scripts/prepare-runtime-check.sh
        DESTDIR=<ctx>/payload cmake --install build/deps/holonight-config --prefix /usr
        DESTDIR=<ctx>/payload cmake --install build/deps/holonight-qt     --prefix /usr
        DESTDIR=<ctx>/payload cmake --install build/release               --prefix /usr
        cp scripts/isolated-runtime.sh   <ctx>/check/scripts/
        cp packaging/Dockerfile.runtime-check <ctx>/Dockerfile
        echo <repository-relative ctx> > build/runtime-check-context
  → docker build -t holonight-files-runtime-check "$(cat build/runtime-check-context)"
  → docker run --rm --network none holonight-files-runtime-check
        (container CMD runs scripts/isolated-runtime.sh, which performs every REQ-F-009 check
         and exits non-zero on any failure, which `docker run` propagates)
```

`--network none` on the run (not the build) matches REQ-F-009's "runs standalone without development
dependencies": network access is unnecessary for a pure filesystem/process check and its absence proves
nothing at runtime reaches out for anything the installed payload should already contain. This task
requires `build/deps/holonight-config` and `build/deps/holonight-qt` to already exist (i.e. `task deps`
has run) — `prepare-runtime-check.sh` installs *from* those build trees, it does not rebuild them.
`isolated-runtime-check` does not depend on `task: deps` in the Taskfile graph (mirroring
`holonight-viewer`, where the CI workflow runs `task deps` once up front rather than each check
re-running it); this is documented as a prerequisite rather than encoded as a dependency, to avoid
rebuilding providers on every invocation during local iteration.

## 5. Interfaces

### 5.1 `scripts/uninstall.sh`

- **Input**: `DESTDIR` environment variable (optional; unset or empty → operates on real `/usr`).
  Reads no other environment, no CLI arguments, no CMake cache, no install manifest (REQ-C-004).
- **Payload list** (hardcoded, exactly 5 paths — REQ-C-002 forbids a legacy-name pair, so this list is
  one item shorter than `holonight-viewer`'s 6):
  ```
  $prefix/bin/holonight-files
  $prefix/share/applications/org.holonight.Files.desktop
  $prefix/share/icons/hicolor/scalable/apps/org.holonight.Files.svg
  $prefix/share/licenses/holonight-files/LICENSE
  $prefix/share/licenses/holonight-files/GPL-3.0-or-later.txt
  ```
  where `$prefix = ${DESTDIR:-}/usr`.
- **Behavior**: `rm -f --` (nonrecursive, non-glob, absent files are not an error) on the five paths as
  ordered individual commands, so a partial failure (e.g. a directory occupying one of those paths, which `rm -f`
  without `-r` refuses) aborts before any `rmdir`/database step runs. Then
  `rmdir --ignore-fail-on-non-empty` the license directory (removed only if now empty — an unrelated
  file left in `share/licenses/holonight-files/` blocks removal, preserving it). Then, only if
  `share/applications` exists under `$prefix`, run `update-desktop-database` on it.
- **Exit codes**: propagates the first failing command's exit status unmodified (`set -euo pipefail`);
  0 on full success including the idempotent already-removed case (REQ-F-004's "succeeds on second
  invocation").
- **Idempotency**: guaranteed by `rm -f` (no error on missing file) and
  `rmdir --ignore-fail-on-non-empty` (no error on missing/nonempty directory); a second run after a
  clean first run does nothing and still exits 0.

### 5.2 Taskfile task signatures

| Task | Vars | Depends on (Task graph) | `desc:` |
|---|---|---|---|
| `install` | none | none (self-contained) | "Build Release against installed system providers, install to /usr and refresh desktop metadata" |
| `uninstall` | none | none | "Remove the installed Files payload from /usr and refresh desktop metadata" |
| `uninstall-check` | none | none | "Verify uninstall in disposable staged filesystems with mocked privileged commands" |
| `isolated-runtime-check` | none | `build` (PRESET=release, inline) | "Build and verify the installed payload in a disposable, network-isolated container" |
| `lint` | none | `tidy`, `qml-lint` | "Run C++ and QML lint checks" |
| `check` / `verify` | none | `build`×2, `test`, `format-check`, `lint`, `license-check`, `install-check`, `uninstall-check`, in that order | "Sequentially run all checks; stop on first failure" |
| `clean` | none | none | "Remove Files debug/release/test/system-install build directories, preserving provider installs" |

Every task — including the pre-existing `deps`, `configure`, `build`, `run`, `test`, `format`,
`format-check`, `tidy`, `qml-lint`, `license-check`, `install-check`, which currently lack one — gets a
`desc:` field, since REQ-F-010's acceptance criterion is unconditional ("each task has a `desc:`
field"), not scoped to only the new tasks.

### 5.3 `packaging/Dockerfile.runtime-check` build context contract

- **Build context**: the disposable directory `prepare-runtime-check.sh` creates under `build/`, not
  the repo root. Contains only `payload/usr/...` (from staged `DESTDIR` installs) and
  `check/scripts/isolated-runtime.sh`.
- **Base image**: `files-ci` (built by the existing `packaging/Dockerfile.ci`), assumed already present
  in the local Docker image cache (CI builds it earlier in the same workflow job; local developers must
  `docker build -t files-ci -f packaging/Dockerfile.ci .` first).
- **`CMD`**: `bash scripts/isolated-runtime.sh`, run as files-test from `WORKDIR /opt/check`, with HOME=/home/files-test and a private mode-0700 XDG_RUNTIME_DIR=/run/files-test.
- **Exit contract**: nonzero exit from `isolated-runtime.sh` fails the `docker build`/`docker run`
  step and thus `task isolated-runtime-check` and the CI job.

### 5.4 `scripts/isolated-runtime.sh` checks (REQ-F-009, in order)

1. Defensively assert no workspace is mounted (`test ! -e /work/files/CMakeLists.txt`, `test ! -e
   /work/files/build`) — a no-op under the documented `--network none`, no-volume `docker run`, but
   catches an accidental future CI change that adds a mount.
2. Require all five payloads to be nonempty readable regular files (no symlinks), root:root owned, with mode 0755 for the executable and 0644 for assets/licenses; require executable access.
3. RPATH/RUNPATH audit: `find /usr/bin/holonight-files /usr/lib -type f \( -name holonight-files -o
   -iname '*holonight*' \)`, `readelf -d` each match, grep for `(RPATH)`/`(RUNPATH)` entries and reject
   any containing `/work`, `/home`, `/build`, or `/tmp` (REQ-F-014).
4. `QT_QPA_PLATFORM=offscreen holonight-files --version` succeeds (REQ-F-009).
5. `desktop-file-validate /usr/share/applications/org.holonight.Files.desktop`.
6. Reject pre-existing matching processes, capture gio diagnostics, discover a process within three
   seconds, resolve /proc/PID/exe to /usr/bin/holonight-files, then observe its PID and start time
   alive and non-zombie for three additional seconds. An EXIT trap prints diagnostics and terminates
   only the tracked process while its start time still matches; no broad pkill.

The image performs COPY and ldconfig as root, creates a private runtime directory and writable home,
then uses USER files-test and sanitized offscreen runtime variables.

### 5.5 CI integration (REQ-F-016)

The build container runs task deps as root, chowns build/ to files-test, runs task check under
C.UTF-8 and task test under en_US.UTF-8 as files-test, then runtime context
preparation as files-test. The context marker is relative to the repository so the runner can
resolve it after leaving the build container. Separate runner steps build and run the runtime
image with --network none and no volumes or Docker socket. Pipefail preserves failures through
tee; a build-container EXIT trap restores build/ ownership to the checkout owner on success or failure so a runner with a different UID can write/read evidence. Logs and uploaded evidence remain under build/.

Unlike `holonight-viewer`'s `isolated-runtime.sh`, this script has no `installed-runtime-probe`
libexec step and no `format-fixtures.py`/image-codec fixture generation — the SPEC explicitly excludes
that qualification tool for Files (Files opens folders, not individual media files; codec/EXIF
correctness is covered by earlier SDD cycles' own test suites, out of scope here per REQ-NF-001).

## 6. Task-interface consolidation

Two pre-existing gaps in `Taskfile.yml` surfaced while reading it against the SPEC; both are fixed as
part of this cycle rather than left for later, since REQ-F-001 and REQ-F-010 directly govern them.

### 6.1 Development launch without desktop registration

`task run` builds and executes the selected preset directly. Per REQ-F-017, remove
desktop-install and desktop-check and their dedicated scripts; CI keeps the installed
desktop validation and startup checks through staged install and isolated-runtime checks.
Remove identified build/desktop-check.* trees (including the copied CI workspace),
the dedicated desktop-check log, and this checkout's generated XDG application entry
and icon. Verify the user entry points into this checkout before removing it.

### 6.2 Missing `desc:` fields

Every retained task has a nonempty `desc:` and appears in `task --list`, satisfying
REQ-F-010. Retired development desktop tasks are excluded from the task interface.

## 7. Key decisions and rationale

1. **Formal `system-install` CMake preset rather than inline raw `cmake` flags in the Task (REQ-F-002).**
   `holonight-viewer`'s `install` task runs a hand-written `cmake -S . -B build/system-install -G Ninja
   -DCMAKE_BUILD_TYPE=Release ...` block directly in the Taskfile. REQ-F-002's own wording ("Release
   preset in `build/system-install`") and Files' existing `CMakePresets.json` (which already
   establishes `debug`/`release`/`test` as named presets rather than inline flags) both favor adding a
   fourth named preset instead: it is directly invocable and inspectable (`cmake --preset system-install
   -N`), consistent with how every other Files build variant is expressed, and keeps `Taskfile.yml`'s
   `install` task to four one-line commands instead of duplicating a 6-flag `cmake -S -B` invocation
   inline.
2. **Hardcoded 5-path payload list in `uninstall.sh`, not a parsed CMake install manifest (REQ-C-004).**
   REQ-C-004 mandates this directly, and the rationale holds independent of the requirement: reading
   `install_manifest.txt` or re-running `cmake --install --component` requires the uninstall script to
   locate and trust a *build tree* (which may not exist, may belong to a different Files version than
   what's actually installed, or may have been deleted after `task clean`) or a *CMake configuration
   step* (which needs a source tree). A host administrator uninstalling a system Files install has
   neither. A 5-line hardcoded list has no such dependency, is trivially auditable in a code review, and
   changes exactly when `apps/files/CMakeLists.txt`'s `install()` calls change — which is already the
   file a reviewer would be looking at in the same change.
3. **`sudo`-wrapped `DESTDIR` script, not distro packaging (REQ-NF-003, REQ-C-004).** An RPM/deb/pacman
   package would give transactional install/uninstall and a real manifest for free, but REQ-NF-003
   excludes it outright for this cycle, and it would also change the verification story entirely (needs
   a package build step, a repository, and a package manager transaction log instead of a disposable
   `DESTDIR` tree) — disproportionate to "harden the existing `cmake --install` workflow," which is the
   SPEC's literal charter.
4. **Mocked-command Python test harness for `uninstall-check`, not a container (REQ-F-007).** The
   container is reserved for REQ-F-008's "no development context" proof, which is a different concern
   (runtime purity) from REQ-F-007's "every removal code path, including failure injection, runs
   without touching the host" (script correctness). Mocking `rm`/`rmdir`/`update-desktop-database`/
   `sudo` on `PATH` lets the test inject arbitrary exit codes (`REMOVE_STATUS=23`, etc.) to prove
   REQ-F-005's stop-on-failure ordering — something a real container run cannot easily parametrize
   per-scenario without rebuilding the image for each failure case. Real file operations are still
   exercised (the mocks `exec` into the genuine `rm`/`rmdir` unless overridden), so the test is not
   purely a mock of behavior — only of the two privileged/environment-touching commands.
5. **`Dockerfile.runtime-check` builds `FROM files-ci`, not a fresh base image (REQ-F-008).** Reusing
   the CI image guarantees the *exact* Qt6/glibc ABI the Release build in `task isolated-runtime-check`
   was compiled against is present as system Qt in the container (REQ-F-008's own wording: "no source
   tree, no build tree" — but the *toolchain-adjacent runtime*, e.g. Qt's private plugin ABI, must
   still line up). A fresh `archlinux:base` risks a newer/older Qt package landing between the CI build
   step and the runtime-check step in the same pipeline run.
6. **Keep the local runtime task and split CI at the container boundary.** The task remains
   independently runnable. CI prepares the same context inside its build container and invokes
   Docker build/run on the runner, avoiding a Docker socket mount in the build or runtime container.
7. **Task `run` decoupled from `desktop-install` (REQ-F-001).** Covered in §6.1.

## 8. Alternatives considered

- **Reading `build/system-install/install_manifest.txt` in `uninstall.sh` instead of a hardcoded list.**
  Rejected per REQ-C-004 and decision 2 above — would require a build tree to exist and to match what's
  actually installed, which is precisely the coupling `uninstall.sh` must not have.
- **A single combined `task maintain` doing install-check + uninstall-check + isolated-runtime-check.**
  Rejected: REQ-F-010 requires each to be independently runnable, and folding container qualification
  into `check` (or a `check`-adjacent umbrella) was explicitly rejected by the viewer precedent's own
  non-goals ("Docker qualification remain[s] separate from `check`") for build-time reasons — a
  container build/run adds tens of seconds to minutes that `task check`'s "run before every commit"
  audience should not pay by default.
- **Distro packaging (pacman `PKGBUILD`, Flatpak, AppImage) as the uninstall mechanism.** Rejected by
  REQ-NF-003; see decision 3.
- **Keeping `task run` → `desktop-install` as-is and reinterpreting REQ-F-001 as "only forbids writing
  outside a build-scoped XDG override."** Rejected: REQ-F-001's acceptance criterion is literal
  ("XDG_DATA_HOME remains unchanged after `task run`"), and `register-desktop.py` has no code path that
  respects a build-scoped override unless one is explicitly exported — the correct fix is to stop
  calling it from `run`, matching the already-proven `holonight-viewer` shape, not to add an
  auto-detected sandboxing mode to the registration script itself (which would be new script behavior
  for a task this SPEC does not ask `run` to have).

## 9. Known risks

- **`sudo` requirement in CI.** `task install`/`task uninstall` (real, non-`DESTDIR` invocations)
  require interactive or passwordless `sudo`, which most CI runners either lack or must be configured
  for. Mitigation: CI never calls `task install`/`task uninstall` directly — it exercises the install
  path only via `task install-check` (staged, no `sudo`) and the uninstall path only via
  `task uninstall-check` (mocked `sudo`, no real privilege escalation). Real `task install`/`uninstall`
  are host/developer-only commands, documented as such.
- **Container build time.** `task isolated-runtime-check` rebuilds a Docker image on every invocation
  (`prepare-runtime-check.sh` always creates a fresh `mktemp -d` context, so Docker's layer cache only
  helps the `FROM files-ci` base layer). This is acceptable for CI (runs once per pipeline) but makes
  the task noticeably slower than the other checks for local iteration; documented as a check to run
  before pushing, not on every save.
- **RPATH-audit false positives from substring matching.** The `grep -E '/work|/home|/build|/tmp'`
  pattern (inherited from `holonight-viewer`) matches *substrings*, not path components — a legitimate
  system path that happens to contain one of these words (e.g. a hypothetical `/usr/lib/buildinfo/`)
  would fail the check even though it is not a development path. Mitigation: anchor the pattern to path
  boundaries (`(^|/)(work|home|build|tmp)(/|$)`) when implementing `isolated-runtime.sh`, rather than
  reusing the bare substring pattern verbatim.
- **`update-desktop-database` availability.** Both `uninstall.sh` and `task install` assume
  `update-desktop-database` is on `PATH` (provided by `desktop-file-utils`, already a
  `packaging/Dockerfile.ci` dependency and a reasonable assumption on any Arch+labwc target per
  REQ-C-001). On a real host missing that package, `task install`'s final step or `uninstall.sh`'s
  refresh step fails loudly (by design, per REQ-F-005) rather than silently skipping — documented as an
  install prerequisite rather than made optional, since a stale desktop database after install/uninstall
  is a real (if minor) regression.
- **Desktop startup observation is bounded.** gio launch returning zero does not prove successful
  application startup. Polling /proc establishes PID/start-time/executable identity, then observes
  three seconds of survival; later failures and native compositor behavior remain outside this check.
  Linux pidfds bind cleanup to the observed process even if its numeric PID is reused.
- **Provider version drift between `build/deps/` and `/usr`.** `isolated-runtime-check` installs
  `build/deps/holonight-config` and `build/deps/holonight-qt` (whatever the developer's local
  `task deps` last built) into the container image, not necessarily the same commit CI's own
  `holonight-config`/`holonight-qt` checkout pins. This mirrors `holonight-viewer`'s existing behavior
  and is an accepted characteristic of the "sibling repos built from source, no package registry" model
  the whole HoloNight umbrella currently uses — not a regression introduced by this cycle.

## 10. Documentation plan

Per the SPEC's "Documentation expectations": `Taskfile.yml` carries `desc:` on every task (§6.2, so
`task --list` is self-documenting); README/CONTRIBUTING gains a short section describing `task install`
(requires `sudo`, targets real `/usr`, requires providers already installed system-wide — REQ-F-002's
"not the development dependency prefix" is a hard prerequisite the docs must state explicitly, since
`task install` cannot install `holonight-config`/`holonight-qt` itself), `task uninstall`
(uninstall-before-reinstall pattern: run `task uninstall` before a repeat `task install` to avoid
partial-overwrite states), `task uninstall-check` and `task isolated-runtime-check` as the two
non-host-mutating verification commands a contributor can run locally, and `task clean`'s scope (four
build directories only, providers and check evidence preserved). CI configuration documentation notes
that `task deps` → `task check` → `task isolated-runtime-check` is the full pipeline sequence, with the
last step network-isolated and run after the others.
