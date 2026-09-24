# Shared SVG support — Tasks

Status: Done (local acceptance) against published/pinned Images 3da5f4e51fe2eed9a1bd9f72c0ab523aa57a5ceb.

- [x] Implement explicit validated SVG dispatch on verified descriptor with vector result facts.
- [x] Make DPR size/dispatch/adequacy/resize logic vector-aware and skip EXIF.
- [x] Add SVG policy marker to existing thumbnail tiers and validate before every cache lookup.
- [x] Test 24x24 enlargement, fractional DPR, resize loops, caches/invalidation, external resources and stale results.
- [x] Confirm raster regressions; run focused tests, task check and isolated runtime acceptance.
- [x] Record manual preview-pane/Quick Look checks; commit locally and request authorized publication.

Automated evidence is recorded in [VERIFICATION.md](VERIFICATION.md). Native visual acceptance passed: the user
confirmed on 2026-09-24 that SVGs rendered correctly in both applications. Consumer publication and umbrella pin
updates require a separate authorized handoff.
