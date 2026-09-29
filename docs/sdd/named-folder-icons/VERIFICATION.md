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
