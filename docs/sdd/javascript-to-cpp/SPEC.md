# JavaScript to C++ specification

Approved by the user’s implementation-plan instruction on 2026-09-16.
Implementation and automated verification are complete; native visual acceptance
remains pending in [verification](VERIFICATION.md).

- **REQ-001:** When a byte size is formatted, SizeFormat shall return empty text for
  negative values, integer bytes below 1024 and one decimal above, dividing by 1024
  with KB/MB/GB/TB labels capped at TB, decimal points and SizeFormat translations.
- **REQ-002:** When an icon chain fails, IconFallbacks shall retain the exact chain
  for the QML engine lifetime, sharing it within that engine and isolating engines.
  Queries shall remain nonreactive; deferred QML failedChain updates shall remain.
- **REQ-003:** When an inspection key is pressed, InspectionKeys shall normalize
  Space/Escape/Return/Enter, otherwise use text, preserve popup Ctrl+O/I precedence,
  restrict popup keys to Space/Escape/j/k and consume repeated Space without dispatch.
  A null controller shall perform no dispatch.
- **REQ-004:** When Space is released, the helper shall consume it. When a shortcut
  override is requested, it shall consume Space and Escape in popup or VISUAL mode.
  QML shall only set acceptance when the helper returns true.
- **REQ-005:** The HolonightFiles module shall expose the three C++ singletons and
  contain no standalone JavaScript helpers or active imports/resource registrations.
  Embedded QML handlers, focus, shortcuts and image-provider cache shall be preserved.
- **REQ-006:** Automated tests and required build/quality/runtime checks shall verify
  behavior; unavailable native visual acceptance shall remain explicitly pending.

No new features, keyboard redesign, cache policy changes or sibling-source edits.
