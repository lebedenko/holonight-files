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
  void setEditing(bool editing) { editing_ = editing; }
  bool hiddenVisible() const { return hidden_visible_; }
  void setHiddenVisible(bool visible);
  bool sortDescending() const { return sort_order_ == Qt::DescendingOrder; }
  void setSortDescending(bool descending);
  // Pins the source row at sourceRow (an INSERT-mode o/O create placeholder) to sort immediately
  // adjacent to the entry named anchorName, instead of by its own (empty) name — the placeholder
  // borrows anchorName/anchorIsDir as its sort key and is tie-broken to land right after
  // (below=true) or right before (below=false) the anchor (SPEC.md REQ-F-010/011). Real entries
  // never share a name, so this tie only ever occurs between the placeholder and its anchor.
  // Call with sourceRow < 0 to release the pin. Must be set before the placeholder row is
  // inserted into the source model, so it sorts correctly the moment it appears.
  void setPlaceholder(int sourceRow, const QString& anchorName, bool anchorIsDir, bool below);

 signals:
  void hiddenVisibleChanged();
  void sortDescendingChanged();

 protected:
  bool lessThan(const QModelIndex& left, const QModelIndex& right) const override;
  bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

 private:
  QCollator collator_;
  Qt::SortOrder sort_order_ = Qt::AscendingOrder;
  bool editing_ = false;
  bool hidden_visible_ = false;
  int placeholder_source_row_ = -1;
  QString placeholder_anchor_name_;
  bool placeholder_anchor_is_dir_ = false;
  bool placeholder_below_ = false;
};
