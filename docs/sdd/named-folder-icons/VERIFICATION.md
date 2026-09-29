# Named-folder icons verification

## Manual acceptance — 2026-09-29

The user confirmed: "I did manual checks, no visual issues, works as expected."
This records user-performed visual acceptance; no native desktop interaction was automated.

## Review baseline

- 127 focused tests passed using the existing test binary: `PlaceList.*`, `IconNameResolver.*`,
  `DirectoryModel.*`, `PlacesModel.*`, `PreviewService.*`, and
  `Files.NamedFolderFallsBackToTheFolderGlyphWhenTheThemeLacksItsIcon`.
- `cmake --build build/test --target qml-lint` passed.
- `git diff --check` passed.

## Repeated-failure regression

`IconImageProvider.RepeatedNamedFolderRequestsDoNotInflateLookupsOrWarnings` exercises
`folder-documents/folder/inode-directory` 100 times at a fixed size with both an empty theme
and a theme containing only `folder`. It verifies the result for every request, no additional
theme lookups after the initial request, and zero Qt warnings. The existing window test covers
the QML fallback glyph and `IconFallbacks` registration separately.

Validation on 2026-09-29:

- `task deps`: passed; provider revisions current.
- `task build PRESET=test`: passed (configure and build); final include formatting was rebuilt with
  `cmake --build --preset test`.
- Focused regression command below: all 7 tests passed.
- `clang-format --dry-run --Werror tests/icon_image_provider_test.cpp`: passed.
- `git diff --check`: passed.

```sh
QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software \
  QML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml" \
  LD_LIBRARY_PATH="$PWD/build/deps/prefix/lib" \
  build/test/tests/files-smoke \
  --gtest_filter='IconImageProvider.*:Files.NamedFolderFallsBackToTheFolderGlyphWhenTheThemeLacksItsIcon'
```

Full `task check` and isolated runtime acceptance were not rerun during this review and test-only fix;
the focused results above do not establish those broader checks. No publication was performed.

## Remote integration and publication checks — 2026-09-30

Merged `origin/main` at `dd31d10` without conflicts. The new thumbnail dependency was checked out
at the CI-pinned `d27addc044f277686850588147ec825c40c0f252` under
`build/deps/sources/holonight-thumbnails`; checks use `HOLONIGHT_THUMBNAILS_SOURCE` to select it.

The full suite exposed a stale column-layout test from the earlier 32 px icon change: it still
expected a 20 px icon and used window widths sized for that icon. Updated the size assertion and
width fixtures, preserving checks for all three column visibility states, minimum name width,
and header/delegate alignment. The focused layout test passed, followed by all 21 CTest checks
outside the sandbox. The sandbox blocks the Unix-socket fixture, so that environment cannot run
the complete suite successfully.

Isolated runtime acceptance passed on the merged production sources before the test-only correction,
including the staged desktop launch. Evidence: `build/container-runtime.nvuv0ieg/verification.log`.

The final `task check` run passed builds, all 21 CTest checks, and formatting, then stopped on
three test-only clang-tidy findings. Corrected the fixture member spelling, moved the warning
counter into a function-local static, and named the unused callback parameters in comments.
All other translation units passed that lint run; rerunning `run-clang-tidy` on the two corrected
files passed. Rebuilt tests and reran the affected model/provider and window regressions:
40 tests passed. Completed the remaining `task check` stages individually with
`task format-check qml-lint license-check install-check qml-import-check qmltypes-check`; all passed.
Thus every check stage passed, though not in one uninterrupted invocation.
