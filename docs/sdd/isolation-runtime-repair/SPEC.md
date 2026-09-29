# Isolation and runtime repair

Approved by the user's “proceed” on 2026-09-29, following the investigation and proposed fixes.

- REQ-F-001: The runtime image shall provide a writable private home for its unprivileged verifier.
- REQ-F-002: If runtime verification fails, then the verifier shall identify the failing check.
- REQ-F-003: When local isolated runtime acceptance runs, the task shall build providers and Files against the same image used for runtime acceptance.
- REQ-F-004: The CI isolation job shall require successful namespace setup for both filesystem test executables.
- REQ-C-001: The local container task shall preserve existing host build artifacts and source edits.
- REQ-C-002: The runtime verifier shall run without host mounts or networking as files-test.

Ordinary developer tests may still skip unavailable namespaces. Publication and umbrella changes are excluded.
