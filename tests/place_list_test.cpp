#include "places/place_list.h"

#include "directory_fixtures.h"

#include <QDir>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using files_test::writeFile;

namespace {
constexpr auto kHome = "/home/tester";

QString userDirsFile(const QTemporaryDir& dir, const QString& text) {
  return writeFile(dir, QStringLiteral("user-dirs.dirs"), text.toUtf8());
}
}  // namespace

TEST(PlaceList, HomeComesFirstWithUserHomeIcon) {
  const auto places = PlaceList::standardPlaces(QString(kHome) + "/", QStringLiteral("/nonexistent/user-dirs.dirs"));
  ASSERT_EQ(places.size(), 1U);
  EXPECT_TRUE(places[0].is_home);
  EXPECT_EQ(places[0].path, QString(kHome));
  EXPECT_EQ(places[0].icon_name, QStringLiteral("user-home"));
}

TEST(PlaceList, XdgEntriesFollowHomeInKeyOrder) {
  const QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto file = userDirsFile(dir, QStringLiteral("XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
                                                     "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n"));
  const auto places = PlaceList::standardPlaces(QString(kHome), file);
  ASSERT_EQ(places.size(), 3U);
  EXPECT_TRUE(places[0].is_home);
  EXPECT_EQ(places[1].path, QString(kHome) + "/Documents");
  EXPECT_EQ(places[1].icon_name, QStringLiteral("folder-documents"));
  EXPECT_FALSE(places[1].is_home);
  EXPECT_EQ(places[2].path, QString(kHome) + "/Downloads");
  EXPECT_EQ(places[2].icon_name, QStringLiteral("folder-download"));
}

TEST(PlaceList, DuplicatePathsAreDroppedSilently) {
  const QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto file = userDirsFile(dir, QStringLiteral("XDG_DOCUMENTS_DIR=\"$HOME/Same\"\n"
                                                     "XDG_MUSIC_DIR=\"$HOME/Same\"\n"));
  const auto places = PlaceList::standardPlaces(QString(kHome), file);
  ASSERT_EQ(places.size(), 2U);
  EXPECT_EQ(places[1].icon_name, QStringLiteral("folder-documents"));
}

TEST(PlaceList, IconMapIsKeyedByPlacePath) {
  const QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const auto file = userDirsFile(dir, QStringLiteral("XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n"));
  const auto places = PlaceList::standardPlaces(QString(kHome), file);
  const auto map = PlaceList::iconMap(places);
  ASSERT_NE(map, nullptr);
  ASSERT_EQ(map->size(), static_cast<qsizetype>(places.size()));
  for (const auto& place : places) {
    EXPECT_EQ(map->value(place.path), place.icon_name);
  }
  EXPECT_EQ(map->value(QString(kHome)), QStringLiteral("user-home"));
}

TEST(PlaceList, IconMapOfEmptyListIsNonNullAndEmpty) {
  const auto map = PlaceList::iconMap({});
  ASSERT_NE(map, nullptr);
  EXPECT_TRUE(map->isEmpty());
}
