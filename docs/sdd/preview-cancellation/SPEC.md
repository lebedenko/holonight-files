# Requirements

- R1: When a preview is cancelled, Files shall pass that request's token through
  thumbnail inspection and bounded decoding, terminating silently on cancellation.
- R2: When cancellation is observed, Files shall avoid subsequent cache tiers,
  source decoding, memory-cache insertion, publication and metadata work.
- R3: When cancellation is observed before cache preparation, encoding or commit,
  Files shall abandon the temporary write and preserve any existing cache entry.
- R4: Files shall preserve verified source descriptors, orientation, tags, tiers,
  fitted bounds, memory-cache policy, generation checks and timeout presentation.
- R5: Acceptance shall include deterministic real-image regressions, red/green
  evidence, five fresh processes per performance scenario on baseline/candidate,
  clean Release, task check and isolated installed-runtime verification.

Cancellation is cooperative. Active codec/filesystem calls and a commit already
started may finish. A committed valid entry is never removed to simulate cancellation.
No scheduling, QML, provider API or cache-format changes are in scope.
