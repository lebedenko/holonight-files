# Verification — 2026-09-29

Approved scope implemented locally; no publication, commits, umbrella pins or host installation changes.

## Runtime repair

The original image failed silently on `test -w "$HOME"`: `/home/files-test` was root-owned mode 0700. Correcting that in a disposable container exposed missing `libwebp.so.7`; the stale local CI image lacked the package already listed in the current Dockerfile.

Both images now enforce private home ownership. The verifier reports failed commands and line numbers. The local task snapshots working sources, rebuilds the current CI image, builds/stages inside that immutable image, and uses it as the runtime base. Conventional image tags are refreshed, but commands use immutable IDs. Host build directories are not mounted.

`task isolated-runtime-check` passed twice. The final runner, including log retention and image tags, passed with evidence in `build/container-runtime.qwh7fo__/verification.log` and `/tmp/files-isolation-runtime-final.log`:

- CI image: `sha256:a89e811aeb3242507318a2a7f6edd371c76db73fcd9fceb4c65493382fb2c8c1`.
- Runtime image: `sha256:961a9593ad3d735e6f4e22dc4f6c84cc7f1ba84a46ea6ca1dce35ee67e59cb92`.
- Installed files, ownership/modes, runtime paths, version, desktop association and three-second desktop launch observation passed as files-test, without network or source mounts.
- Nine corruption fixtures passed against the first repaired image (same installed sources and runtime recipe), including the added unwritable-home diagnostic. Evidence: `/tmp/files-isolation-runtime-fixtures.log`, `build/runtime-fixtures.wr3p52kf/`. CI now runs and retains these fixtures.
- A temporary Git fixture verified snapshot preservation of HEAD, working edits, deletions, untracked files and symlinks, exclusion of ignored build output, unchanged host artifacts, command logging and failure propagation.

## Filesystem isolation

The two rebuilt local binaries passed all 19 cases with `FILES_REQUIRE_FS_ISOLATION=1`.
The snapshot at `build/container-runtime.it8l_rht/work` was built using the same CI image for container checks. Only the two changed test sources were subsequently refreshed for stderr diagnostic verification.

Docker checks use an ordinary UID, no network, and only the disposable snapshot mount:

- Default policy plus required mode: CTest exits 8; both binaries report unavailable isolation and `unshare(CLONE_NEWUSER|CLONE_NEWNS): Operation not permitted` on stderr.
- Default policy without required mode: both binaries retain the existing 16 + 3 optional skips.
- Seccomp disabled plus required mode: all 16 + 3 cases pass, with no skips.

Evidence: `/tmp/files-isolation-{denied,optional,enabled}-final.log`. The dedicated CI matrix job preserves the existing `build` check name, runs the isolation job separately, and fails if namespaces are unavailable. Its AppArmor profile is selected only for that test container; no host sysctls or Docker daemon defaults change.

## Repository acceptance

`task check` passed outside the sandbox: 21/21 CTest entries, formatting, full clang-tidy, QML lint, REUSE, staged installation, import policy and generated metadata. Evidence: `/tmp/files-isolation-check-unsandboxed.log`. The initial sandbox run failed only the existing Unix-socket fixture because bind was denied. The successful smoke run had 689 passing cases and seven existing native/performance/environment skips.

After replacing Qt logging with direct C++23 stderr output in the two tests, focused builds/tests, formatting and targeted clang-tidy were rerun. Runtime production payloads are unaffected by that diagnostic-only change.

Workflow YAML and every embedded shell block parse successfully. Python compilation, shell syntax and `git diff --check` pass. Final documentation licensing is checked separately.

## Limitations

Hosted CI and its AppArmor-enabled Ubuntu branch have not run; this local host does not provide that environment. The profile follows Ubuntu's documented named-unconfined profile with explicit userns permission. The required-isolation check will fail visibly if the hosted kernel imposes another restriction. Native/GPU acceptance was not performed or needed for these infrastructure/test changes. Branch-protection configuration was not changed.
