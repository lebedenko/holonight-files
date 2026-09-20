#pragma once

#include <QAbstractListModel>
#include <QStringList>

// Storage-only list model behind the Quick Look text viewer. One row per line; navigation and the
// current-line state live in PreviewService, never here.
class TextLineModel : public QAbstractListModel {
  Q_OBJECT

 public:
  enum Role { LineTextRole = Qt::UserRole + 1, LineNumberRole };

  explicit TextLineModel(QObject* parent = nullptr);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

  void setLines(QStringList lines);
  // No-op when the model is already empty, so an idle clear never resets attached views.
  void clear();

 private:
  QStringList lines_;
};
