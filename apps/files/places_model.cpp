#include "places_model.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

PlacesModel::PlacesModel(QObject* parent) : PlacesModel(QStandardPaths::writableLocation, parent) {}

PlacesModel::PlacesModel(const LocationProvider& location, QObject* parent) : QAbstractListModel(parent) {
  const auto append = [this](const QString& name, const QString& path, const QString& icon) {
    if (!path.isEmpty()) {
      places_.append({.name = name, .path = QDir::cleanPath(path), .iconName = icon + "/folder/inode-directory"});
    }
  };
  const auto home = location(QStandardPaths::HomeLocation);
  append(tr("Home"), home, "user-home");
  append(tr("Documents"), location(QStandardPaths::DocumentsLocation), "folder-documents");
  append(tr("Downloads"), location(QStandardPaths::DownloadLocation), "folder-download");
  append(tr("Pictures"), location(QStandardPaths::PicturesLocation), "folder-pictures");
  append(tr("Music"), location(QStandardPaths::MusicLocation), "folder-music");
  append(tr("Videos"), location(QStandardPaths::MoviesLocation), "folder-videos");
  if (!home.isEmpty() && QFileInfo(QDir(home).filePath("Projects")).isDir()) {
    append(tr("Projects"), QDir(home).filePath("Projects"), "folder-development");
  }
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
  if (role == IconNameRole) {
    return place.iconName;
  }
  return {};
}
QHash<int, QByteArray> PlacesModel::roleNames() const {
  return {{NameRole, "name"}, {PathRole, "path"}, {IconNameRole, "iconName"}};
}
