# Places sidebar design

Approved scope: supplied plan. GPL-3.0-or-later (repository REUSE metadata).

PlacesModel owns a startup snapshot of standard locations and optional Projects.
Skip empty values before path cleaning. A test seam supplies standard locations
and home for deterministic isolated fixtures; production uses QStandardPaths.
Expose translated name, path and iconName; prefer user-home/folder category icons
with folder/inode-directory candidates.

PlacesPanel uses a fixed muted heading over a clipped scrolling ListView. Shared
HnListDelegate compact rows use horizontal token insets and centered leading
HnIcon components, the existing image provider, and bundled folder fallback.
A typed PlaceRow wrapper prevents shared implicit ListView selection from
highlighting the keyboard row. A single focusable list handles Up/Down and Enter/Space; delegates retain names
and mouse activation. Current keyboard row is independent of exact-path selection.
Gate the panel on NORMAL/no prompt/no Quick Look and recheck activation. Existing
controller navigated signal returns focus to the directory listing.

Verification: model fixtures for ordering, translation, roles, empty/missing paths
and Projects; rendered window tests for navigation, history, focus, guards,
selection, fallback and scrolling. Capture light/dark and fractional-scale renders.
No new install payload or shared-package modifications.
