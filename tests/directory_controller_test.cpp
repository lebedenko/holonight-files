#include "directory_controller.h"

#include "directory_fixtures.h"

#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeFile;

namespace {
bool settled(const DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.scanning(); });
}
}  // namespace

TEST(DirectoryController, JAndKMoveTheCursorByOneAndClampAtBoundaries) {
  QTemporaryDir dir(fixturePattern("ctrl-jk"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  writeFile(dir, "b.txt");
  writeFile(dir, "c.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.handleKey("k"));  // already at top; stays clamped
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 1);
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 2);
  EXPECT_TRUE(controller.handleKey("j"));  // already at bottom; stays clamped
  EXPECT_EQ(controller.cursorRow(), 2);
  EXPECT_TRUE(controller.handleKey("k"));
  EXPECT_EQ(controller.cursorRow(), 1);
}

TEST(DirectoryController, NumericPrefixAppliesAsMotionCount) {
  QTemporaryDir dir(fixturePattern("ctrl-count"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 10; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("5"));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 5);
  EXPECT_TRUE(controller.handleKey("2"));
  EXPECT_TRUE(controller.handleKey("k"));
  EXPECT_EQ(controller.cursorRow(), 3);
  // Multi-digit counts accumulate: "1" then "0" then "j" means count 10, not two separate motions.
  EXPECT_TRUE(controller.handleKey("1"));
  EXPECT_TRUE(controller.handleKey("0"));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 9);  // clamped to the last row (10 entries)
}

TEST(DirectoryController, CountBufferResetsAfterAnyKeyMotionOrNot) {
  QTemporaryDir dir(fixturePattern("ctrl-count-reset"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 10; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("5"));
  EXPECT_FALSE(controller.handleKey("x"));  // unrecognized key: still consumes the pending count
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 1);  // count was discarded, not carried over to this j
}

TEST(DirectoryController, GgJumpsToFirstRowAndGJumpsToLastRow) {
  QTemporaryDir dir(fixturePattern("ctrl-gg"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 5; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("G"));
  EXPECT_EQ(controller.cursorRow(), 4);
  EXPECT_TRUE(controller.handleKey("g"));
  EXPECT_TRUE(controller.handleKey("g"));
  EXPECT_EQ(controller.cursorRow(), 0);
}

TEST(DirectoryController, PendingGExpiresAfterTimeoutInsteadOfJumping) {
  QTemporaryDir dir(fixturePattern("ctrl-gg-timeout"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 5; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("j");
  controller.handleKey("j");
  ASSERT_EQ(controller.cursorRow(), 2);
  EXPECT_TRUE(controller.handleKey("g"));
  QTest::qWait(700);  // exceeds the 600ms pending-g timeout
  EXPECT_TRUE(controller.handleKey("g"));
  // The first "g" expired, so this second "g" starts a fresh pending sequence rather than
  // completing "gg" — the cursor must not have jumped to row 0.
  EXPECT_EQ(controller.cursorRow(), 2);
  EXPECT_TRUE(controller.handleKey("g"));
  EXPECT_EQ(controller.cursorRow(), 0);
}

TEST(DirectoryController, HNavigatesToParentDirectory) {
  QTemporaryDir parent(fixturePattern("ctrl-parent"));
  ASSERT_TRUE(parent.isValid());
  ASSERT_TRUE(QDir(parent.path()).mkdir("child"));
  const auto childPath = QDir(parent.path()).filePath("child");
  DirectoryController controller;
  controller.open(childPath);
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), childPath);
  EXPECT_TRUE(controller.handleKey("h"));
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(QDir(controller.currentPath()).canonicalPath(), QDir(parent.path()).canonicalPath());
}

TEST(DirectoryController, CursorRowStaysClampedToZeroOnAnEmptyDirectory) {
  QTemporaryDir dir(fixturePattern("ctrl-empty"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.handleKey("G"));
  EXPECT_EQ(controller.cursorRow(), 0);
}

TEST(DirectoryController, WatcherObservesCreateRenameDeleteWithoutExplicitRefresh) {
  QTemporaryDir dir(fixturePattern("watcher"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  auto names = [&] {
    QStringList result;
    for (int row = 0; row < controller.listing()->rowCount(); ++row) {
      result.append(
          controller.listing()->data(controller.listing()->index(row, 0), DirectoryModel::NameRole).toString());
    }
    return result;
  };
  ASSERT_FALSE(writeFile(dir, "created").isEmpty());
  ASSERT_TRUE(QTest::qWaitFor([&] { return names() == QStringList{"created"}; }));
  ASSERT_TRUE(QFile::rename(dir.filePath("created"), dir.filePath("renamed")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return names() == QStringList{"renamed"}; }));
  ASSERT_TRUE(QFile::remove(dir.filePath("renamed")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return names().isEmpty(); }));
}

TEST(DirectoryController, SpaceTogglesQuickLookWhenACursorIsOnAValidRow) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_FALSE(controller.quickLookOpen());
  EXPECT_TRUE(controller.handleKey(" "));
  EXPECT_TRUE(controller.quickLookOpen());
  EXPECT_TRUE(controller.handleKey(" "));
  EXPECT_FALSE(controller.quickLookOpen());
}

TEST(DirectoryController, SpaceOnAnEmptyDirectoryIsConsumedAsANoOp) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-empty"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey(" "));  // REQ-F-013: consumed, but explicitly a no-op
  EXPECT_FALSE(controller.quickLookOpen());
}

TEST(DirectoryController, EscapeClosesQuickLookAndReturnsFalseWhenAlreadyClosed) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-escape"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  // Closed already: falls through so the window-level fullscreen Shortcut can handle Escape.
  EXPECT_FALSE(controller.handleKey("Escape"));
  ASSERT_TRUE(controller.handleKey(" "));
  ASSERT_TRUE(controller.quickLookOpen());
  EXPECT_TRUE(controller.handleKey("Escape"));
  EXPECT_FALSE(controller.quickLookOpen());
}

TEST(DirectoryController, JAndKKeepUpdatingThePreviewWhileQuickLookStaysOpen) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-live"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  writeFile(dir, "b.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey(" "));
  ASSERT_TRUE(controller.quickLookOpen());
  const auto firstName = controller.preview()->name();
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_TRUE(controller.quickLookOpen());  // still open — j/k never close it
  EXPECT_NE(controller.preview()->name(), firstName);
}

TEST(PlacesModel, FixedStandardLocations) {
  const PlacesModel places;
  ASSERT_EQ(places.rowCount(), 4);
  const QList<QStandardPaths::StandardLocation> locations{
      QStandardPaths::HomeLocation, QStandardPaths::DocumentsLocation, QStandardPaths::DownloadLocation,
      QStandardPaths::PicturesLocation};
  const QStringList names{"Home", "Documents", "Downloads", "Pictures"};
  for (int row = 0; row < places.rowCount(); ++row) {
    EXPECT_EQ(places.data(places.index(row), PlacesModel::NameRole).toString(), names[row]);
    EXPECT_EQ(places.data(places.index(row), PlacesModel::PathRole).toString(),
              QDir::cleanPath(QStandardPaths::writableLocation(locations[row])));
  }
}
