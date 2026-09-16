# SDD Tasks — quick-look-redesign

- [x] T-001: QuickLookGeometry.js library and CMake registration
  - REQs: REQ-F-001, REQ-F-006, REQ-F-011
  - Check: QuickLookGeometry.js is listed in apps/files/CMakeLists.txt QML_FILES and compiles with classify(), fitRect(), compactCardWidth() functions defined

- [x] T-002: PreviewServiceTestAccess::quickLookRequestedSize accessor
  - REQs: REQ-F-017
  - Check: Code compiles and `PreviewServiceTestAccess::quickLookRequestedSize(const PreviewService&)` returns a QSize

- [x] T-003: Overlay card background, padding, backdrop structure
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-C-004
  - Check: QuickLookOverlay.qml compiles, the quickLookCard background Rectangle has a runtime radius > 0, a C.Overlay.modal Rectangle in HoloniightPalette.scrim covers the window, and closePolicy is still C.Popup.NoAutoClose

- [x] T-004: Caption column layout with labels and text/color properties
  - REQs: REQ-F-004, REQ-F-005, REQ-F-008, REQ-F-010, REQ-F-012, REQ-F-014
  - Check: QuickLookOverlay.qml has nameLabel, metadataLabel, hintLabel with objectNames (quickLookName, quickLookMetadata, quickLookHint) and text/color properties bound to preview.* and computed properties

- [x] T-005: settle() state machine and geometry computation for all entry kinds
  - REQs: REQ-F-001, REQ-F-006, REQ-F-009, REQ-F-011, REQ-F-013
  - Check: settle() function computes settledWidth/settledHeight/settledFrameWidth/settledFrameHeight for compact/image/text kinds without animation, triggered on preview.changed() and parent size changes

- [x] T-006: Busy indicator and compact icon elements
  - REQs: REQ-F-011, REQ-F-015
  - Check: BusyIndicator with objectName quickLookBusy visible when `busy && !hasImage && !hasText`, HnIcon with objectName quickLookIcon visible for compact kind

- [x] T-007: reportRequestedSize() rewiring and requestedSizeCallCount property
  - REQs: REQ-F-017, REQ-F-018
  - Check: reportRequestedSize() fires only on parent width/height/Screen.devicePixelRatio changes (never on preview.changed), requestedSizeCallCount increments at each call site

- [x] T-008: Card layout tests - bounds, centering, styling, backdrop, name, hint
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005
  - Check: TEST(Files, QuickLookCardStaysWithinBoundsForEveryKind), QuickLookBackdropCoversWindowWithScrim, QuickLookNameElidesLongFilenamesAndStaysCentered (including hint assertions in QuickLookCardStaysWithinBoundsForEveryKind) all pass

- [x] T-009: Image preview tests - aspect-fit frame, rounded clip, metadata with dimensions
  - REQs: REQ-F-006, REQ-F-007, REQ-F-008
  - Check: TEST(Files, QuickLookImageFrameAspectFitsPreviewBounds) and QuickLookImageMetadataLineShowsDimensionsThenSizeOnly pass for 6000×4000, 1000×5000, 5000×1000 sources

- [x] T-010: Text preview tests - frame bounds, monospace view, metadata with truncation
  - REQs: REQ-F-009, REQ-F-010
  - Check: TEST(Files, QuickLookTextFrameFillsPreviewBoundsWithMonospaceView) passes, including its truncated-suffix assertions

- [x] T-011: Compact preview tests - card sizing with icon, metadata for dirs and errors
  - REQs: REQ-F-011, REQ-F-012
  - Check: TEST(Files, QuickLookCompactCardShowsDirIconAndStaysSmall) and QuickLookCompactCardShowsErrorAndMimeDescription pass

- [x] T-012: Pending navigation tests - geometry retention, immediate caption update, busy indicator
  - REQs: REQ-F-013, REQ-F-014, REQ-F-015
  - Check: TEST(Files, QuickLookRetainsSettledGeometryWhilePending) passes with a blocked worker, including immediate-caption and stale-image assertions

- [x] T-013: Reopen on different kind test - geometry uses new entry's layout
  - REQs: REQ-F-016
  - Check: TEST(Files, QuickLookReopenOnDifferentKindUsesNewGeometry) passes after open/close/settle/reopen cycle

- [x] T-014: Requested size stability test - unchanged across navigation, changes on window resize
  - REQs: REQ-F-017, REQ-F-018
  - Check: TEST(Files, QuickLookRequestedSizeStableAcrossNavigationButNotResize) passes with requestedSizeCallCount unchanged during navigation but incremented on window resize

- [x] T-015: No binding loops test across navigation and window resize
  - REQs: REQ-NF-001
  - Check: TEST(Files, QuickLookNavigationAndResizeProduceNoBindingLoops) passes with zero "Binding loop detected" warnings from qInstallMessageHandler

- [x] T-016: Regression validation - existing tests, key routing, object names, decode pipeline
  - REQs: REQ-C-001, REQ-C-002, REQ-C-003, REQ-C-004
  - Check: Existing smoke/history_navigation_window/preview_integration/directory_controller tests pass, git diff shows no changes to InspectionKeys.js/DirectoryController/*service*.cpp, each REQ-C-002 objectName found exactly once

- [x] T-017: Format check registration and styling compliance
  - REQs: REQ-C-005, REQ-F-002
  - Check: task format-check and task qml-lint pass; QuickLookGeometry.js is registered in QML_FILES and no new .qml file needs format-list registration; grep finds no hardcoded radius/color/margins/padding/spacing in QuickLookOverlay.qml

- [x] T-018: holonight-qt and decode pipeline unchanged validation
  - REQs: REQ-C-003
  - Check: git -C ../holonight-qt status --porcelain output is empty and git diff over apps/files/*service*.{h,cpp} is empty

- [ ] T-019: Manual native Hyprland 1.5× DPR rendering acceptance
  - REQs: REQ-NF-002
  - Check: User confirms on Hyprland display at scale 1.5 that card corners, image clip, caption text are crisp, caption centered, and listing behind card is dimmed

- [ ] T-020: Smoke test latency budget unchanged
  - REQs: REQ-NF-003
  - Check: The opt-in NativeInspectionAcceptance test in tests/smoke.cpp still satisfies EXPECT_LT(quickLookMs, 200) when run natively

- [x] T-021: Refit retained pending geometry on window resize and add a blocked-worker regression
  - REQs: REQ-F-001, REQ-F-005, REQ-F-013, REQ-F-015, REQ-NF-001
  - Check: QuickLookPendingResizeKeepsCaptionInsideCard passes for shrinking and restoring bounds

- [x] T-022: Release blocked workers before harness destruction on every test exit
  - REQs: REQ-F-008, REQ-F-013
  - Check: Both blocked-worker tests declare the release guard after their harness

Verification commands, results and pending native acceptance are recorded in [VERIFICATION.md](VERIFICATION.md).
