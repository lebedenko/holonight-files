# Panel keyboard navigation

## Intent and scope

Directional panel shortcuts work, but panel-local Vim keys are inconsistent. Fix
sidebar activation with plain l and breadcrumb focus movement with plain h/l now.
A broader redesign of Tab/Backtab traversal, focus groups, overflow-menu keyboard
behavior and focus restoration during breadcrumb model changes remains separate
follow-up design work; this change preserves existing Tab behavior.

## Contract and ownership

Files routes sidebar l through the same activateCursor path as Enter, with the
existing NORMAL-mode activation guard. Ctrl+L retains panel focus routing.
The provider adds opt-in HnBreadcrumbBar.vimNavigation (default false). Files opts
in; its existing enabled binding gates all breadcrumb interaction by mode/modal state.
Only unmodified h/l on breadcrumb buttons move focus in visual order: visible
root, ellipsis, visible ancestor segments, current segment. Hidden/disabled/zero-size
buttons are skipped. Boundaries retain focus; movement uses keyboard focus indication
and never emits segmentActivated. Enter retains activation. The edit button and
popup menu are outside this local route; Ctrl shortcuts are unchanged.

## Acceptance

Headless provider regressions cover expanded/collapsed/root trails, boundaries,
keyboard indication, no activation, opt-out and modifier isolation. Files window
regressions cover breadcrumb wiring and sidebar l for both Places and Devices.
Run focused regressions and QML checks against an isolated provider prefix, then
reuse the prior broader acceptance except where these changes require rechecking.
Manual native checks must verify visible focus and local keys; never drive native input.

Provider contract: [shared controls](../../../holonight-qt/docs/shared-file-ui-controls.md).
Suggested commit explanation: fix(navigation): support panel-local Vim keys.
