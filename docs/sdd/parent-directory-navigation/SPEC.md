# Parent Directory Navigation Requirements

Status: Approved by the user implementation plan (2026-09-24).

- PDN-001: For any non-root directory path loaded by DirectoryModel, the system shall include a synthetic parent entry named ".." representing the parent directory.
- PDN-002: When the directory path is the filesystem root ("/" or QDir::isRoot() is true), the system shall omit the ".." row.
- PDN-003: The ".." row shall expose IsParentRole = true, IsDirRole = true, PathRole matching the parent directory's cleaned path, SizeRole = -1, and ModeRole = S_IFDIR | 0755.
- PDN-004: In DirectoryProxyModel, the ".." row shall always be pinned to visual index 0 (the first item), independently of sorting order (ascending or descending) or column.
- PDN-005: In DirectoryProxyModel, the ".." row shall always remain visible regardless of the hidden files toggle state (never filtered out by hiddenVisible = false).
- PDN-006: When the ".." row is activated via single-click, Enter/Return, or 'l', the system shall navigate to the parent directory via navigateParent(), landing on the child directory in the parent listing and recording the traversal in history.
- PDN-007: When entering a directory without a restore target, the selection cursor shall land on visual row 0 ("..").
- PDN-008: The ".." row shall be strictly protected from file modifications: rename ('r'/'R'), cut ('d'), yank ('y'), and trash ('x'/Delete) shall treat ".." as a no-op, and visual selection operations shall exclude "..".
- PDN-009: Quick Look ('Space') shall be disabled on "..", and search ('/') shall exclude ".." from match candidates.
- PDN-010: The preview pane shall display ".." with a folder icon and directory metadata when selected.

Non-goals: Supporting ".." navigation above root "/", permitting modification of parent directory links, or changing filesystem worker algorithms.
