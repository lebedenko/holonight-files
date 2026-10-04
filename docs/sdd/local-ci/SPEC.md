# Requirements

- R1: When task ci or push CI runs, Files shall verify identical current-source
  snapshots with pinned tools and all six exact provider revisions.
- R2: The build lane shall retain full task check as files-test in C.UTF-8,
  additional en_US.UTF-8 tests, installed-runtime verification and all nine
  healthy/negative runtime fixtures.
- R3: The isolation lane shall build both filesystem executables separately and
  require user/mount namespace coverage in an offline unprivileged invocation.
- R4: Each lane shall start with fresh application/provider build state, preserve
  source read-only and report non-ignored new inputs before publication.
- R5: A failed or unavailable required check shall fail task ci; logs, revision,
  dirty state, image/tool/provider identity and results shall remain host-owned
  under ignored build/ci. Docker shall be preferred, with Podman fallback.
- R6: Licensing shall use REUSE 6.2.0. Existing quick development and specialized
  acceptance tasks shall remain available. No publication or pin changes.
