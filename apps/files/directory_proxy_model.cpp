#include "directory_proxy_model.h"

#include "directory_model.h"

DirectoryProxyModel::DirectoryProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
  if (collator_.locale().language() == QLocale::C) {
    collator_.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
  }
  collator_.setNumericMode(true);
  collator_.setCaseSensitivity(Qt::CaseInsensitive);
  setDynamicSortFilter(true);
  // Establishes the initial sort; without one explicit call, dynamicSortFilter has nothing to
  // reapply as new rows land, and rows would stay in walk-discovery order.
  sort(0, sort_order_);
}
void DirectoryProxyModel::setHiddenVisible(bool visible) {
  if (editing_ || hidden_visible_ == visible) {
    return;
  }
  hidden_visible_ = visible;
  beginFilterChange();
  endFilterChange();
  emit hiddenVisibleChanged();
}
void DirectoryProxyModel::setSortDescending(bool descending) {
  const auto order = descending ? Qt::DescendingOrder : Qt::AscendingOrder;
  if (editing_ || sort_order_ == order) {
    return;
  }
  sort_order_ = order;
  sort(0, sort_order_);
  emit sortDescendingChanged();
}
void DirectoryProxyModel::setPlaceholder(int sourceRow, const QString& anchorName, bool anchorIsDir, bool below) {
  placeholder_source_row_ = sourceRow;
  placeholder_anchor_name_ = anchorName;
  placeholder_anchor_is_dir_ = anchorIsDir;
  placeholder_below_ = below;
}
// Qt's descending mode reverses whatever lessThan() decides, including the directories-first
// grouping below; REQ-F-003 only mandates dirs-first "by default" (ascending), so this is accepted.
bool DirectoryProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  // The placeholder row borrows its anchor's (isDir, name) sort key instead of its own (it has no
  // real name yet) — see setPlaceholder().
  const bool leftIsPlaceholder = placeholder_source_row_ >= 0 && left.row() == placeholder_source_row_;
  const bool rightIsPlaceholder = placeholder_source_row_ >= 0 && right.row() == placeholder_source_row_;
  const bool leftIsDir =
      leftIsPlaceholder ? placeholder_anchor_is_dir_ : sourceModel()->data(left, DirectoryModel::IsDirRole).toBool();
  const bool rightIsDir =
      rightIsPlaceholder ? placeholder_anchor_is_dir_ : sourceModel()->data(right, DirectoryModel::IsDirRole).toBool();
  if (leftIsDir != rightIsDir) {
    return leftIsDir;
  }
  const auto leftName =
      leftIsPlaceholder ? placeholder_anchor_name_ : sourceModel()->data(left, DirectoryModel::NameRole).toString();
  const auto rightName =
      rightIsPlaceholder ? placeholder_anchor_name_ : sourceModel()->data(right, DirectoryModel::NameRole).toString();
  int cmp = collator_.compare(leftName, rightName);
  if (cmp == 0) {
    cmp = QString::compare(leftName, rightName, Qt::CaseSensitive);
  }
  if (cmp != 0) {
    return cmp < 0;
  }
  const bool belowInAscending = placeholder_below_ != (sort_order_ == Qt::DescendingOrder);
  if (leftIsPlaceholder != rightIsPlaceholder) {
    return leftIsPlaceholder ? !belowInAscending : belowInAscending;
  }
  return false;
}
bool DirectoryProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
  if (hidden_visible_) {
    return true;
  }
  const auto sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
  return !sourceModel()->data(sourceIndex, DirectoryModel::IsHiddenRole).toBool();
}
