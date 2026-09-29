# Requirements

- **REQ-F-001:** When a raster preview misses the worker memory cache, Files shall use the shared thumbnail disk cache before decoding the verified source descriptor.
- **REQ-F-002:** When a source decode succeeds at a supported cache tier, Files shall offer the pixels to the shared cache without treating cache write failure as preview failure.
- **REQ-F-003:** Files shall accept external raster thumbnails with valid freedesktop metadata and dimensions, and shall reject conflicting HoloNight metadata when present.
- **REQ-F-004:** Files shall validate SVG resource policy before memory or disk lookup and shall reuse only policy marked SVG cache entries.
- **REQ-F-005:** Files shall retain source inspection and decoding, cancellation, preview stages, and its two-entry worker memory cache.
- **REQ-F-006:** Requests above the 1024-pixel cache tier shall continue through source decoding without disk caching.
