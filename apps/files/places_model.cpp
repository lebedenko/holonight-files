#include "places_model.h"

#include <QDir>
#include <QStandardPaths>

namespace {
QString location(QStandardPaths::StandardLocation type) {
  return QDir::cleanPath(QStandardPaths::writableLocation(type));
}
}  // namespace

PlacesModel::PlacesModel(QObject* parent) : QAbstractListModel(parent) {
  places_ = {
      {.name = QObject::tr("Home"), .path = location(QStandardPaths::HomeLocation)},
      {.name = QObject::tr("Documents"), .path = location(QStandardPaths::DocumentsLocation)},
      {.name = QObject::tr("Downloads"), .path = location(QStandardPaths::DownloadLocation)},
      {.name = QObject::tr("Pictures"), .path = location(QStandardPaths::PicturesLocation)},
  };
}
int PlacesModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(places_.size());
}
QVariant PlacesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.model() != this || index.row() >= places_.size()) {
    return {};
  }
  const auto& place = places_[index.row()];
  if (role == NameRole) {
    return place.name;
  }
  if (role == PathRole) {
    return place.path;
  }
  return {};
}
QHash<int, QByteArray> PlacesModel::roleNames() const { return {{NameRole, "name"}, {PathRole, "path"}}; }
