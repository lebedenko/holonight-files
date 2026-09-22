# Tasks

- [x] Inspect baseline and inventory fixtures (R1–R6).
- [x] Record contract and design under authorized roadmap.
- [x] Implement owned provider or consumer migration (R1–R6).
- [x] Run focused regressions, clean build and required checks.
- [x] Review diff and record evidence and remaining integration actions.

## Verification — 2026-09-22

Provider: `efe3e780327fa793fb76c82b18fddde15298120b`; Qt provider: `863af4183bdf09ce05199b37e8f5dfb46a311ba1`; config: `fe69a59e6b73167fd5349223a4d265d75386c139`.
GCC 16.2.1, Qt 6.11.2, Release dependencies with BUILD_WAYLAND=OFF, installed package linked through the project-local dependency prefix.

- Clean Release configure/build in `build/images-acceptance` passed without compiler warnings; affected units rebuilt after corrections.
- Debug, Release and test preset builds passed.
- `task check` was attempted; remaining components were continued individually after correcting the issues below, without repeating unaffected expensive checks.
- `QMLFORMAT=/usr/lib/qt6/bin/qmlformat task format-check` passed. The default `/usr/bin/qmlformat` is a different binary and returned failure on unchanged QML; the existing override selects Qt 6's formatter.
- Full `tidy` scan plus affected-file rechecks with repository configuration passed after brace/initializer corrections.
- `task qml-lint`, `task license-check`, `task install-check`, `task qml-import-check`, `task qmltypes-check` passed.
- Updated dependency preparation and runtime staging scripts passed `bash -n`.
- CTest: 21/22 initially passed; `files-provider-revisions` passed after adding a three-provider fixture. The original untracked `tests/provider_revisions_test.sh` remains untouched; the new tracked fixture covers config, Qt and images refresh behavior.
- Focused EXIF/thumbnail/preview/decode/stale-result suite: 66/66 passed; final EXIF/thumbnail/decode-limit recheck including stored-orientation regression: 26/26 passed.
- Isolated installed runtime passed in `holonight-files-images-runtime` with no workspace mount/network. Rebuilt `files-ci` from the updated Dockerfile because the old cached base lacked libwebp; final desktop launch passed.

Implementation is locally verified. Publication, canonical remote availability, umbrella pinning and final native ecosystem checks remain pending; this is not an Integrated initiative.
