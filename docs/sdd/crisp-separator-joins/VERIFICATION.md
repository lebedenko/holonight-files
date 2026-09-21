# Verification — 2026-09-21

## Runtime identity

The initial direct `hn-files` launch resolved the system Holonight modules, despite a separate local
provider installation. QML import tracing and `/proc/<pid>/maps` identified
`/usr/lib/qt6/qml/Holonight/Controls/libholonight_controls_qml.so`, SHA-256
`c40cf32cc8a22276b6a1a23a7b73011f69cd529fc95b33b06679791f54e602d2`.
That baseline trace used the offscreen software backend and QT_SCALE_FACTOR=1.25. Old Files wide/narrow
captures were also taken, but do not reproduce the original native screenshot failure conclusively.
No claim of native baseline reproduction is made from those offscreen images.

The final real executable was launched offscreen with the explicit local prefix, QT_SCALE_FACTOR=1.5625,
QML_IMPORT_TRACE=1, QSG_INFO=1, QT_QUICK_BACKEND=rhi and QSG_RHI_BACKEND=opengl. Its trace reported
OpenGL / Mesa Intel(R) Graphics (RPL-S), Mesa 26.2.3, Qt 6.11.2, without binding-loop warnings. The process
maps confirm these libraries (paths are relative to the umbrella except the system platform-theme plugin):

```text
5638fefe837161f2fc27ce4a9eb64643140e6446399c08ffb67b792c252ca775  holonight-files/build/deps/prefix/lib/libholonight_config.so
02c505b30ba323e66f947119add7f89970bdb7c76defb1c9ab6270aca724ef3a  holonight-files/build/deps/prefix/lib/qt6/qml/Holonight/Controls/libholonight_controls_qml.so
2f1d897bb138f1202d3433589847ac426a2ecbed40bfcccdc8880a674fa06741  holonight-files/build/deps/prefix/lib/qt6/qml/Holonight/Core/libholonight_core_qml.so
d8688d59dde3395de8a7d73abf24842e1057e30d72ebc09eade72a43d783dcc3  holonight-files/build/deps/prefix/lib/qt6/qml/Holonight/impl/libholonight_impl_qml.so
c81fb7d9e06f192db827ad2d7c2a9e65dce8b37de3ba78f2f6726c3da955aa7b  holonight-files/build/deps/prefix/lib/qt6/qml/Holonight/libholonight_qml.so
f473b45148b9c68fddb38dbaf31c8227937782a31e8698b54568e3b3690c4ce1  /usr/lib/qt6/plugins/platformthemes/libqholonight.so

```

The system platform-theme plugin remains loaded; the separator implementation comes from the explicitly
staged Controls library above. Import-trace search candidates alone do not identify the loaded binary.
The automated application test separately asserts effective DPR and graphics API at all six scales.

## Rebuild and launch

From the umbrella root, explicitly rebuild and stage the dirty provider; the dependency helper's HEAD
freshness check cannot detect these uncommitted source edits:

```sh
cmake --build holonight-files/build/deps/holonight-qt -j4
cmake --install holonight-files/build/deps/holonight-qt
cmake --build holonight-files/build/debug -j4
QML_IMPORT_PATH="$PWD/holonight-files/build/deps/prefix/lib/qt6/qml" \
LD_LIBRARY_PATH="$PWD/holonight-files/build/deps/prefix/lib" \
QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl QT_SCALE_FACTOR=1.25 \
holonight-files/build/debug/apps/files/hn-files
```

These commands use the existing project-local configured build directories. No system install is needed.
For identity diagnostics, prepend QML_IMPORT_TRACE=1 and QSG_INFO=1, and inspect the process's mapped
Controls library. Do not infer identity from installed QML source text alone.

## Automated acceptance

- Debug, Release and test builds pass; working-tree Files CTest suite: 22/22.
  The publication snapshot excludes the unrelated dependency-refresh test and passes a clean build,
  QML lint/metadata and all 21/21 remaining tests, including both six-DPR renderer matrices.
- Software and real Intel OpenGL junction tests pass at DPR 1, 1.25, 1.5, 1.5625, 1.75 and 2.
  Window widths 1000/850/740/420/1000 cover resizing and hidden-column states. Complete junction regions
  check shared endpoints, clipping, adjacent coverage and half-opacity overlap, not midpoint colors alone.
- Provider tests cover physical thicknesses 1–3, all fades, alpha/ancestor opacity, both orientations,
  fractional positions, alignment, signed scale, layout/anchors, explicit slots and zero-value recovery.
- DPR 1.5625 first reproduced the reported implicit-size binding warnings; separate logical-thickness
  notification fixes them. Both provider and application tests fail on binding-loop warnings.
- The additional DPR exposed a header pixel hidden by an adjacent background. Explicit header stacking
  fixes that composition issue; all twelve Files renderer/DPR processes pass after the correction.
- Formatting, focused final clang-tidy, QML lint, QML policy/metadata, REUSE licensing and staged Release
  installation pass. The full tidy run was inspected and its new test diagnostics corrected; remaining
  task-check stages were then completed individually. Sandbox-denied socket tests were rerun with access.

Reproduce the rendering matrix with:

```sh
cmake -S holonight-qt -B holonight-qt/build -DHOLONIGHT_ACCELERATED_SEPARATOR_TESTS=ON
cmake --build holonight-qt/build -j4
ctest --test-dir holonight-qt/build --output-on-failure -R separator_rendering
cmake -S holonight-files -B holonight-files/build/test -DFILES_ACCELERATED_SEPARATOR_TESTS=ON
cmake --build holonight-files/build/test -j4
ctest --test-dir holonight-files/build/test --output-on-failure -R files-separator
```

The opt-in accelerated tests require actual GPU access and reject software fallback. They use a public
QQuickRenderControl/FBO to avoid unreliable fractional offscreen window-buffer capture. Platform DPR
change is simulated with the public event in provider tests; no physical monitor transition is claimed.

## Native observation

The user reported clean native joins with QT_SCALE_FACTOR=1.25 and monitor scale 1.25 (combined DPR 1.5625).
After staging the notification fix, the user relaunched and explicitly confirmed that warnings were gone.
After the final header stacking correction, the user relaunched again and confirmed: “Joins look clean;
no warnings.” Native acceptance and the full automated junction matrix are complete. No pointer or focus
automation was used. The scale-1 capture is a visual reference, not a portable pixel-identical golden.

The user subsequently authorized committing, publishing and pinning the accepted implementation.
The umbrella ledger records the published revisions and CI snapshot separately from these local results.
