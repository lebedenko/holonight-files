# Parent Directory Navigation Design

Approved scope: Supplied implementation plan. GPL-3.0-or-later.

## Architecture

1. **DirectoryModel**:
   - `DirectoryEntry` gains `bool is_parent = false;`.
   - `DirectoryModel::Role` gains `IsParentRole = Qt::UserRole + 11`.
   - During `load(path)`, if `!path.isEmpty() && !QDir(path).isRoot()`, the model synthesizes a parent row at `entries_[0]` with:
     - `name = ".."`
     - `absolute_path = QDir::cleanPath(QFileInfo(path).absolutePath())`
     - `is_dir = true`
     - `is_parent = true`
     - `mode = S_IFDIR | 0755`
     - `icon_name = IconNameResolver::candidateIconNames(S_IFDIR, parent.name).join(IconNameResolver::kChainSeparator)`
   - On `refresh()`, `seen_this_refresh_` preserves `".."`, preventing `finishDiff()` from dropping it.
   - Worker-thread `walkDirectory` continues to ignore `.` and `..` from `readdir`, keeping worker thread logic pure and isolated from UI row management.

2. **DirectoryProxyModel**:
   - `filterAcceptsRow`: Rows with `IsParentRole == true` are always accepted regardless of `hiddenVisible`.
   - `lessThan`: Pin the parent row to visual index 0 in both ascending and descending sorts:
     ```cpp
     const bool leftIsParent = sourceModel()->data(left, DirectoryModel::IsParentRole).toBool();
     const bool rightIsParent = sourceModel()->data(right, DirectoryModel::IsParentRole).toBool();
     if (leftIsParent != rightIsParent) {
       return leftIsParent ? (sort_order_ == Qt::AscendingOrder) : (sort_order_ == Qt::DescendingOrder);
     }
     ```
   - Placeholder rows anchored to `..` sort below `..`.

3. **Navigation & Interaction**:
   - `NavigationSession::openEntry` and `NavigationSession::navigateInto`: If the target row has `IsParentRole == true`, call `navigateParent()`, which records history and sets the restore target to the exited directory's basename.
   - `NavigationSession::navigateParent`: Return early if `QDir(current_path_).isRoot()`.
   - `EditingSession::beginRename`: No-op if cursor is on `..`.
   - `EditingSession::updateSearchQuery`: Skip `..` when collecting names for search candidates.
   - `DirectoryController::yankOrCut` & `requestTrash`: No-op if cursor is on `..`.
   - `DirectoryController::collectVisualSelectionPaths`: Exclude rows with `IsParentRole == true`.
   - `PreviewSelection::canPreviewSelection`: Returns `false` for `..`.
   - `PreviewSelection::syncPreviewTarget`: Pass entry name to `PreviewService::setTarget` so `".." ` is displayed.

4. **Presentation**:
   - `DirectoryListing.qml`: Exposes `isParent` in delegate, renders `..` with folder icon, and leaves size and modified columns blank.
