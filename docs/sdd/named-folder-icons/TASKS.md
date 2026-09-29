# SDD Tasks — named-folder-icons

- [x] T-001: IconNameResolver namedIcon arg and resolver unit tests
  - REQs: REQ-C-005, REQ-F-003, REQ-F-005, REQ-F-016
  - Check: IconNameResolver::candidateIconNames() accepts defaulted namedIcon parameter, prepends it only for S_ISDIR modes, and resolves exactly as spec'd; icon_name_resolver_test.cpp includes cases for matched directories, non-directories with namedIcon, and existing tests still pass.

- [x] T-002: PlaceList module, CMakeLists.txt entries, and unit tests
  - REQs: REQ-F-001, REQ-C-001, REQ-NF-001
  - Check: PlaceList::standardPlaces(homePath, userDirsFilePath) returns Home first with user-home, then UserDirsParser entries in Key order, deduplicated by path; PlaceList::iconMap(places) keyed by cleaned path; place_list_test.cpp covers Home-first, XDG order, dedup, missing file, empty-list cases; apps/files/CMakeLists.txt lists place_list.cpp and .h; tests/CMakeLists.txt adds place_list_test to files-smoke source list.

- [x] T-003: PlacesModel refactor to consume PlaceList and expose placeIcons()
  - REQs: REQ-F-014, REQ-C-001
  - Check: PlacesModel::buildPlaces() consumes PlaceList::standardPlaces() and PlaceList::iconMap(); hard-coded "user-home" literal is removed; placeIcons() accessor returns shared_ptr<const IconMap>; places_model_test.cpp existing tests pass unmodified and one new test asserts placeIcons() map keys equal place paths.

- [x] T-004: DirectoryModel setPlaceIcons, map parameter threading, and integration tests
  - REQs: REQ-F-002, REQ-F-004, REQ-F-008, REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013, REQ-F-017, REQ-C-002, REQ-C-003, REQ-NF-002
  - Check: DirectoryModel::setPlaceIcons(shared_ptr<const IconMap>) accepts the map; readEntry(), readParentEntry(), syntheticParentEntry(), walkDirectory() gain map parameter and call namedIconFor() to prepend matched icon to chain; directory_model_test.cpp includes nine test cases (matched vs. unmatched Documents, symlinks, files, home, parent row, placeholder, list-before-set, sidebar parity); worker thread captures map by value; no new role or Q_PROPERTY added.

- [x] T-005: DirectoryController wiring line and isolation of controller/window tests
  - REQs: REQ-F-013
  - Check: DirectoryController::DirectoryController() calls navigation_.model_.setPlaceIcons(places_.placeIcons()) after proxy setup; grep tests/directory_controller_test.cpp and tests/window_test.cpp for 'folder/inode-directory' assertions listing real $HOME and isolate them with XDG_CONFIG_HOME and HOME environment variables set to temporary directories rather than weakening assertions.

- [x] T-006: PreviewService isDirectory property and unit test
  - REQs: REQ-F-006, REQ-F-015
  - Check: PreviewService adds Q_PROPERTY(bool isDirectory READ isDirectory NOTIFY changed) backed by existing is_dir_ member; PreviewService::isDirectory() returns true exactly when has_entry_ && is_dir_; preview_service_test.cpp asserts isDirectory true for directory, false for file and after clear().

- [x] T-007: PreviewPane.qml folder fallback selector and window/QML test
  - REQs: REQ-F-006, REQ-F-007
  - Check: PreviewPane.qml line ~264 replaces isFolderIconName property logic to use root.preview.isDirectory instead of startsWith("folder/") or === "folder" tests; window/QML test with a bare theme (lacking folder-documents) asserts folder-fallback.svg shown for Documents directory and generic-file-fallback.svg for a regular file; IconFallbacks records at most one unresolved chain per distinct chain name across 100 same-chain rows.
  - Regression coverage: `IconImageProvider.RepeatedNamedFolderRequestsDoNotInflateLookupsOrWarnings` sends 100 identical named-folder requests to both an empty theme and a folder-only theme, asserting that theme lookup counts stop increasing after the first request and no Qt warnings are emitted. The window test separately verifies the named chain is registered in `IconFallbacks` and selects the folder glyph.

- [x] T-008: Full verify, manual review, and native visual check
  - REQs: All REQs
  - Check: task build and task test pass; task format-check and task tidy pass with no style issues in new headers under apps/files/places/; task qml-lint passes; manual code review of all SPEC.md acceptance criteria; native visual check on real Hyprland display at 1.5x fractional scale confirms listed Documents and sidebar Documents show identical folder-documents icon or folder fallback when themed.
  - Manual acceptance (2026-09-29): the user confirmed they performed the manual checks, found no visual issues, and the feature works as expected.
  - Automated evidence and remaining verification limits: see [VERIFICATION.md](VERIFICATION.md).
