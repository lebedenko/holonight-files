# System installation and maintenance verification

Review remediation, 2026-09-11. The approved implementation plan covers all four findings.
This record supersedes the earlier claims that one multi-file rm stopped subsequent removals,
process discovery proved successful startup, root execution checked permissions, and CI was out
of scope. Historical verification files from other SDD cycles are unchanged.

## Executed checks

| Command | Outcome and evidence under build/ |
| --- | --- |
| `task deps` | Exit 0; maintenance-deps.log. Providers installed only beneath build/deps; sibling sources unchanged. |
| `task uninstall-check` | Exit 0; maintenance-uninstall.log. Full, partial, absent, repeated uninstall, unrelated files/user configuration, blocked first/middle files, injected first/middle failure, rmdir/database failures, and mocked sudo entry all pass. Helper propagates injected status 29 unchanged and leaves later files untouched. |
| `task check` (sandbox) | Exit 201 at files-smoke: socket creation denied by sandbox; maintenance-check.log. This is not counted as a pass. |
| `task check` (outside sandbox, ordinary host user) | Exit 0; maintenance-check-unsandboxed.log. Debug/release builds, 6/6 CTest entries, formatting, tidy/QML lint, REUSE (129/129), staged install, uninstall pass. Two native/rendered smoke cases skip in offscreen mode. |
| `task isolated-runtime-check` (Docker access outside sandbox) | Exit 0; maintenance-runtime.log. Image built from files-ci, installed five Files payloads plus providers, no volumes, network disabled. files-test verifies root:root ownership, 0755 executable/0644 assets and licenses, nonempty readable regular files, executable access, RPATH audit, version, desktop validity, and three-second process survival after discovery. |
| `python3 scripts/check-runtime-fixtures.py` | Exit 0; maintenance-runtime-fixtures.log and runtime-fixtures.admshj8e/*.log. Healthy exits 0 after 3.554s; delayed startup exit fails during observation (1.546s); missing executable, root-only executable, unreadable desktop entry, incorrect owner, pre-existing process each exit 1 with the expected diagnostic. Fixtures modify only disposable container files. |
| `docker build -t files-ci -f packaging/Dockerfile.ci .` | Exit 0 using local cache; maintenance-ci-image.log. |
| `bash build/maintenance-ci-ownership.sh` | Exit 0; maintenance-ci-ownership.log. Container user UID 1000 writes private evidence; EXIT trap restores ownership to runner UID 1001, which can read/write logs after exits 0 and 23. Original exit status preserved. |
| `bash -n scripts/isolated-runtime.sh scripts/prepare-runtime-check.sh scripts/uninstall.sh`; `git diff --check` | Exit 0. Shell syntax also passed for all four workflow run blocks. PyYAML/actionlint were unavailable locally; hosted workflow validation remains pending. |

The delayed-exit fixture replaces the executable with a small compiled program that accepts
--version, starts through gio at the required installed path, sleeps one second, then exits 1.
It exercises the actual verifier rather than a mock process-discovery function. Diagnostics
are captured by the verifier and emitted on success or failure; fixture logs include statuses
and elapsed times. Linux pidfds and /proc start times scope cleanup to the tracked process.

## CI integration and acceptance

The workflow now runs deps as root, chowns build/ to files-test, and runs check under C.UTF-8,
then en_US.UTF-8 tests and context preparation as files-test. The runner
consumes the repository-relative context marker, builds the runtime image, and runs it with
--network none and no workspace or Docker-socket mounts. Bash pipefail preserves exit status
through tee; an EXIT trap restores build/ ownership to the checkout owner on success or failure, and an always-run artifact step retains logs under build/.

Before the subsequent desktop-task removal below, local reproduction used a disposable checkout copy under build/maintenance-ci/work, mounted
only into the build container at /work. This command exited 0 (maintenance-ci-check.log):

```sh
docker run --rm -v "$PWD/build/maintenance-ci/work:/work" files-ci bash -euc '
  task deps
  chown -R files-test:files-test /work/files/build
  runuser -u files-test -- env LC_ALL=C.UTF-8 task check
  runuser -u files-test -- env LC_ALL=en_US.UTF-8 task test
  runuser -u files-test -- env LC_ALL=C.UTF-8 task desktop-check
  runuser -u files-test -- bash scripts/prepare-runtime-check.sh'
```

Both locale runs passed 6/6 CTest entries; formatting, lint, license, staged install,
uninstall, and desktop-check passed. The generated marker contains
`build/runtime-check.8iRoYb`, usable relative to the checkout outside the container.
`bash build/maintenance-ci-runtime.sh` then exited 0: it changed to the copied checkout,
built `holonight-files-runtime-ci-check` from that marker, and ran it with
`docker run --rm --network none holonight-files-runtime-ci-check`, without mounts.
Logs: maintenance-ci-runtime-build.log and maintenance-ci-runtime.log; the installed
process survived the full three-second observation interval as files-test.

The ownership EXIT trap was added during this run and tested separately with differing
UIDs, including a failure exit (above). The final workflow shell blocks pass bash syntax
checks; the hosted YAML runner and artifact service have not been exercised locally.
Hosted GitHub Actions execution remains pending; local reproduction does not prove artifact
upload or hosted runner behavior.

## Requirement coverage and limitations

- REQ-F-004–REQ-F-007 / T-004, T-008: staged removal and failure evidence above.
- REQ-F-008–REQ-F-009, REQ-F-013–REQ-F-015 / T-005–T-007, T-013, T-019:
  installed-runtime and fixture evidence above. Exactly five Files payloads are checked.
  The offscreen survival interval does not establish native labwc usability or later stability.
- REQ-F-011: combined check passed outside the sandbox; REQ-F-016 / T-018:
  workflow implemented, local container reproduction and ownership handoff passed, hosted execution pending.
- REQ-F-001, REQ-F-010, REQ-F-012: task recipes retain prior behavior; no new direct task run,
  task clean, or exhaustive help invocation was performed in this remediation.
- REQ-F-002–REQ-F-003 and real host REQ-F-004 / T-020: real /usr install/uninstall and
  native desktop acceptance remain pending. No host /usr mutation was performed.
- REQ-NF-001–REQ-NF-003 and REQ-C-003: changes limited to maintenance/build/CI/docs;
  application code, sibling sources, and historical SDD records were preserved.
- T-017: closed below (2026-09-11 re-run). Its own criterion — `task check` and
  `task isolated-runtime-check` passing end-to-end with REQ evidence recorded — is met.
  Real-host `/usr` install/uninstall and hosted CI execution are tracked separately as
  T-020 and the CI gap noted above; they do not block T-017.

Spark delegation was attempted for the uninstall helper and tests, but failed before work
because the Spark usage limit was exhausted. The main agent completed that isolated change.


## Development desktop task removal — 2026-09-11

REQ-F-017 / T-021 supersedes the development-registration interface from the scaffold cycle.
Removed desktop-install and desktop-check from Taskfile.yml, their dedicated Python scripts,
the CI caller, and active README guidance. The packaged desktop entry/icon and installed
desktop validation and launch checks remain in place. Prior commands and evidence above
record what actually ran before this follow-up and are not claims that desktop-check still exists.

Removed the three identified desktop-check.* trees (two directly under build/ and one
under build/maintenance-ci/work/files/build/), build/review-desktop.log, and the generated
~/.local/share/applications/org.holonight.Files.desktop and
~/.local/share/icons/hicolor/scalable/apps/org.holonight.Files.svg. Before removal, the
user entry was verified to point into this checkout's build/debug tree and its icon
matched the packaged source. Exact paths and removal checks: build/desktop-task-removal.log.

- `task --list`: exit 0; build/desktop-task-removal-tasks.log contains no desktop-* tasks.
- `task deps`: exit 0; build/desktop-task-removal-deps.log.
- Focused reference/artifact checks and workflow shell syntax: passed; removal log above.
- `task check` outside the sandbox: exit 0; build/desktop-task-removal-check.log. All six CTest entries, formatting, lint, license, staged install, and uninstall checks passed.
- `git diff --check`: passed.
- Hosted CI after task removal remains pending. Installed payload rules and runtime
  verification code were not modified by this follow-up.

Spark delegation was attempted again but failed before edits due to its usage limit;
the main agent completed the isolated changes locally.

## T-017 closure — end-to-end re-run (2026-09-11)

Re-ran both commands named in T-017's own check criterion, unsandboxed, against the
current working tree (including the same-day T-118/T-119/T-122/T-127 file-operations
additions), rather than relying on the earlier remediation's evidence above.

- `task check`: exit 0; `build/t017-check.log`. Debug and release configure/build,
  `ctest --preset test` 7/7 suites passed in 30.58s total — `files-smoke` (30.21s, includes
  the T-127 multi-gigabyte throughput/memory test), `files-fsops-smoke`,
  `files-fsops-window-smoke` (the new combined rendered cross-filesystem VISUAL trash
  binary), `files-help`, `files-version`, `files-reject-argument`, `files-qml-failure`.
  `clang-format`/`qmlformat` format-check, `clang-tidy`/`qmllint` lint, REUSE license-check
  (128/128 files compliant), staged `install-check`, and `uninstall-check` all passed with
  no findings.
- `task isolated-runtime-check`: exit 0; `build/t017-isolated-runtime.log`. Release build,
  `scripts/prepare-runtime-check.sh` staged the payload, `docker build` produced
  `holonight-files-runtime-check` from `files-ci`, and `docker run --rm --network none`
  reported `holonight-files 0.1.0` and "Desktop launch passed: ... observed for three
  seconds" — matching REQ-F-008/REQ-F-009's isolated, network-disabled runtime check.

This confirms T-017's stated acceptance gate (both tasks pass end-to-end, REQ evidence
recorded) against the current tree, not just the earlier remediation snapshot. It does not
newly satisfy T-020 (real host `/usr` mutation) or hosted CI, which remain separately open.
