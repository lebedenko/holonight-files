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
  if (hidden_visible_ == visible) {
    return;
  }
  hidden_visible_ = visible;
  beginFilterChange();
  endFilterChange();
  emit hiddenVisibleChanged();
}
void DirectoryProxyModel::setSortDescending(bool descending) {
  const auto order = descending ? Qt::DescendingOrder : Qt::AscendingOrder;
  if (sort_order_ == order) {
    return;
  }
  sort_order_ = order;
  sort(0, sort_order_);
  emit sortDescendingChanged();
}
// Qt's descending mode reverses whatever lessThan() decides, including the directories-first
// grouping below; REQ-F-003 only mandates dirs-first "by default" (ascending), so this is accepted.
bool DirectoryProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  const bool leftIsDir = sourceModel()->data(left, DirectoryModel::IsDirRole).toBool();
  const bool rightIsDir = sourceModel()->data(right, DirectoryModel::IsDirRole).toBool();
  if (leftIsDir != rightIsDir) {
    return leftIsDir;
  }
  const auto leftName = sourceModel()->data(left, DirectoryModel::NameRole).toString();
  const auto rightName = sourceModel()->data(right, DirectoryModel::NameRole).toString();
  return collator_.compare(leftName, rightName) < 0;
}
bool DirectoryProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
  if (hidden_visible_) {
    return true;
  }
  const auto sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
  return !sourceModel()->data(sourceIndex, DirectoryModel::IsHiddenRole).toBool();
}
