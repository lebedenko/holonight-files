# Verification

Date: 2026-09-16. Implementation and automated acceptance are complete;
native desktop acceptance remains pending. Generated logs and captures are under
`build/quick-look-cpp-presentation/` (not tracked).

## Requirement evidence

| Requirements | Evidence |
| --- | --- |
| REQ-001/007 | QuickLookGeometry.js removed; QuickLookOverlay binds to QuickLookPresentationModel and contains no settle/request functions. Caption formatting and other JS helpers remain. |
| REQ-002 | QuickLookClassification.PreservesPrecedenceAndIntermediateLoadingStates and StartsCompactAndClassifiesBeforeBusyIsSet cover precedence and the real pre-dispatch signal. |
| REQ-003 | PendingAndAbsentRetainInputsAcrossShrinkAndRestore; existing rendered pending-resize and reopen regressions pass. |
| REQ-004 | PortraitRetainsCaptionWidthAndZeroBoundsDoNotDivide, TextFillsAvailableBoundsAndErrorsUseCompactLayout, MeasurementsUpdateLayoutWithoutRequestFeedback and FractionalBoundsAndDprRoundRequestsIndependentlyOfFrame; existing bounds/aspect/caption tests pass. |
| REQ-005 | DuplicateInputsAndSameRoundedSizeDoNotResubmit, QmlConstructionDefersFirstRequest, fractional DPR tests, and rendered QuickLookRequestedSizeStableAcrossNavigationButNotResize using the private C++ counter. |
| REQ-006 | ReplacementDisconnectsAndDestructionResetsRetainedState and ObserversSeeConsistentOutputs. |
| REQ-008 | Commands below; native acceptance explicitly pending. |

## Commands and results

- `task deps`: passed (`deps.log`). Provider builds/install staging stayed under build/;
  no sibling sources changed.
- `task build`: passed (`build.log`).
- `task test`: initial sandbox run passed all new/Quick Look tests but failed the
  existing SocketAndDeviceCopiesAreRejectedWithoutReading because Unix socket bind
  returned Permission denied (`test.log`). The approved unrestricted rerun passed
  all eight CTest targets (`test-unrestricted.log`). The files-smoke target has 356
  tests: 354 passed, two opt-in native/performance tests skipped, including 11 new
  direct presentation tests and 15 existing Quick Look rendered tests.
- `task format-check`: passed (`format-check.log`).
- `task check`: first run reached clang-tidy after passing builds/tests/formatting,
  then reported arithmetic-parentheses and protected test-fixture member style
  findings (`check.log`). These were corrected without behavior changes. Focused
  clang-tidy on both affected files passed (`tidy-focused.log`). The final full
  pipeline passed (`check-final.log`): Debug/Release builds, all eight test targets,
  formatting, C++/QML lint, REUSE license checks, staged install and uninstall checks.
- `task isolated-runtime-check`: initial sandbox run could not access docker.sock
  (`isolated-runtime.log`); approved unrestricted rerun passed
  (`isolated-runtime-unrestricted.log`). Repeated successfully against the final
  rebuilt application (`isolated-runtime-final.log`), including installed desktop launch as an
  ordinary user in the network-disabled container.
- Fractional render: `QT_QPA_PLATFORM=offscreen QSG_RHI_BACKEND=software
  QT_SCALE_FACTOR=1.5 HOLONIGHT_APPEARANCE_FILE="$PWD/tests/fixtures/dark.toml"
  QML_IMPORT_PATH="$PWD/build/deps/prefix/lib/qt6/qml"
  LD_LIBRARY_PATH="$PWD/build/deps/prefix/lib"
  FILES_CAPTURE_PREFIX="$PWD/build/quick-look-cpp-presentation/dark-1.5"
  build/test/tests/files-smoke
  '--gtest_filter=Files.QuickLook*:Files.InspectionImageSplitterAndPixelSizing'`:
  all 16 passed (`fractional-render.log`). Inspected `dark-1.5-image-popup.png`:
  centered image card and its filename, metadata and close hint remain within
  bounds, with the image aspect ratio preserved. Offscreen captures are not
  native rendering acceptance.

## Pending acceptance

Native Hyprland rendering at 1.5x and native latency acceptance remain pending,
consistent with the original Quick Look verification. No native acceptance claim
is made by this refactor. Spark delegation was attempted but its configured model
was unavailable; implementation and review were performed by the main agent.
