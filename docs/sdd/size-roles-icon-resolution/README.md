# Extra-small Files header

Status: Implemented locally; full suite has unrelated failures. Baseline: `c85e0e0d9d75e2ddcd2130fb23c8ba3fedc0051c`.

## Contract

Files consumes `HnControlSize.Xs` and `HnHeaderBar.sizeRole` from the Qt provider. The application header is 42 px high. Its history buttons and breadcrumb are 24 px high; the buttons retain 16 px icons. Breadcrumb horizontal padding is 6 px. Existing navigation, keyboard focus, and accessible names stay intact.

## Implementation and verification

`apps/files/qml/AppHeaderBar.qml` owns the presentation change. `tests/history_navigation_window_test.cpp` checks rendered geometry and icon size alongside existing navigation and focus tests. This consumer requires the locally verified Qt provider revision from the umbrella initiative.

Accepted local Qt provider: `61d0c16`.

Verification on 2026-09-26: all 14 `WindowHistoryNavigation` tests and the affected icon tests passed with the local Qt plugin. Six accelerated OpenGL checks passed outside the sandbox. The full non-accelerated CTest run passed 20 of 21 entries; `files-smoke` had two failures in unchanged assertions: `Files.ModeBadgeWidthConstant` still expects a Pill radius after the prior badge-corner change, and `PlacesWindow.ExtraSpacingGapPrecedesFirstBookmarkRow` reads zero spacing while measuring a 4 px gap. Files formatting, QML import policy, QML lint, QML type metadata, staged installation, REUSE lint, and clang-tidy passed. These two test failures remain open outside this implementation scope.

After committing Qt, `task deps` refreshed the Files dependency prefix to `61d0c16`; the focused history/icon tests and `files-provider-revisions` passed again against that installed revision.
