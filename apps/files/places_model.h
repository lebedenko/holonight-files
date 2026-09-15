#pragma once

#include <QAbstractListModel>
#include <QStandardPaths>
#include <QtQml/qqmlregistration.h>

#include <functional>

// Startup snapshot of standard places and the optional ~/Projects directory.
class PlacesModel : public QAbstractListModel {
  Q_OBJECT
  QML_ELEMENT
  QML_UNCREATABLE("Created by the application")
 public:
  enum Role { NameRole = Qt::UserRole + 1, PathRole, IconNameRole };
  explicit PlacesModel(QObject* parent = nullptr);
  using LocationProvider = std::function<QString(QStandardPaths::StandardLocation)>;
  explicit PlacesModel(const LocationProvider& location, QObject* parent = nullptr);
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;

 private:
  struct Place {
    QString name;
    QString path;
    QString iconName;
  };
  QList<Place> places_;
};
