# Shared SVG support — verification

Date: 2026-09-24. Local implementation acceptance; no consumer publication or hosted CI claimed.

## Accepted providers and environment

- Images: `3da5f4e51fe2eed9a1bd9f72c0ab523aa57a5ceb`, published and pinned before adoption.
- Qt: `863af4183bdf09ce05199b37e8f5dfb46a311ba1`.
- Config: `fe69a59e6b73167fd5349223a4d265d75386c139`.
- GNU 16.2.1, Qt Core/Svg 6.11.2, libexif 0.6.26, libwebp 1.6.0.
- `JOBS=2 task deps` refreshed the project-local provider prefix.

## Automated checks

- Focused regressions: PreviewService.*Svg*:ThumbnailService.*Svg* (initial 7/7 passed); added fractional Quick Look, input-budget and stale-result cases passed in full CTest.
- Full `task check` passed (exit 0): CTest 27/27, formatting, clang-tidy, QML lint, REUSE,
  staged installation, QML import policy and QML type metadata.
  Initial sandbox attempts could not create required private sockets/D-Bus/OpenGL contexts;
  the authorized unsandboxed rerun is the acceptance run (`build/svg-task-check-unrestricted.log`).
- Separate clean Release acceptance passed without compiler warnings:

```sh
cmake -S . -B build/svg-clean-acceptance -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF \
  -DCMAKE_INSTALL_PREFIX=/usr -DCMAKE_INSTALL_LIBDIR=lib \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps/prefix" \
  -DQML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml"
cmake --build build/svg-clean-acceptance --parallel 2
```

  Complete log: `build/svg-clean-acceptance.log`.
- Isolated installed-payload runtime acceptance passed: `bash scripts/prepare-runtime-check.sh`,
  then `docker build --network none` with its generated context and
  `docker run --rm --network none holonight-files-svg-runtime`.
  Container Qt/image library versions match the local build. Logs:
  `build/svg-runtime-prepare.log`, `build/svg-runtime-build.log`, `build/svg-runtime-test.log`.
  These installed-runtime checks exercise the existing packaged launch/format contract;
  the new SVG behavior is covered by the local CTest regressions.

## Native acceptance and publication

Native visual acceptance passed, reported by the user on 2026-09-24 after the requested application checks:
"I've done manual check, SVGs rendered correctly in both applications."
No pointer/focus automation was used. The umbrella remains Accepted until published exact-revision
integration and manual ecosystem verification are complete.
