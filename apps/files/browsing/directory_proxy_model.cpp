#include "directory_proxy_model.h"

#include <HolonightFileBrowser/directory_roles.h>

FilesDirectoryProxyModel::FilesDirectoryProxyModel(QObject* parent) : DirectoryProxyModel(parent) {}
void FilesDirectoryProxyModel::setHiddenVisible(bool visible) {
  if (!editing_) {
    DirectoryProxyModel::setHiddenVisible(visible);
  }
}
void FilesDirectoryProxyModel::setSortDescending(bool descending) {
  if (!editing_) {
    DirectoryProxyModel::setSortDescending(descending);
  }
}
void FilesDirectoryProxyModel::setPlaceholder(int sourceRow, const QString& anchorName, bool anchorIsDir, bool below) {
  placeholder_source_row_ = sourceRow;
  placeholder_anchor_name_ = anchorName;
  placeholder_anchor_is_dir_ = anchorIsDir;
  placeholder_below_ = below;
}
// Qt's descending mode reverses whatever lessThan() decides, including the directories-first
// grouping below; REQ-F-003 only mandates dirs-first "by default" (ascending), so this is accepted.
bool FilesDirectoryProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  if (placeholder_source_row_ < 0 ||
      (left.row() != placeholder_source_row_ && right.row() != placeholder_source_row_)) {
    return DirectoryProxyModel::lessThan(left, right);
  }
  const bool leftIsParent = sourceModel()->data(left, DirectoryRoles::IsParentRole).toBool();
  const bool rightIsParent = sourceModel()->data(right, DirectoryRoles::IsParentRole).toBool();
  if (leftIsParent != rightIsParent) {
    return leftIsParent ? !sortDescending() : sortDescending();
  }
  // The placeholder row borrows its anchor's (isDir, name) sort key instead of its own (it has no
  // real name yet) — see setPlaceholder().
  const bool leftIsPlaceholder = placeholder_source_row_ >= 0 && left.row() == placeholder_source_row_;
  const bool rightIsPlaceholder = placeholder_source_row_ >= 0 && right.row() == placeholder_source_row_;
  const bool leftIsAnchorParent = leftIsPlaceholder && placeholder_anchor_name_ == QStringLiteral("..");
  const bool rightIsAnchorParent = rightIsPlaceholder && placeholder_anchor_name_ == QStringLiteral("..");
  if (leftIsAnchorParent != rightIsAnchorParent) {
    return leftIsAnchorParent ? !sortDescending() : sortDescending();
  }
  const bool leftIsDir =
      leftIsPlaceholder ? placeholder_anchor_is_dir_ : sourceModel()->data(left, DirectoryRoles::IsDirRole).toBool();
  const bool rightIsDir =
      rightIsPlaceholder ? placeholder_anchor_is_dir_ : sourceModel()->data(right, DirectoryRoles::IsDirRole).toBool();
  if (leftIsDir != rightIsDir) {
    return leftIsDir;
  }
  const auto leftName =
      leftIsPlaceholder ? placeholder_anchor_name_ : sourceModel()->data(left, DirectoryRoles::NameRole).toString();
  const auto rightName =
      rightIsPlaceholder ? placeholder_anchor_name_ : sourceModel()->data(right, DirectoryRoles::NameRole).toString();
  const int cmp = compareNames(leftName, rightName);
  if (cmp != 0) {
    return cmp < 0;
  }
  const bool belowInAscending = placeholder_below_ != sortDescending();
  if (leftIsPlaceholder != rightIsPlaceholder) {
    return leftIsPlaceholder ? !belowInAscending : belowInAscending;
  }
  return false;
}

void FilesDirectoryProxyModel::setParentVisible(bool visible) {
  if (parent_visible_ == visible) {
    return;
  }
  beginFilterChange();
  parent_visible_ = visible;
  endFilterChange(QSortFilterProxyModel::Direction::Rows);
}
bool FilesDirectoryProxyModel::filterAcceptsRow(int row, const QModelIndex& parent) const {
  return (parent_visible_ || !sourceModel()->index(row, 0, parent).data(DirectoryRoles::IsParentRole).toBool()) &&
         DirectoryProxyModel::filterAcceptsRow(row, parent);
}
