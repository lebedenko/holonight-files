# Design

Explicitly create the private home with correct ownership in both Dockerfiles. Add an ERR diagnostic to the runtime verifier.

Replace the host-build runtime task with a Python orchestrator. Snapshot each repository's current tracked and nonignored untracked files into a fresh build subdirectory, retaining HEAD via a no-checkout local clone. Build the current CI Dockerfile, capture its immutable image ID, and build/stage providers plus Files inside it as the invoking UID. Build the runtime image FROM that same immutable ID. Retain snapshots and logs for diagnosis. Never mount the live source or host build directories into the build container.

Use a separate CI matrix job for filesystem isolation. Build its two executables under ordinary Docker restrictions. Run only those executables in an unprivileged, network-disabled container with seccomp disabled so user/mount namespaces work; leave other checks and runtime acceptance under the default policy. No host Docker socket or privileged container is needed. A required-isolation environment flag makes unavailable namespace setup fatal. Host LSM restrictions can still fail the dedicated job explicitly rather than silently skipping coverage.

On AppArmor-enabled hosted runners, load a named test-only profile with the unconfined flag and explicit userns permission, and select it only for that container. Docker's default profile denies mounts, and Ubuntu restricts unnamed unconfined user namespaces. This follows [Ubuntu's documented profile form](https://discourse.ubuntu.com/t/ubuntu-24-04-lts-noble-numbat-release-notes/39890) without changing host sysctls or daemon defaults. Local Arch verification cannot establish hosted AppArmor acceptance.

Focused acceptance: both binaries pass with isolation required; default Docker denies setup and the required flag fails; namespace-enabled Docker passes all 19 cases. Runtime acceptance must pass the installed verifier and corruption fixtures. Run task check after focused regressions. Hosted CI remains unverified until publication.
