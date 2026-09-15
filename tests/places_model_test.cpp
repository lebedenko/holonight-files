#include "places_model.h"

#include "directory_fixtures.h"

#include <QCoreApplication>
#include <QTranslator>

#include <gtest/gtest.h>

namespace {
class PlaceTranslator : public QTranslator {
 public:
  [[nodiscard]] bool isEmpty() const override { return false; }
  QString translate(const char* context, const char* source, const char* /*disambiguation*/, int /*n*/) const override {
    return QByteArray(context) == "PlacesModel" ? QString("translated-%1").arg(source) : QString();
  }
};
}  // namespace

TEST(PlacesModel, OrderedTranslatedLocationsIconsAndStartupProjects) {
  QTemporaryDir home(files_test::fixturePattern("places"));
  ASSERT_TRUE(home.isValid());
  const auto location = [&](QStandardPaths::StandardLocation type) {
    return type == QStandardPaths::HomeLocation ? home.path() : home.filePath(QString::number(type));
  };
  PlaceTranslator translator;
  QCoreApplication::installTranslator(&translator);
  PlacesModel absent(location);
  QCoreApplication::removeTranslator(&translator);
  const QStringList names{"Home", "Documents", "Downloads", "Pictures", "Music", "Videos"};
  const QList<QStandardPaths::StandardLocation> types{
      QStandardPaths::HomeLocation,     QStandardPaths::DocumentsLocation, QStandardPaths::DownloadLocation,
      QStandardPaths::PicturesLocation, QStandardPaths::MusicLocation,     QStandardPaths::MoviesLocation};
  const QStringList icons{"user-home",       "folder-documents", "folder-download",
                          "folder-pictures", "folder-music",     "folder-videos"};
  ASSERT_EQ(absent.rowCount(), names.size());
  for (int i = 0; i < names.size(); ++i) {
    EXPECT_EQ(absent.data(absent.index(i), PlacesModel::NameRole).toString(), "translated-" + names[i]);
    EXPECT_EQ(absent.data(absent.index(i), PlacesModel::PathRole).toString(), location(types[i]));
    EXPECT_EQ(absent.data(absent.index(i), PlacesModel::IconNameRole).toString(), icons[i] + "/folder/inode-directory");
  }
  EXPECT_EQ(absent.roleNames().value(PlacesModel::IconNameRole), "iconName");
  ASSERT_FALSE(files_test::writeFile(home, "Projects").isEmpty());
  EXPECT_EQ(PlacesModel(location).rowCount(), 6);
  ASSERT_TRUE(QFile::remove(home.filePath("Projects")));
  ASSERT_TRUE(QDir().mkdir(home.filePath("Projects")));
  PlacesModel present(location);
  ASSERT_EQ(present.rowCount(), 7);
  EXPECT_EQ(present.data(present.index(6), PlacesModel::PathRole).toString(), home.filePath("Projects"));
  EXPECT_EQ(present.data(present.index(6), PlacesModel::NameRole).toString(), "Projects");
  EXPECT_EQ(absent.rowCount(), 6);
}

TEST(PlacesModel, EmptyLocationsAreOmittedBeforeCleaning) {
  PlacesModel empty([](QStandardPaths::StandardLocation) { return QString(); });
  EXPECT_EQ(empty.rowCount(), 0);
  EXPECT_FALSE(empty.data({}, PlacesModel::NameRole).isValid());
  PlacesModel missing([](QStandardPaths::StandardLocation type) {
    return type == QStandardPaths::MusicLocation ? QString("/missing/../missing/music") : QString();
  });
  ASSERT_EQ(missing.rowCount(), 1);
  EXPECT_EQ(missing.data(missing.index(0), PlacesModel::PathRole).toString(), "/missing/music");
}
