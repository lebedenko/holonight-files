# Design

Explicitly create the private home with correct ownership in both Dockerfiles. Add an ERR diagnostic to the runtime verifier.

Replace the host-build runtime task with a Python orchestrator. Snapshot each repository's current tracked and nonignored untracked files into a fresh build subdirectory, retaining HEAD via a no-checkout local clone. Build the current CI Dockerfile, capture its immutable image ID, and build/stage providers plus Files inside it as the invoking UID. Build the runtime image FROM that same immutable ID. Retain snapshots and logs for diagnosis. Never mount the live source or host build directories into the build container.

Use a separate CI matrix job for filesystem isolation. Build its two executables under ordinary Docker restrictions. Run only those executables in an unprivileged, network-disabled container with seccomp disabled so user/mount namespaces work; leave other checks and runtime acceptance under the default policy. No host Docker socket or privileged container is needed. A required-isolation environment flag makes unavailable namespace setup fatal. Host LSM restrictions can still fail the dedicated job explicitly rather than silently skipping coverage.

On AppArmor-enabled hosted runners, load a named test-only profile with the unconfined flag and explicit userns permission, and select it only for that container. Docker's default profile denies mounts, and Ubuntu restricts unnamed unconfined user namespaces. This follows [Ubuntu's documented profile form](https://discourse.ubuntu.com/t/ubuntu-24-04-lts-noble-numbat-release-notes/39890) without changing host sysctls or daemon defaults. Local Arch verification cannot establish hosted AppArmor acceptance.

Focused acceptance: both binaries pass with isolation required; default Docker denies setup and the required flag fails; namespace-enabled Docker passes all 19 cases. Runtime acceptance must pass the installed verifier and corruption fixtures. Run task check after focused regressions. Hosted CI remains unverified until publication.

### Consumer acceptance restoration

The portable/host split is defined by exhaustive executable inventories in
`tests/lanes.json` and `tests/unknown-dimensions-lanes.json`. Test-only CLI runtime
inputs are passed through existing constructors and thumbnail/metadata arguments;
production runtime selection is unchanged. Direct metadata scanner read-budget
and device-error assertions remain portable against the installed provider API:
the parent's isolated input capture necessarily reads bytes that the worker's
metadata scanner can seek over. Consumer decoding and cancellation stay in the
required host lane.

Unknown-size coverage uses a private-signature TIFF test plugin in a fresh,
non-installed bundle prepared by the unchanged provider tooling. A known-size
variant proves the worker loaded that codec. The unknown variant exits 86 if
pixel decoding is attempted; a portable subprocess independently proves the
trap. Host inspection, decoding and thumbnail assertions require successful
runtime completion and `Damaged`, with no pixels/cache, followed by PNG recovery.

`task test:isolated` requires explicit production and synthetic runtimes. The
umbrella `scripts/check-isolated-previews.py` publication gate also checks rendered
consumers, installed-provider acceptance and relocation with the original prefix
unavailable. See `CONTRIBUTING.md` and the umbrella isolation note for exact inputs.
