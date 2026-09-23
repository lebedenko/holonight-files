# Sharp previews requirements

The user-approved implementation plan authorizes this cycle (2026-09-15).
Scope: image sizing/cache and publication; metadata, text and QML interfaces remain.
No migration/deletion, other-app cache relaxation or new media support.

- REQ-S-001: When a consumer reports physical pixels (including DPR), the system shall aspect-fit to the source, cap at native resolution and select the smallest containing 128/256/512/1024 tier.
- REQ-S-002: When previewing an image, the system shall check adequate same-revision memory, selected disk tier, then progressively larger disk tiers, validating actual dimensions, URI, mtime, size and Files revision.
- REQ-S-003: If no adequate cache exists, the system shall decode only the selected tier and save atomically with private permissions; write failure shall not prevent display. Above 1024px, it shall decode bounded direct dimensions without disk writes. If dimensions cannot be established, the shared provider shall reject the source as Damaged before codec pixel decoding, with empty pixels and no disk cache publication.
- REQ-S-004: When selection changes, the system shall clear old pixels immediately and publish only adequate pixels. When the same selection grows, it shall retain pixels until an upgrade and never downgrade. EXIF completion shall not replace pixels.
- REQ-S-005: The system shall preserve two-entry/64 MiB memory retention, decode limits, verified-descriptor reads, cancellation, generation rejection, debounce, timeout and metadata/text behavior.

This cycle supersedes inspect-selection REQ-F-017/019/020 normal-only cache
acceptance, REQ-R-004 thumbnail-first publication and REQ-R-005 exact-size/normal-only
cache rules. REQ-F-005 exact decoded-size acceptance now permits adequate larger tiers.
Native scaling/navigation/resize/Quick Look and cold/warm latency checks remain
acceptance gates; unavailable checks must remain pending.

2026-09-23 amendment: the previous unknown-size bounded-decode wording predates
shared Images policy. [Synthetic runtime acceptance](../unknown-dimension-acceptance/SPEC.md)
verifies rejection, not additional format support; historical verification is preserved.
