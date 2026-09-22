# Design

The failed orientation build rejects `/work/holonight-config` during `task deps`.
Git trust configured by checkout on the hosted runner does not describe the container's
`/work` mount paths and does not cover both container users.

Add the four explicit `safe.directory` entries to the disposable container's system
configuration before dependency preparation. `/etc/gitconfig` is visible to root and
`files-test`. Do not add wildcard trust or modify host Git configuration/source ownership.
Keep the existing root dependency preparation, unprivileged `task check`/second locale
pass/runtime staging, and exit trap that restores ownership of build artifacts.

Verification uses separate local clones of the exact baseline revisions mounted at the
workflow paths. Execute the workflow's extracted shell body unchanged, with only parallel
build limits supplied as environment variables. A separate read-only mount probe changes
only the container test user's UID to 1001: both users must reject all four repositories
before the trust loop and resolve their exact revisions afterward. This avoids dependence
on whether the runner checkout owner happens to match the default test-user UID 1000.

Use the existing `files-ci` image and stage the installed payload for the existing isolated
runtime image. No application, provider revision policy, public API or dependency changes.
Hosted CI is not rerun or represented as passing before publication.
