# Tasks and verification

| ID | Deliverable | State |
|---|---|---|
| F-001 | Baseline and contracts | Done |
| F-002 | Session extraction and typed commands | Done |
| F-003 | Build boundaries, QML and tooling | Done |
| F-004 | Automated acceptance and staging | Done |
| F-005 | Native interaction review and performance limitations | Done |

## Acceptance — 2026-09-19

- Clean Debug/test and Release configurations built with GCC 16.2.1 and Qt 6.11.2.
  Commands: `CMAKE_BUILD_PARALLEL_LEVEL=6 task build PRESET=test HOLONIGHT_DEPENDENCY_PREFIX=/usr
  HOLONIGHT_QML_IMPORT_PATH=/usr/lib/qt6/qml` and the same command with `PRESET=release`.
  After lint corrections, affected targets were rebuilt. Complete build logs had no compiler warnings/errors.
- External configure/build also passed with `cmake -S . -B /tmp/holonight-files-alignment -G Ninja
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=/usr
  -DQML_IMPORT_PATH=/usr/lib/qt6/qml`. Cache retained caller paths; no project prefix was prepended.
- `ctest --test-dir build/test --output-on-failure`: 9/9 CTest entries passed (43.47 s).
  Main suite: 489 passed, 2 opt-in tests skipped; separate memory benchmark, 16 cross-filesystem tests,
  3 rendered cross-filesystem tests, CLI/startup checks and Fusion editing all passed.
  Real socket/mount-namespace fixtures required running outside the agent sandbox.
- `cmake --build build/test --target qml-lint qml-import-check qmltypes-check format-check`: passed without
  QML warnings. `reuse lint`: passed. Full recursive C++ tidy covered all sources, including places;
  six files with findings were corrected and rerun successfully using `run-clang-tidy -p build/test
  -removed-arg=-mno-direct-extern-access -config-file=.clang-tidy -j 4` and their paths.
  Other source results remain valid; unaffected expensive lint was not repeated.
- `HOLONIGHT_DEPENDENCY_PREFIX=/usr HOLONIGHT_QML_IMPORT_PATH=/usr/lib/qt6/qml
  bash scripts/check-install.sh`: passed with the final Release payload, desktop metadata, MIME registration,
  CLI identity and headless startup. Stage: `build/install-check.Fq4MMP`.
- Reused Config/Qt artifacts at the exact accepted revisions recorded in README: all 14 Config and 142 Qt
  installed regular files matched the umbrella ownership manifest hashes. Provider sources were clean;
  Release build options and Qt package versions matched the runtime image (qt6-base 6.11.2-3,
  qt6-declarative 6.11.2-2, qt6-svg 6.11.2-1). No provider changes were needed.
- Isolated runtime: built `files-ci` from `packaging/Dockerfile.ci`; prepared the payload with
  `HOLONIGHT_CONFIG_BUILD=../.source-install/build/holonight-config` and
  `HOLONIGHT_QT_BUILD=../.source-install/build/holonight-qt` (absolute paths in the actual invocation).
  `docker build -t holonight-files-runtime-check build/runtime-check.ZieZit` and
  `docker run --rm --network none holonight-files-runtime-check` passed, including an observed desktop
  launch. The container ran as files-test without workspace mounts or development search paths.
- User reported all manual native checks passed: rename/create/search focus and cancellation, Quick Look
  Escape before fullscreen Escape, dark/light appearance, fractional scaling and minimum-window behavior.
  No native pointer/focus interaction was automated by the agent.
- The opt-in native test and rendered performance benchmark were not run automatically. Native functional
  checks are covered by the user's report; controlled native performance/timing measurement remains unavailable.
  The copy-memory benchmark did run and pass. No new performance claim is made.

## Regression and review notes

The baseline touch-atime test also failed on untouched bb34b9a: navigation/validation could dereference a
symlink after its timestamp was established. Its setup now uses the existing pre-commit hook so it measures
UTIME_OMIT at the commit boundary; no production filesystem algorithm changed. New regressions cover duplicate
worker completion and repeated shutdown, idempotent independent engine providers, and style-independent inline
validation and commit. Existing mode/history/operation/preview regressions remain in place.

The directory model/proxy, file-operation/trash/task workers and preview/thumbnail/text/EXIF implementations
were compared byte-for-byte with the baseline after moving and remain unchanged. The formerly omitted places
parser received only lint-driven member naming/designated-initializer corrections.

System installation/removal is owned by the umbrella. Standalone privileged tasks and their obsolete removal
fixture were retired; standalone build and DESTDIR staging remain supported. The umbrella's Files ownership
fixtures verify collision refusal and modified-file preservation. Publication and umbrella gitlink registration
remain separate authorized steps; this local acceptance is not an Integrated umbrella state.
