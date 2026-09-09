#pragma once

#include <QAbstractListModel>
#include <QtQml/qqmlregistration.h>

// Fixed, hardcoded list of standard places (Home, Documents, Downloads, Pictures). No
// removable-media discovery in this stage — see docs/sdd/browse-folder/SPEC.md REQ-F-008.
class PlacesModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the application")
 public:
  enum Role { NameRole = Qt::UserRole + 1, PathRole };
  explicit PlacesModel(QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

 private:
  struct Place {
    QString name;
    QString path;
  };
  QList<Place> places_;
};
