# Unknown-dimension acceptance

Approved by the supplied implementation plan, 2026-09-23. Starting Files revision:
`05fa9f74965fc82d98610a4777ce7a527a941075`; installed Images contract:
`3633865d2f39e4f163f0159a0f252f88245379f0`.

- R1: When a readable handler cannot establish dimensions, the provider and Files shall return Damaged with empty pixels before codec pixel decoding.
- R2: When this source is previewed or thumbnailed, Files shall publish no thumbnail cache entry and shall recover on selection of a valid image.
- R3: When pre-cancelled, operations shall return Cancelled without pixels, decoding, cache publication or a presentation error.
- R4: The synthetic handler shall remain test-only and isolated from ordinary plugin discovery and installation.

This is synthetic runtime coverage, not added format support. No production decoder,
public API, limits or codec policy changes. Historical sharp-preview evidence remains.
