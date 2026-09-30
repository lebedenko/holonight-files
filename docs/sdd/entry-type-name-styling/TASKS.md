# SDD Tasks — entry-type-name-styling

- [x] T-001: Add is_symlink field to DirectoryEntry struct
  - REQs: REQ-F-010
  - Check: `apps/files/browsing/directory_model.h` declares `bool is_symlink = false;` on DirectoryEntry after `is_parent` and before `operator==`.

- [x] T-002: Implement hoisted lstat() in readEntry() worker function
  - REQs: REQ-F-011, REQ-F-012, REQ-F-013, REQ-NF-002
  - Check: `apps/files/browsing/directory_model.cpp` readEntry() calls `::lstat()` once before `::stat()`, unconditionally setting `entry.is_symlink`, with no duplicate lstat() in the stat-failure branch.

- [x] T-003: Add IsSymlinkRole to DirectoryModel Role enum
  - REQs: REQ-F-014
  - Check: `apps/files/browsing/directory_model.h` Role enum contains `IsSymlinkRole` appended after `IsParentRole`.

- [x] T-004: Expose IsSymlinkRole via data() and roleNames() methods
  - REQs: REQ-F-014
  - Check: `apps/files/browsing/directory_model.cpp` data() method has case returning `entry.is_symlink` for IsSymlinkRole, and roleNames() maps `{IsSymlinkRole, "isSymlink"}`.

- [x] T-005: Extend directory_model_test.cpp with is_symlink coverage
  - REQs: REQ-F-006, REQ-F-010, REQ-F-011, REQ-F-012
  - Check: `tests/directory_model_test.cpp` verifies IsSymlinkRole returns true for symlink-to-file, symlink-to-directory, and dangling symlink (via PermissionFixture); false for regular file and directory.

- [x] T-006: Add isSymlink required property to DirectoryListing delegate
  - REQs: REQ-F-015
  - Check: `apps/files/qml/listing/DirectoryListing.qml` delegate declares `required property bool isSymlink` and `task qml-lint` passes.

- [x] T-007: Add type-styling readonly properties to DirectoryListing delegate
  - REQs: REQ-F-016, REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005
  - Check: `apps/files/qml/listing/DirectoryListing.qml` delegate declares `typeItalic`, `typeWeight`, and `typeColor` readonly properties with correct three-way precedence (symlink > folder > file).

- [x] T-008: Update filenameRun HnLabel font and color bindings with type styling
  - REQs: REQ-F-016, REQ-F-017, REQ-F-007, REQ-F-008, REQ-F-009
  - Check: `apps/files/qml/listing/DirectoryListing.qml` filenameRun HnLabel binds `font.italic` to `delegate.typeItalic`, `font.weight` and `color` to matched/type ternaries preserving search-highlight overrides for matched runs only.

- [x] T-009: Extend smoke.cpp with filenameRun type-styling integration assertions
  - REQs: REQ-F-001, REQ-F-002, REQ-F-016, REQ-F-017, REQ-F-007, REQ-F-008, REQ-F-009
  - Check: `tests/smoke.cpp` filenameRun object-name walk asserts font.italic/weight/color for symlink/folder/file fixture rows and verifies search-highlight color/weight override works with new type-styling base.

- [x] T-010: Run full verification suite and confirm constraints
  - REQs: REQ-C-003, REQ-C-004, REQ-C-005, REQ-NF-001, REQ-NF-003
  - Check: `task qml-lint`, `task format-check`, and `task test` all pass; production code changes are limited to directory_model.h/cpp and DirectoryListing.qml; no holonight-qt/Places/breadcrumb/QuickLook changes present.

- [ ] T-011: Compare regular-file pixels against the pre-feature baseline
  - REQs: REQ-F-003, REQ-F-016
  - Pending: property assertions do not establish pixel equality; capture and compare the same fixture with the pre-feature build.

- [ ] T-012: Measure filesystem syscall counts
  - REQs: REQ-F-013, REQ-NF-002
  - Pending: source inspection confirms one call site, but no syscall trace has been recorded. Count listed entries separately from parent-row and process startup calls.

- [ ] T-013: Compare 1000-entry listing latency against the pre-feature baseline
  - REQs: REQ-NF-004
  - Pending: run baseline/candidate measurements on the same fixture and environment; require less than 50 ms additional latency.

## Verification record

Review checks: the existing build/test files-smoke target was current; 34 focused tests passed
(DirectoryModel.*, entry styling, and runtime editing/search), task format-check passed,
the build/debug qml-lint target passed, and git diff --check passed. These results do not
establish full task check, isolated runtime acceptance, baseline pixel equality, syscall
counts, or baseline-relative latency. No publication or commit was performed.

Fix verification: the rebuilt files-smoke executable passed 35 focused tests, including
EntryNameSearchOverridesColorAndWeightWhilePreservingItalic for regular files, folders,
file symlinks, directory symlinks, and dangling symlinks. The test checks matched/unmatched
runs and base-style restoration over two SEARCH-mode entry/exit cycles. task format-check
and git diff --check passed after formatting the added test.

Broad verification: task check completed debug/release/test builds, then stopped at sandbox
failures in files-smoke (socket bind denied) and six accelerated separator tests. All seven
failed CTest targets passed with `ctest --preset test --rerun-failed --output-on-failure`
outside the sandbox; the other 20 targets passed in the initial run.

Quality checks: task lint (C++ tidy and QML lint) passed. REUSE initially failed because the
sandbox denied its multiprocessing socket; `task license-check install-check qml-import-check
qmltypes-check` passed outside the sandbox. Together with task format-check, the builds, and
CTest results above, all task check components passed. The original task check invocation
itself stopped at the sandbox test failures. T-011/T-012/T-013 remain pending; isolated runtime
and manual native visual acceptance have not been performed, and no publication occurred.
