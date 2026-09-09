#pragma once

#include <QCollator>
#include <QSortFilterProxyModel>
#include <QtQml/qqmlregistration.h>

// Layers natural, case-insensitive, directories-first sorting and the hidden-files toggle on top
// of a raw DirectoryModel. dynamicSortFilter keeps the order correct as new batches land mid-walk,
// so nothing here hand-rolls incremental re-sort logic (see SPEC.md REQ-F-003, REQ-F-014, REQ-F-015).
class DirectoryProxyModel : public QSortFilterProxyModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the application")
  Q_PROPERTY(bool hiddenVisible READ hiddenVisible WRITE setHiddenVisible NOTIFY hiddenVisibleChanged)
  Q_PROPERTY(bool sortDescending READ sortDescending WRITE setSortDescending NOTIFY sortDescendingChanged)
 public:
  explicit DirectoryProxyModel(QObject* parent = nullptr);
  bool hiddenVisible() const { return hidden_visible_; }
  void setHiddenVisible(bool visible);
  bool sortDescending() const { return sort_order_ == Qt::DescendingOrder; }
  void setSortDescending(bool descending);

 signals:
  void hiddenVisibleChanged();
  void sortDescendingChanged();

 protected:
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

 private:
  QCollator collator_;
  Qt::SortOrder sort_order_ = Qt::AscendingOrder;
  bool hidden_visible_ = false;
};
