# Shared image maintenance

Approved scope: supplied implementation plan. Work package M-002.
Baseline: `86e3f4a34a3b190f59d64f1c3e3a5584f98cd646` (clean HEAD and umbrella pin verified 2026-09-23).

## Requirements

- When opted in, tooling shall add versioned measurement metadata and strict five-trial JSON/Markdown comparisons, then run fresh identical-production baseline/candidate datasets.
- Tooling shall preserve production APIs, behavior, codecs and cache policy.
- Verification shall retain raw evidence and failures outside tracked source.
- Publication and umbrella pins shall wait for explicit authorization.

See [design](DESIGN.md), [tasks](TASKS.md) and [verification](VERIFICATION.md).
