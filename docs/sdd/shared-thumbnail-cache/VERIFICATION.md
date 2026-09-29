# Verification

Verified on 2026-09-29 against `holonight-thumbnails`
`d27addc044f277686850588147ec825c40c0f252`. The Files dependency prefix
recorded that exact revision.

- Focused thumbnail, SVG, orientation and cancellation cases: 55 passed after the final descriptor correction.
- `ctest --preset test --output-on-failure`: 27/27 passed outside the sandbox. The preceding `task check` run stopped at sandbox-only Unix-socket and offscreen OpenGL failures; both failure types passed outside the sandbox. The subsequent correction only makes cache lookup miss when descriptor metadata is unavailable; its affected 55 tests were rerun.
- `task format-check`, `task lint` (clang-tidy and QML lint), `task license-check`, `task install-check`, `task qml-import-check`, and `task qmltypes-check`: passed. REUSE needed execution outside the sandbox because its worker could not bind a local socket there.
- `task isolated-runtime-check`: passed outside the sandbox after updating the helper to include the thumbnail provider and use the locally built `files-ci` tag for the final Docker image. Evidence: `build/container-runtime.42is1bcg/verification.log` (untracked build output). The container built and staged the exact provider and Files sources, then verified desktop launch.
- Final diff check and staged diff review: passed.

The container build emitted only existing Qt private-module compatibility notices
from `holonight-qt` and generic unused CMake option notices from dependency
preparation. No actionable compiler warnings appeared.

Manual native Files/Viewer raster and SVG reuse remains pending for user operation during umbrella integration.
