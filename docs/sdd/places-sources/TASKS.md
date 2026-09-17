# SDD Tasks — places-sources

Commands below use Google Test filters, not CTest test-name filters: these suites
all belong to the single `files-smoke` CTest test. Run them with
`QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software` and the installed HoloNight
`QML_IMPORT_PATH`/`LD_LIBRARY_PATH` (see [VERIFICATION.md](VERIFICATION.md)).
Checked implementation tasks do not certify every original acceptance assertion;
the verification record identifies the evidence actually available.

- [x] T-001: XdgPaths additions (userDirsFilePath, dataDirPath, placesFilePath)
  - REQs: REQ-C-001
  - Check: `build/test/tests/files-smoke --gtest_filter='XdgPaths.*'` passes with new cases asserting `userDirsFilePath()` is `<config>/user-dirs.dirs` (no `holonight-files` segment) and `placesFilePath()` is `<data>/holonight/holonight-files/places.toml`, for unset, empty, relative and absolute `XDG_CONFIG_HOME`/`XDG_DATA_HOME`.

- [x] T-002: UserDirsParser (pure parser, labels/icons table) + new `apps/files/places/` wiring
  - REQs: REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-010, REQ-C-009
  - Wiring: add `apps/files/places/` sources to `apps/files/CMakeLists.txt`, `tests/user_dirs_parser_test.cpp` to `tests/CMakeLists.txt`, and `apps/files/places/*` to both `format` and `format-check` globs in `Taskfile.yml`.
  - Check: `build/test/tests/files-smoke --gtest_filter='UserDirsParser.*'` passes covering all 9 keys in display order from a shuffled file, `$HOME/…` and absolute forms, `\"` `\\` `\$` `` \` `` unescaping, `$HOME` and `$HOME/` treated as disabled, junk/unknown/unquoted lines ignored, a missing file yielding no entries, and the source file's mtime unchanged after parsing.

- [x] T-003: TomlDocument array-of-tables accessors
  - REQs: REQ-C-003
  - Check: `build/test/tests/files-smoke --gtest_filter='TomlDocument.*'` passes with new cases for `arrayOfTablesSize/Value/Keys/Line` (present, absent, non-array key, out-of-range index), and the existing `TomlLibraryIsIncludedOnlyByTheAdapter` test still passes.

- [x] T-004: BookmarkStore reader
  - REQs: REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-011, REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-015, REQ-C-002, REQ-C-007, REQ-C-008, REQ-C-009
  - Note: no comment in `apps/files/places/*` may contain the literal TOML library name (scanned by a test); say "TOML library".
  - Check: `build/test/tests/files-smoke --gtest_filter='BookmarkStore.*'` passes asserting, via `RecordingWarningSink`, the exact message and count for every DESIGN.md §4.2 row (missing file → 0 warnings; syntax error → 1; missing/wrong `version` → 1; missing/non-string `path` → 1 per entry; relative/`$VAR` path → 1 per entry; unknown top-level and in-entry keys → 1 per key with the entry still loaded), `name` defaulting to the base name, `~/` expansion, file order preserved, and no file created when `places.toml` is absent.

- [x] T-005: PlaceAvailabilityChecker interface, Stat implementation, test fakes
  - REQs: REQ-NF-001 (seam)
  - Check: `build/test/tests/files-smoke --gtest_filter='PlaceAvailabilityChecker.*'` passes asserting `StatPlaceAvailabilityChecker` returns true for a readable temp directory and false for a missing path, a regular file, and a mode-000 directory; `FakePlaceAvailabilityChecker` in `tests/directory_fixtures.h` records calling thread per call and supports per-path results and a semaphore gate.

- [x] T-006: PlacesModel rewrite — sources, dedup, roles, startsBookmarks (checks not yet dispatched asynchronously)
  - REQs: REQ-F-001, REQ-F-016, REQ-F-017, REQ-F-019, REQ-F-024, REQ-F-025, REQ-F-026, REQ-F-027, REQ-C-004, REQ-C-007
  - Check: `build/test/tests/files-smoke --gtest_filter='PlacesModel.*'` passes (old `LocationProvider` tests replaced) asserting Home first, XDG in display order, bookmarks in file order, all rows `Checking` immediately after construction, all five roles plus `startsBookmarks` (true only on the first bookmark after a standard row), two XDG keys with the same path keeping only the first with 0 warnings, bookmark duplicates of Home / an XDG place / an earlier bookmark each dropped with exactly 1 warning, and a symlink bookmark plus its target both retained.

- [x] T-007: Async availability dispatch with DeliveryGuard
  - REQs: REQ-F-018, REQ-F-020, REQ-F-021, REQ-NF-001, REQ-NF-002, REQ-NF-003, REQ-C-005, REQ-C-006
  - Check: `build/test/tests/files-smoke --gtest_filter='PlacesModel.*'` passes asserting every checker call ran off the GUI thread, a gated (hung) check leaves its row `Checking` while another row resolves, a 50 ms GUI timer keeps firing while all checks are gated, a failed XDG check emits exactly one `rowsRemoved` and a failed bookmark check exactly one `dataChanged` to `Unavailable`, Home becomes `Available` even when its check returns false, and destroying the model while a check is gated then releasing the gate causes no crash and no delivery (the guard's model pointer is null and no signal is observed).

- [x] T-008: PlacesModel::recheckBookmark with per-place generation
  - REQs: REQ-F-022 (model part), REQ-C-006
  - Check: `build/test/tests/files-smoke --gtest_filter='PlacesModel.*'` passes asserting `recheckBookmark` returns the place id for a bookmark row and 0 for Home/XDG/out-of-range rows, an `Available` bookmark whose directory was removed stays `Available` until rechecked then becomes `Unavailable`, two rapid rechecks of the same place emit `bookmarkRecheckResolved` once (freshest result only), and the returned id stays correct after an earlier XDG row is removed.

- [x] T-009: DirectoryController::activateBookmark and recheck resolution
  - REQs: REQ-F-022, REQ-NF-004, REQ-C-010
  - Check: `build/test/tests/files-smoke --gtest_filter='DirectoryController.*'` passes asserting that activating a bookmark whose directory now exists navigates (`currentPath` equals it, history gains an entry), activating one whose directory is missing leaves `currentPath` unchanged and sets `statusMessage` to "Location is currently unavailable", a navigation performed between activation and resolution suppresses both effects, and two activations add no warnings to the recording sink.

- [x] T-010: PlacesPanel.qml — bookmark activation, Unavailable rendering, accessibility
  - REQs: REQ-F-023, REQ-F-036, REQ-F-037, REQ-F-038
  - Wiring: add `icons/warning-fallback.svg` to `apps/files/CMakeLists.txt` RESOURCES.
  - Check: `build/test/tests/files-smoke --gtest_filter='PlacesWindow.*'` passes with existing cases unchanged plus new cases asserting an `Unavailable` bookmark row has opacity 0.5, a visible warning badge (fallback SVG shown when the theme icon fails), `Accessible.name` equal to its label and `Accessible.description` containing "unavailable", is reachable with Down and activatable with Enter (calling `activateBookmark`), and that bookmark activation is ignored in VISUAL mode, with a task prompt, or with Quick Look open.

- [x] T-011: PlacesPanel.qml — extra gap before bookmarks and preserved layout
  - REQs: REQ-F-028, REQ-F-029, REQ-F-030, REQ-F-031, REQ-F-032, REQ-F-033, REQ-F-034, REQ-F-035
  - Check: `build/test/tests/files-smoke --gtest_filter='PlacesWindow.*'` passes asserting the vertical gap between the last standard row and the first bookmark row exceeds the gap between two bookmark rows by exactly `HnMetrics.internalSpacing(HnControlSize.Compact)`, no `HnSeparator` or "Bookmarks" label exists in the panel, all rows share the same delegate height, the heading uses the muted palette colour, the highlighted row follows `currentPath`, and the list scrolls with the heading visible at 300 px height.

- [x] T-012: README documentation
  - REQs: (documentation of REQ-F-002, REQ-F-007, REQ-F-009, REQ-F-010, REQ-F-018)
  - Check: README.md documents `places.toml` location (with `XDG_DATA_HOME` fallback), a `version = 1` + `[[bookmarks]]` example, accepted path forms, the read-only/manual-edit status, that Places reads `user-dirs.dirs` with no fallback, that missing XDG folders are hidden while missing bookmarks show as unavailable, and that `~/Projects` comes from `XDG_PROJECTS_DIR` or a bookmark.

- [x] T-013: Full verification run
  - REQs: all
  - Check: `task build`, `task test`, `task format-check`, `task tidy` and `task qml-lint` all exit 0 on the final tree.

- [ ] T-014: Manual native visual verification on Hyprland @1.5 (manual)
  - REQs: REQ-F-023, REQ-F-028, REQ-F-031
  - Check: The user confirms on the native 1.5-scale display that Desktop/Templates/Public/Projects rows appear per `user-dirs.dirs`, a bookmark to a missing folder shows muted with a crisp warning badge, the extra gap before bookmarks is visible, and activating that bookmark shows the unavailable message without navigating.

- [x] T-015: Move bookmarks file under an extra `holonight/` segment (post-cycle amendment)
  - REQs: REQ-F-007, REQ-C-001 (path only; `config.toml`/`state.toml` locations are unchanged)
  - Change: `XdgPaths::dataDirPath()`/`placesFilePath()` now resolve to `$XDG_DATA_HOME/holonight/holonight-files[/places.toml]` instead of `$XDG_DATA_HOME/holonight-files[/places.toml]`.
  - Check: `build/test/tests/files-smoke --gtest_filter='XdgPaths.*'` and `build/test/tests/files-smoke --gtest_filter='DirectoryController.*'` / `build/test/tests/files-smoke --gtest_filter='PlacesWindow.*'` pass with the new path; README and SPEC/DESIGN reflect the new location.

- [x] T-016: Review remediation for asynchronous bookmark delivery
  - REQs: REQ-F-022, REQ-F-037, REQ-C-006, REQ-NF-003
  - Change: guard startup results with the current generation; enforce interaction
    guards at controller dispatch and delivery without suppressing model status.
  - Check: `PlacesModel.StartupResultCannotOverwriteCompletedBookmarkRecheck` and
    `DirectoryController.BookmarkCompletionPreservesInterveningInteractionGuards`.
    Outcomes and remaining checks are recorded in [VERIFICATION.md](VERIFICATION.md).
