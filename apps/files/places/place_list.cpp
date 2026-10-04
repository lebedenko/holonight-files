#include "places/place_list.h"

#include "places/user_dirs_parser.h"

#include <QCoreApplication>
#include <QDir>
#include <QSet>

namespace PlaceList {

std::vector<StandardPlace> standardPlaces(const QString& homePath, const QString& userDirsFilePath) {
  const auto cleanedHome = QDir::cleanPath(homePath);
  std::vector<StandardPlace> places;
  QSet<QString> seen;
  places.push_back({
      .name = QCoreApplication::translate("PlacesModel", "Home"),
      .path = cleanedHome,
      .icon_name = QStringLiteral("user-home"),
      .is_home = true,
  });
  seen.insert(cleanedHome);
  for (const auto& entry : UserDirsParser::parseFile(userDirsFilePath, cleanedHome)) {
    if (seen.contains(entry.path)) {
      continue;
    }
    seen.insert(entry.path);
    places.push_back({
        .name = UserDirsParser::label(entry.key),
        .path = entry.path,
        .icon_name = UserDirsParser::iconName(entry.key),
        .is_home = false,
    });
  }
  return places;
}

std::shared_ptr<const IconMap> iconMap(const std::vector<StandardPlace>& places) {
  auto map = std::make_shared<IconMap>();
  for (const auto& place : places) {
    map->insert(place.path, place.icon_name);
  }
  return map;
}

}  // namespace PlaceList
