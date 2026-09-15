# Design

Approved through the supplied implementation plan.

First rename the CMake executable and all active invocation/check references
(R1, R5). Retain applicationName and project/package identity. Uninstall removes
the new and legacy binaries before metadata, preserving fail-fast sequencing
(R2). Next register the existing local single-folder CLI in the stable desktop
entry using a literal end-of-options marker (R3, R4).

Extend staged payload checks to reject the old binary and assert desktop fields.
Use an isolated XDG data/config tree to refresh and query MIME registration.
Extend the container desktop launch observer to pass a folder with spaces and
Unicode and inspect its NUL-separated process arguments. Existing startup tests
cover no-argument and invalid-directory behavior. Keep install's database refresh;
never set a default application.

Update README/backlog and retain all generated evidence under build/folder-handler
or existing build check directories (R6). Use installed provider packages; task
deps builds providers into build/ without editing their sources.

Implementation and automated verification complete; native/host acceptance remains
pending in [verification](VERIFICATION.md).
