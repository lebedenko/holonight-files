# SDD Tasks — browse-folder

- [x] T-001: DirectoryModel with async worker thread and incremental batching
  - REQs: REQ-F-001, REQ-F-002
  - Check: DirectoryModel loads a populated test directory asynchronously, inserts incremental batches, and does not block UI thread during walk.

- [x] T-002: Error handling in DirectoryModel (directory and per-entry stat failures)
  - REQs: REQ-F-004, REQ-F-005
  - Check: Opening an unreadable directory sets directoryError property; entries with target-stat failures render with StatFailedRole=true and StatErrorRole containing error message.

- [x] T-003: PlacesModel with standard locations and DirectoryProxyModel with natural sorting
  - REQs: REQ-F-003, REQ-F-008
  - Check: PlacesModel constructor populates 4 rows (Home/Documents/Downloads/Pictures) with correct paths; ProxyModel sorts directories before files using QCollator with numeric mode and case-insensitive ordering.

- [x] T-004: DirectoryController facade owning all models and coordinating state
  - REQs: REQ-F-001, REQ-F-007
  - Check: DirectoryController instantiates DirectoryModel, DirectoryProxyModel, PlacesModel, and QFileSystemWatcher; exposes Q_PROPERTYs (currentPath, scanning, directoryError, cursorRow, statusMessage) and invokables (open, shutdown).

- [x] T-005: Keyboard navigation handlers (j/k/count prefix/gg/G/h)
  - REQs: REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013
  - Check: DirectoryController.handleKey() moves cursorRow down/up by count (default 1) with j/k, jumps to row 0 with gg, to last row with G, and calls navigateParent() on h.

- [x] T-006: Directory entry navigation and file opening (l/Enter and openEntry dispatch)
  - REQs: REQ-F-006
  - Check: handleKey() calls openEntry() on l and Return; navigateInto() opens directory in place; openEntry() on file calls QDesktopServices::openUrl with file:// URL.

- [x] T-007: Hidden files toggle and sort order toggle
  - REQs: REQ-F-014, REQ-F-015
  - Check: handleKey(".") calls toggleHidden(), sets DirectoryProxyModel.hiddenVisible property and calls beginFilterChange()/endFilterChange(); handleKey("s") calls toggleSortDirection(), flips sortDescending and re-sorts; changes are immediate without re-walk.

- [x] T-008: QFileSystemWatcher integration and diffing refresh
  - REQs: REQ-F-007
  - Check: DirectoryController points watcher to currentPath; on directoryChanged signal calls DirectoryModel.refresh(), which diffs old vs new entries by name and emits targeted insert/remove row ranges rather than full reset.

- [x] T-009: CLI argument parsing with QCommandLineParser and fallback resolution
  - REQs: REQ-C-001
  - Check: Passing valid directory path opens that directory; passing invalid/missing path opens home directory with statusMessage explaining reason ("not a directory", "permission denied", "path does not exist", or "no folder given").

- [x] T-010: main.cpp initialization sequence and shutdown draining
  - REQs: REQ-C-001
  - Check: main.cpp resolves directory argument, constructs DirectoryController, loads Main.qml, calls controller.open() via QTimer::singleShot(0); on lastWindowClosed calls controller.shutdown() and waits for shutdownFinished before QCoreApplication::quit().

- [x] T-011: QML Main.qml two-pane layout, PlacesPanel.qml sidebar, and DirectoryListing.qml with delegate
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-008
  - Check: Application launches showing two-pane layout (places sidebar left, directory listing right); DirectoryListing displays entries with name/isDir/size/modified roles from proxy model; remains scrollable during background load.

- [x] T-012: Inline error state rendering and per-entry error glyphs in QML
  - REQs: REQ-F-004, REQ-F-005
  - Check: DirectoryListing shows inline error state (reusing HnEmptyState visual language) when directoryError is set; entries with statFailed=true display error glyph or HnStatusIndicator.warning instead of normal rendering.

- [x] T-013: Test fixtures with empty, large, permission-denied, symlink, and Unicode entries
  - REQs: REQ-C-002, REQ-C-003
  - Check: Fixture directory contains QTemporaryDir-based subdirectories: empty, 10,000+ entry, chmod 000 entry, dangling symlink, and Unicode-named entries; permission test skips assertion when geteuid()==0 with documented reason in test code.

- [x] T-014: DirectoryModel unit tests (async loading, batching, incremental insertion, stat failures)
  - REQs: REQ-F-001, REQ-F-005, REQ-NF-001
  - Check: Test suite passes all scenarios: async walk does not block, multiple batches arrive on a large fixture, batching threshold met, entries with target-stat failures included with StatFailedRole and error message.

- [x] T-015: ProxyModel sorting/filtering unit tests and toggle behavioral tests
  - REQs: REQ-F-003, REQ-F-014, REQ-F-015
  - Check: Tests pass for natural collation order, directories sort before files, hidden-files filter correctly removes/includes dotfiles on toggle, sort-direction toggle reverses order without full model reset.

- [x] T-016: Keyboard handler unit tests (motion, count buffer, gg sequence, boundaries)
  - REQs: REQ-F-009, REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013
  - Check: Test suite passes all key sequences: j/k single and with count, gg two-key sequence, G single key, cursorRow clamped to [0, rowCount-1], count buffer cleared after motion or non-motion key.

- [x] T-017: Performance and interactivity validation on 10,000+ entry directory
  - REQs: REQ-NF-001
  - Check: Manual test on large fixture confirms first visible row renders within 150ms of open() call and active-scrolling frame gaps stay ≤100ms and posted-key input latency stays ≤100ms during background walk completion.

- [x] T-018: Review regression coverage and delivery checks
  - REQs: REQ-C-001–003, REQ-F-001–015, REQ-NF-001
  - Check: production-window input, watcher create/delete/rename, file URL dispatch,
    startup fallback, before-open races and injected read errors; no-op/batched diff
    signals; functional tests in both locales as non-root; header formatting, build,
    release, tidy, QML lint, licensing, install checks, populated/error screenshots.

All tasks were reopened for review and closed against the passing evidence in
[VERIFICATION.md](VERIFICATION.md). T-017 passed native Wayland acceptance on the
recorded 12,000-entry fixture; offscreen runs alone do not close that gate. Hosted
CI execution is not claimed.
