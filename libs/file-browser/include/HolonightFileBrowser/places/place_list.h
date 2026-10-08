#pragma once

#include <QHash>
#include <QString>

#include <memory>
#include <vector>

// The one startup list of standard places (Home + XDG user directories) that both the sidebar
// (PlacesModel) and the directory listing (DirectoryModel, via IconMap) are derived from
// (named-folder-icons REQ-F-001, REQ-C-001). Pure and QObject-free; the only file I/O is
// UserDirsParser::parseFile().
namespace PlaceList {

struct StandardPlace {
  QString name;       // translated sidebar label (context "PlacesModel")
  QString path;       // QDir::cleanPath()'d absolute path
  QString icon_name;  // "user-home" for Home, UserDirsParser::iconName(key) otherwise
  bool is_home = false;
};

// cleaned absolute path -> named icon name. Built once, never mutated afterwards.
using IconMap = QHash<QString, QString>;

// Home first, then UserDirsParser::parseFile(userDirsFilePath, cleanedHome) in its Key order. An entry
// whose path was already seen is dropped silently (two XDG_*_DIR keys aliasing one path). A missing
// user-dirs.dirs yields the Home-only list.
[[nodiscard]] std::vector<StandardPlace> standardPlaces(const QString& homePath, const QString& userDirsFilePath);

// Keyed by StandardPlace::path with no filtering or transformation. Never null.
[[nodiscard]] std::shared_ptr<const IconMap> iconMap(const std::vector<StandardPlace>& places);

}  // namespace PlaceList
