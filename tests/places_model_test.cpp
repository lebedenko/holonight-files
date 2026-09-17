#include "places_model.h"

#include "directory_fixtures.h"
#include "settings_fixtures.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QTimer>
#include <QTranslator>

#include <gtest/gtest.h>
#include <memory>

using Origin = PlacesModel::Origin;
using Status = PlacesModel::Status;
using files_test::FakePlaceAvailabilityChecker;
using files_test::fixturePattern;
using files_test::RecordingWarningSink;
using files_test::writeFile;

struct PlacesModelTestAccess {
  static void deliverStartupResult(PlacesModel& model, int row, bool available) {
    model.deliverResult(model.places_[row].id, 0, available);
  }
};

namespace {

class PlaceTranslator : public QTranslator {
 public:
  [[nodiscard]] bool isEmpty() const override { return false; }
  QString translate(const char* context, const char* source, const char* /*disambiguation*/, int /*n*/) const override {
    return QByteArray(context) == "PlacesModel" ? QString("translated-%1").arg(source) : QString();
  }
};

// Builds a PlacesModel over an isolated fixture: a temp "home" directory, an optional
// user-dirs.dirs and an optional places.toml, with an injected checker/warning sink.
struct ModelFixture {
  QTemporaryDir home{fixturePattern("places-model")};
  QTemporaryDir config{fixturePattern("places-model-config")};
  QTemporaryDir data{fixturePattern("places-model-data")};
  std::shared_ptr<FakePlaceAvailabilityChecker> checker = std::make_shared<FakePlaceAvailabilityChecker>();
  std::shared_ptr<RecordingWarningSink> warnings = std::make_shared<RecordingWarningSink>();

  [[nodiscard]] bool valid() const { return home.isValid() && config.isValid() && data.isValid(); }
  [[nodiscard]] QString userDirsPath() const { return config.filePath("user-dirs.dirs"); }
  [[nodiscard]] QString placesPath() const { return data.filePath("places.toml"); }
  void writeUserDirs(const QByteArray& content) const { writeFile(config, "user-dirs.dirs", content); }
  void writePlaces(const QByteArray& content) const { writeFile(data, "places.toml", content); }

  [[nodiscard]] std::unique_ptr<PlacesModel> build() const {
    return std::make_unique<PlacesModel>(home.path(), userDirsPath(), placesPath(), checker, warnings, nullptr);
  }
};

QModelIndex idx(const PlacesModel& model, int row) { return model.index(row); }

}  // namespace

TEST(PlacesModel, HomeIsAlwaysFirstRowCheckingThenAvailable) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  const auto model = fixture.build();
  ASSERT_GE(model->rowCount(), 1);
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::NameRole).toString(), "Home");
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::PathRole).toString(), fixture.home.path());
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::IconNameRole).toString(), "user-home");
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::OriginRole).value<Origin>(), Origin::Home);
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::StatusRole).value<Status>(), Status::Checking);
}

TEST(PlacesModel, XdgDirectoriesOrderedTranslatedAndIconed) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  for (const auto& name :
       {"Desktop", "Documents", "Downloads", "Pictures", "Music", "Videos", "Projects", "Templates", "Public"}) {
    QDir(fixture.home.path()).mkdir(name);
  }
  fixture.writeUserDirs(
      "XDG_PUBLICSHARE_DIR=\"$HOME/Public\"\n"
      "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
      "XDG_PROJECTS_DIR=\"$HOME/Projects\"\n"
      "XDG_TEMPLATES_DIR=\"$HOME/Templates\"\n"
      "XDG_VIDEOS_DIR=\"$HOME/Videos\"\n"
      "XDG_MUSIC_DIR=\"$HOME/Music\"\n"
      "XDG_PICTURES_DIR=\"$HOME/Pictures\"\n"
      "XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
      "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n");
  PlaceTranslator translator;
  QCoreApplication::installTranslator(&translator);
  const auto model = fixture.build();
  QCoreApplication::removeTranslator(&translator);
  ASSERT_EQ(model->rowCount(), 10);
  const QStringList names{"translated-Home",      "translated-Desktop", "translated-Documents", "translated-Downloads",
                          "translated-Pictures",  "translated-Music",   "translated-Videos",    "translated-Projects",
                          "translated-Templates", "translated-Public"};
  const QStringList icons{"user-home",        "user-desktop",      "folder-documents", "folder-download",
                          "folder-pictures",  "folder-music",      "folder-videos",    "folder-development",
                          "folder-templates", "folder-publicshare"};
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::OriginRole).value<Origin>(), Origin::Home);
  for (int i = 0; i < names.size(); ++i) {
    EXPECT_EQ(model->data(idx(*model, i), PlacesModel::NameRole).toString(), names[i]) << i;
    EXPECT_EQ(model->data(idx(*model, i), PlacesModel::IconNameRole).toString(), icons[i]) << i;
    if (i > 0) {
      EXPECT_EQ(model->data(idx(*model, i), PlacesModel::OriginRole).value<Origin>(), Origin::XdgUserDirectory) << i;
    }
  }
}

TEST(PlacesModel, MissingUserDirsFileHasNoFallbackOnlyHomeAndBookmarks) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm").toUtf8() + "\"\n");
  QDir(fixture.home.path()).mkdir("bm");
  const auto model = fixture.build();
  ASSERT_EQ(model->rowCount(), 2);
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::OriginRole).value<Origin>(), Origin::Home);
  EXPECT_EQ(model->data(idx(*model, 1), PlacesModel::OriginRole).value<Origin>(), Origin::Bookmark);
}

TEST(PlacesModel, MissingPlacesFileIsSilentAndCreatesNothing) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  const auto model = fixture.build();
  for (int i = 0; i < model->rowCount(); ++i) {
    EXPECT_NE(model->data(idx(*model, i), PlacesModel::OriginRole).value<Origin>(), Origin::Bookmark);
  }
  EXPECT_FALSE(QFile::exists(fixture.placesPath()));
  EXPECT_TRUE(fixture.warnings->messages.isEmpty());
}

TEST(PlacesModel, BookmarksLoadInFileOrderAfterXdgWithSpacingFlag) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("Desktop");
  fixture.writeUserDirs("XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n");
  QDir(fixture.home.path()).mkdir("bm1");
  QDir(fixture.home.path()).mkdir("bm2");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm1").toUtf8() +
                      "\"\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm2").toUtf8() + "\"\n");
  const auto model = fixture.build();
  ASSERT_EQ(model->rowCount(), 4);  // Home, Desktop, bm1, bm2
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::StartsBookmarksRole).toBool(), false);
  EXPECT_EQ(model->data(idx(*model, 1), PlacesModel::StartsBookmarksRole).toBool(), false);
  EXPECT_EQ(model->data(idx(*model, 2), PlacesModel::StartsBookmarksRole).toBool(), true);
  EXPECT_EQ(model->data(idx(*model, 2), PlacesModel::OriginRole).value<Origin>(), Origin::Bookmark);
  EXPECT_EQ(model->data(idx(*model, 3), PlacesModel::StartsBookmarksRole).toBool(), false);
  EXPECT_EQ(model->data(idx(*model, 3), PlacesModel::OriginRole).value<Origin>(), Origin::Bookmark);
}

TEST(PlacesModel, DuplicateXdgEntriesAreDroppedSilentlyFirstWins) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("Stuff");
  fixture.writeUserDirs("XDG_DESKTOP_DIR=\"$HOME/Stuff\"\nXDG_DOCUMENTS_DIR=\"$HOME/Stuff\"\n");
  const auto model = fixture.build();
  ASSERT_EQ(model->rowCount(), 2);  // Home + Desktop only
  EXPECT_EQ(model->data(idx(*model, 1), PlacesModel::NameRole).toString(), "Desktop");
  EXPECT_TRUE(fixture.warnings->messages.isEmpty());
}

TEST(PlacesModel, BookmarkDuplicatingXdgIsDroppedWithOneWarning) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("Downloads");
  fixture.writeUserDirs("XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("Downloads").toUtf8() + "\"\n");
  const auto model = fixture.build();
  ASSERT_EQ(model->rowCount(), 2);  // Home + Downloads (XDG); bookmark dropped
  ASSERT_EQ(fixture.warnings->messages.size(), 1);
  EXPECT_TRUE(fixture.warnings->messages.front().contains("Downloads"));
}

TEST(PlacesModel, BookmarkDuplicatingHomeAndRepeatedBookmarkEachWarnOnce) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("other");
  const auto other = fixture.home.filePath("other").toUtf8();
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.path().toUtf8() +
                      "\"\n[[bookmarks]]\npath = \"" + other + "\"\n[[bookmarks]]\npath = \"" + other + "\"\n");
  const auto model = fixture.build();
  ASSERT_EQ(model->rowCount(), 2);  // Home + first "other" bookmark
  ASSERT_EQ(fixture.warnings->messages.size(), 2);
  EXPECT_TRUE(fixture.warnings->messages[0].contains("Home"));
  EXPECT_TRUE(fixture.warnings->messages[1].contains("earlier bookmark"));
}

TEST(PlacesModel, NamesAreNotDeduplicated) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("a");
  QDir(fixture.home.path()).mkdir("b");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("a").toUtf8() +
                      "\"\nname = \"Same\"\n[[bookmarks]]\npath = \"" + fixture.home.filePath("b").toUtf8() +
                      "\"\nname = \"Same\"\n");
  const auto model = fixture.build();
  ASSERT_EQ(model->rowCount(), 3);
  EXPECT_EQ(model->data(idx(*model, 1), PlacesModel::NameRole).toString(), "Same");
  EXPECT_EQ(model->data(idx(*model, 2), PlacesModel::NameRole).toString(), "Same");
}

TEST(PlacesModel, SymlinkAndTargetBothRetainedNoCanonicalisation) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  const auto target = fixture.home.filePath("target");
  QDir(fixture.home.path()).mkdir("target");
  const auto link = fixture.home.filePath("link");
  ASSERT_TRUE(QFile::link(target, link));
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + target.toUtf8() + "\"\n[[bookmarks]]\npath = \"" +
                      link.toUtf8() + "\"\n");
  const auto model = fixture.build();
  ASSERT_EQ(model->rowCount(), 3);
  EXPECT_EQ(model->data(idx(*model, 1), PlacesModel::PathRole).toString(), target);
  EXPECT_EQ(model->data(idx(*model, 2), PlacesModel::PathRole).toString(), link);
}

TEST(PlacesModel, RolesRowCountAndOriginValues) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("Desktop");
  fixture.writeUserDirs("XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n");
  QDir(fixture.home.path()).mkdir("bm");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm").toUtf8() + "\"\n");
  const auto model = fixture.build();
  const auto roles = model->roleNames();
  EXPECT_EQ(roles.value(PlacesModel::NameRole), "name");
  EXPECT_EQ(roles.value(PlacesModel::PathRole), "path");
  EXPECT_EQ(roles.value(PlacesModel::IconNameRole), "iconName");
  EXPECT_EQ(roles.value(PlacesModel::OriginRole), "origin");
  EXPECT_EQ(roles.value(PlacesModel::StatusRole), "status");
  EXPECT_EQ(roles.value(PlacesModel::StartsBookmarksRole), "startsBookmarks");
  ASSERT_EQ(model->rowCount(), 3);
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::OriginRole).value<Origin>(), Origin::Home);
  EXPECT_EQ(model->data(idx(*model, 1), PlacesModel::OriginRole).value<Origin>(), Origin::XdgUserDirectory);
  EXPECT_EQ(model->data(idx(*model, 2), PlacesModel::OriginRole).value<Origin>(), Origin::Bookmark);
}

TEST(PlacesModel, XdgNonDirectoryIsRemovedBookmarkNonDirectoryStaysUnavailable) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  fixture.writeUserDirs("XDG_DESKTOP_DIR=\"$HOME/missing-desktop\"\n");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("missing-bm").toUtf8() + "\"\n");
  fixture.checker->default_result = false;                 // nothing exists in this fixture
  fixture.checker->overrides[fixture.home.path()] = true;  // Home still resolves independently below
  const auto model = fixture.build();
  ASSERT_TRUE(QTest::qWaitFor([&] { return model->rowCount() == 2; }));  // Home + bookmark; XDG removed
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::OriginRole).value<Origin>(), Origin::Home);
  EXPECT_EQ(model->data(idx(*model, 1), PlacesModel::OriginRole).value<Origin>(), Origin::Bookmark);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 1), PlacesModel::StatusRole).value<Status>() == Status::Unavailable; }));
}

TEST(PlacesModel, HomeBecomesAvailableEvenWhenItsOwnCheckReturnsFalse) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  fixture.checker->default_result = false;
  const auto model = fixture.build();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 0), PlacesModel::StatusRole).value<Status>() == Status::Available; }));
  ASSERT_EQ(model->rowCount(), 1);  // never removed
}

TEST(PlacesModel, ChecksRunOffTheGuiThread) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  const auto model = fixture.build();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !fixture.checker->paths().empty(); }));
  for (auto* thread : fixture.checker->threads()) {
    EXPECT_NE(thread, QThread::currentThread());
  }
}

TEST(PlacesModel, HungCheckDoesNotBlockOtherChecksOrConstruction) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("bm");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm").toUtf8() + "\"\n");
  const auto gate = std::make_shared<QSemaphore>();
  fixture.checker->gates[fixture.home.path()] = gate;  // Home's check hangs
  const auto model = fixture.build();                  // must return immediately despite the gated Home check
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 1), PlacesModel::StatusRole).value<Status>() != Status::Checking; }));
  EXPECT_EQ(model->data(idx(*model, 0), PlacesModel::StatusRole).value<Status>(), Status::Checking);
  gate->release();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 0), PlacesModel::StatusRole).value<Status>() == Status::Available; }));
}

TEST(PlacesModel, GuiThreadKeepsRunningWhileChecksAreGated) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  const auto gate = std::make_shared<QSemaphore>();
  fixture.checker->gates[fixture.home.path()] = gate;
  const auto model = fixture.build();
  ASSERT_EQ(model->rowCount(), 1);  // constructor already returned
  int ticks = 0;
  QTimer timer;
  timer.setInterval(5);
  QObject::connect(&timer, &QTimer::timeout, [&] { ++ticks; });
  timer.start();
  QTest::qWait(60);
  EXPECT_GT(ticks, 0);
  gate->release();
}

TEST(PlacesModel, FailedXdgCheckEmitsExactlyOneRowsRemoved) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  fixture.writeUserDirs("XDG_DESKTOP_DIR=\"$HOME/missing\"\n");
  fixture.checker->overrides[fixture.home.filePath("missing")] = false;
  const auto model = fixture.build();
  QSignalSpy removedSpy(model.get(), &QAbstractItemModel::rowsRemoved);
  ASSERT_TRUE(QTest::qWaitFor([&] { return model->rowCount() == 1; }));
  EXPECT_EQ(removedSpy.count(), 1);
}

TEST(PlacesModel, FailedBookmarkCheckEmitsExactlyOneDataChangedToUnavailable) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("missing-bm").toUtf8() + "\"\n");
  const auto gate = std::make_shared<QSemaphore>();
  fixture.checker->gates[fixture.home.filePath("missing-bm")] = gate;  // hold the bookmark's own check
  fixture.checker->overrides[fixture.home.filePath("missing-bm")] = false;
  const auto model = fixture.build();
  // Let Home's independent check settle first, so only the bookmark's own transition is spied on.
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 0), PlacesModel::StatusRole).value<Status>() == Status::Available; }));
  QSignalSpy changedSpy(model.get(), &QAbstractItemModel::dataChanged);
  gate->release();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 1), PlacesModel::StatusRole).value<Status>() == Status::Unavailable; }));
  EXPECT_EQ(changedSpy.count(), 1);
}

TEST(PlacesModel, DestroyingModelWithGatedCheckThenReleasingCausesNoCrashOrDelivery) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  const auto gate = std::make_shared<QSemaphore>();
  fixture.checker->gates[fixture.home.path()] = gate;
  {
    const auto model = fixture.build();
    ASSERT_TRUE(QTest::qWaitFor([&] { return !fixture.checker->paths().empty(); }));
  }  // model destroyed while its check is still gated
  gate->release();
  QTest::qWait(50);  // give the detached thread a chance to (not) deliver; no crash means pass
  SUCCEED();
}

TEST(PlacesModel, RecheckBookmarkReturnsIdForBookmarkAndZeroOtherwise) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("bm");
  fixture.writeUserDirs("XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm").toUtf8() + "\"\n");
  const auto model = fixture.build();
  EXPECT_EQ(model->recheckBookmark(0), 0U);   // Home
  EXPECT_EQ(model->recheckBookmark(1), 0U);   // XDG
  EXPECT_NE(model->recheckBookmark(2), 0U);   // Bookmark
  EXPECT_EQ(model->recheckBookmark(99), 0U);  // out of range
}

TEST(PlacesModel, AvailableBookmarkStaysAvailableUntilRecheckedThenBecomesUnavailable) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("bm");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm").toUtf8() + "\"\n");
  const auto model = fixture.build();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 1), PlacesModel::StatusRole).value<Status>() == Status::Available; }));
  fixture.checker->overrides[fixture.home.filePath("bm")] = false;
  model->recheckBookmark(1);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 1), PlacesModel::StatusRole).value<Status>() == Status::Unavailable; }));
}

TEST(PlacesModel, TwoRapidRechecksEmitBookmarkRecheckResolvedOnceWithFreshestResult) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("bm");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm").toUtf8() + "\"\n");
  const auto model = fixture.build();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 1), PlacesModel::StatusRole).value<Status>() != Status::Checking; }));
  const auto gate = std::make_shared<QSemaphore>();
  fixture.checker->gates[fixture.home.filePath("bm")] = gate;
  QSignalSpy resolvedSpy(model.get(), &PlacesModel::bookmarkRecheckResolved);
  model->recheckBookmark(1);  // gated: superseded before it resolves
  QTest::qWait(20);
  fixture.checker->gates.remove(fixture.home.filePath("bm"));
  const auto secondId = model->recheckBookmark(1);
  gate->release();  // let the first, now-superseded check proceed too
  ASSERT_TRUE(QTest::qWaitFor([&] { return resolvedSpy.count() >= 1; }));
  QTest::qWait(30);  // give any (unwanted) second delivery a chance to arrive
  EXPECT_EQ(resolvedSpy.count(), 1);
  EXPECT_EQ(resolvedSpy.constFirst().at(0).value<quint64>(), secondId);
}

TEST(PlacesModel, StartupResultCannotOverwriteCompletedBookmarkRecheck) {
  for (const bool available : {false, true}) {
    ModelFixture fixture;
    ASSERT_TRUE(fixture.valid());
    fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"/bookmark\"\n");
    fixture.checker->default_result = available;
    const auto model = fixture.build();
    ASSERT_TRUE(QTest::qWaitFor(
        [&] { return model->data(idx(*model, 1), PlacesModel::StatusRole).value<Status>() != Status::Checking; }));
    QSignalSpy resolved(model.get(), &PlacesModel::bookmarkRecheckResolved);
    model->recheckBookmark(1);
    ASSERT_TRUE(QTest::qWaitFor([&] { return resolved.count() == 1; }));
    QSignalSpy changed(model.get(), &QAbstractItemModel::dataChanged);
    // Deterministically deliver an older startup observation after the activation result.
    PlacesModelTestAccess::deliverStartupResult(*model, 1, !available);
    EXPECT_EQ(model->data(idx(*model, 1), PlacesModel::StatusRole).value<Status>(),
              available ? Status::Available : Status::Unavailable);
    EXPECT_EQ(changed.count(), 0);
    EXPECT_EQ(resolved.count(), 1);
  }
}

TEST(PlacesModel, RecheckBookmarkIdStaysCorrectAfterEarlierXdgRowRemoved) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  fixture.writeUserDirs("XDG_DESKTOP_DIR=\"$HOME/missing\"\n");
  fixture.checker->overrides[fixture.home.filePath("missing")] = false;
  QDir(fixture.home.path()).mkdir("bm");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm").toUtf8() + "\"\n");
  const auto model = fixture.build();
  ASSERT_TRUE(QTest::qWaitFor([&] { return model->rowCount() == 2; }));  // XDG row removed
  const auto placeId = model->recheckBookmark(1);
  EXPECT_NE(placeId, 0U);
  QSignalSpy resolvedSpy(model.get(), &PlacesModel::bookmarkRecheckResolved);
  ASSERT_TRUE(QTest::qWaitFor([&] { return resolvedSpy.count() >= 1; }));
  EXPECT_EQ(resolvedSpy.constFirst().at(0).value<quint64>(), placeId);
}

TEST(PlacesModel, NoWarningsDuringAvailabilityChecksOrRechecks) {
  ModelFixture fixture;
  ASSERT_TRUE(fixture.valid());
  QDir(fixture.home.path()).mkdir("other");
  const auto other = fixture.home.filePath("other").toUtf8();
  QDir(fixture.home.path()).mkdir("bm");
  fixture.writePlaces("version = 1\n[[bookmarks]]\npath = \"" + other + "\"\n[[bookmarks]]\npath = \"" + other +
                      "\"\n[[bookmarks]]\npath = \"" + fixture.home.filePath("bm").toUtf8() + "\"\n");
  const auto model = fixture.build();
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return model->data(idx(*model, 2), PlacesModel::StatusRole).value<Status>() != Status::Checking; }));
  const auto countAfterConstruction = fixture.warnings->messages.size();
  EXPECT_EQ(countAfterConstruction, 1);  // the one duplicate-bookmark warning
  model->recheckBookmark(2);
  QTest::qWait(30);
  model->recheckBookmark(2);
  QTest::qWait(30);
  EXPECT_EQ(fixture.warnings->messages.size(), countAfterConstruction);
}
