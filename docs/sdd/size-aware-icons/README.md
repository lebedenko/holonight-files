# Explicit icon rendering in Files

Baseline: `1cc5b2f4390643b388462e5eafa1e46be9735f61`.

Keep Files' current icon choices. Its own `image://icon/` provider continues to supply original theme artwork to Places, listing, and preview views; bundled fallback glyphs continue through semantic rendering. Migrate `HnIcon` callers to explicit rendering and update presentation assertions. Sidebar symbolism and file-view presentation changes belong to later work.

Implementation: the Places, listing, preview, and Quick Look QML views plus `tests/smoke.cpp`. Local verification (2026-09-25): focused icon tests passed (15); the full CTest set passed (27) outside the sandbox for socket and OpenGL access. QML lint, format, import, type metadata, install, license, and tidy checks passed against the local provider build.
