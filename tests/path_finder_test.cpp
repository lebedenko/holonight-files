#include "directory_controller.h"
#include "engine_setup.h"
#include "path_finder_model.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>
#include <iostream>

namespace {
bool touch(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly);
}
bool settled(const PathFinderModel& model) {
  return QTest::qWaitFor([&] { return !model.scanning(); }, 5000);
}
bool hasResults(const PathFinderModel& model) {
  return QTest::qWaitFor([&] { return model.rowCount() > 0; }, 5000);
}
}  // namespace

TEST(PathFinderModel, UnorderedTermsFindPathsAcrossFields) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  ASSERT_TRUE(QDir(directory.path()).mkpath("src/lambda"));
  ASSERT_TRUE(QDir(directory.path()).mkpath("src/audit"));
  ASSERT_TRUE(touch(directory.path() + "/src/lambda/functions.cpp"));
  ASSERT_TRUE(touch(directory.path() + "/src/lambda/audit.cpp"));
  ASSERT_TRUE(touch(directory.path() + "/src/audit/lambda.cpp"));
  PathFinderModel model;
  model.start(directory.path(), false);
  ASSERT_TRUE(settled(model));
  QSignalSpy resets(&model, &QAbstractItemModel::modelReset);
  QSet<QString> forward;
  for (const QString& query : {
           QStringLiteral("lambda"),
           QStringLiteral("lambda func"),
           QStringLiteral("lambda audit"),
           QStringLiteral("audit lambda"),
       }) {
    resets.clear();
    model.setQuery(query);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !resets.isEmpty(); }, 5000)) << query.toStdString();
    int expected = 2;
    if (query == "lambda") {
      expected = 3;
    } else if (query == "lambda func") {
      expected = 1;
    }
    ASSERT_EQ(model.rowCount(), expected) << query.toStdString();
    if (query == "lambda func") {
      EXPECT_EQ(model.pathAt(0), directory.path() + "/src/lambda/functions.cpp");
    } else if (query == "lambda audit") {
      for (int row = 0; row < model.rowCount(); ++row) {
        forward.insert(model.pathAt(row));
      }
    } else if (query == "audit lambda") {
      QSet<QString> reverse;
      for (int row = 0; row < model.rowCount(); ++row) {
        reverse.insert(model.pathAt(row));
      }
      EXPECT_EQ(reverse, forward);
    }
  }
  const auto positions = model.data(model.index(0), PathFinderModel::PositionsRole).value<QList<int>>();
  EXPECT_FALSE(positions.isEmpty());
}

TEST(PathFinderModel, SmartCaseDistinguishesUppercaseQuery) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  ASSERT_TRUE(touch(directory.path() + "/Lambda.cpp"));
  ASSERT_TRUE(touch(directory.path() + "/lambda.cpp"));
  PathFinderModel model;
  model.start(directory.path(), false);
  model.setQuery("lambda");
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(QTest::qWaitFor([&] { return model.rowCount() == 2; }, 5000));
  model.setQuery("Lambda");
  ASSERT_TRUE(QTest::qWaitFor([&] { return model.rowCount() == 1; }, 5000));
  EXPECT_EQ(model.pathAt(0), directory.path() + "/Lambda.cpp");
}

TEST(PathFinderModel, RootSwitchDiscardsOldResultsAndSkipsHiddenAndSymlinkedDirectories) {
  QTemporaryDir first;
  QTemporaryDir second;
  ASSERT_TRUE(first.isValid() && second.isValid());
  ASSERT_TRUE(QDir(first.path()).mkpath("visible/deep"));
  ASSERT_TRUE(touch(first.path() + "/visible/deep/needle.txt"));
  ASSERT_TRUE(touch(first.path() + "/.secret"));
  ASSERT_TRUE(QFile::link(first.path() + "/visible", first.path() + "/linked"));
  ASSERT_TRUE(QFile::link(first.path() + "/visible/deep/needle.txt", first.path() + "/linked-file.txt"));
  ASSERT_TRUE(touch(second.path() + "/other.txt"));
  PathFinderModel model;
  model.start(first.path(), false);
  model.setQuery("needle");
  ASSERT_TRUE(hasResults(model));
  EXPECT_EQ(model.rowCount(), 1);
  model.setQuery("linked-file");
  ASSERT_TRUE(QTest::qWaitFor([&] { return model.pathAt(0) == first.path() + "/linked-file.txt"; }, 5000));
  EXPECT_EQ(model.pathAt(0), first.path() + "/linked-file.txt");
  model.setRoot(second.path());
  model.setQuery("other");
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(hasResults(model));
  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(model.pathAt(0), second.path() + "/other.txt");
  model.setQuery("secret");
  ASSERT_TRUE(QTest::qWaitFor([&] { return model.rowCount() == 0; }, 5000));
}

TEST(PathFinderModel, HiddenPathsRequireExplicitOptIn) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  ASSERT_TRUE(QDir(directory.path()).mkdir(".private"));
  ASSERT_TRUE(touch(directory.path() + "/.private/audit.txt"));
  PathFinderModel model;
  model.start(directory.path(), false);
  model.setQuery("audit");
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 0);
  model.setIncludeHidden(true);
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(QTest::qWaitFor([&] { return model.rowCount() == 1; }, 5000));
  EXPECT_EQ(model.pathAt(0), directory.path() + "/.private/audit.txt");
  model.setIncludeHidden(false);
  EXPECT_FALSE(model.scanning());
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(QTest::qWaitFor([&] { return model.rowCount() == 0; }, 5000));
}

TEST(PathFinderModel, PublishesPartialResultsBeforeScanCompletes) {
  QSemaphore continueScan;
  PathScanFunction scanner = [&continueScan](const QString& root, bool, const SearchExclusionPolicy&,
                                             const std::atomic_bool&, const PathBatchReady& batchReady) {
    batchReady({{.path = root + "/lambda.cpp", .relativePath = "lambda.cpp", .directory = false}});
    continueScan.acquire();
    return true;
  };
  PathFinderModel model(scanner);
  model.start(QStringLiteral("/synthetic"), false);
  model.setQuery(QStringLiteral("lambda"));
  const bool partial = hasResults(model);
  const bool stillScanning = model.scanning();
  continueScan.release();
  EXPECT_TRUE(partial);
  EXPECT_TRUE(stillScanning);
  ASSERT_TRUE(settled(model));
}

TEST(PathFinderModel, FileAcceptanceOpensParentAndRestoresSelection) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  ASSERT_TRUE(touch(directory.path() + "/chosen.txt"));
  DirectoryController controller;
  controller.finder()->start(directory.path(), false);
  controller.finder()->setQuery("chosen");
  ASSERT_TRUE(hasResults(*controller.finder()));
  ASSERT_TRUE(controller.acceptFinderResult(0));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }, 5000));
  EXPECT_EQ(controller.currentPath(), directory.path());
  EXPECT_EQ(controller.cursorPath(), directory.path() + "/chosen.txt");
  controller.finder()->start(directory.path(), false);
  controller.finder()->setQuery("chosen");
  ASSERT_TRUE(hasResults(*controller.finder()));
  ASSERT_TRUE(QFile::remove(directory.path() + "/chosen.txt"));
  EXPECT_FALSE(controller.acceptFinderResult(0));
  EXPECT_FALSE(controller.finder()->error().isEmpty());
  EXPECT_TRUE(QTest::qWaitFor([&] { return controller.finder()->rowCount() == 0; }, 5000));
}

TEST(PathFinderModel, ReopeningAndSwitchingModesReuseCompletedScan) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  ASSERT_TRUE(QDir(directory.path()).mkdir("project"));
  ASSERT_TRUE(touch(directory.path() + "/project.txt"));
  std::atomic_int scanCount = 0;
  PathScanFunction scanner = [&scanCount](const QString& root, bool, const SearchExclusionPolicy&,
                                          const std::atomic_bool&, const PathBatchReady& batchReady) {
    ++scanCount;
    batchReady({
        {.path = root + "/project", .relativePath = "project", .directory = true},
        {.path = root + "/project.txt", .relativePath = "project.txt", .directory = false},
    });
    return true;
  };
  PathFinderModel model(scanner);
  model.start(directory.path(), false);
  model.setQuery("project");
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(hasResults(model));
  EXPECT_FALSE(model.isDirectoryAt(0));
  EXPECT_EQ(model.indexedCount(), 2);
  model.stop();
  model.start(directory.path(), true);
  EXPECT_FALSE(model.scanning());
  EXPECT_EQ(model.indexedCount(), 2);
  model.setQuery("project");
  ASSERT_TRUE(hasResults(model));
  EXPECT_TRUE(model.isDirectoryAt(0));
  model.stop();
  model.start(directory.path(), false);
  model.setQuery("project");
  ASSERT_TRUE(hasResults(model));
  EXPECT_FALSE(model.scanning());
  EXPECT_EQ(scanCount.load(), 1);
}

TEST(PathFinderModel, InterruptedScanRestartsOnReopen) {
  QSemaphore continueScan;
  std::atomic_int scanCount = 0;
  PathScanFunction scanner = [&continueScan, &scanCount](const QString& root, bool, const SearchExclusionPolicy&,
                                                         const std::atomic_bool&, const PathBatchReady& batchReady) {
    const int call = ++scanCount;
    batchReady({{.path = root + "/lambda.cpp", .relativePath = "lambda.cpp", .directory = false}});
    if (call == 1) {
      continueScan.acquire();
    }
    return true;
  };
  PathFinderModel model(scanner);
  model.start(QStringLiteral("/synthetic"), false);
  model.setQuery(QStringLiteral("lambda"));
  const bool partial = hasResults(model);
  const bool stillScanning = model.scanning();
  model.stop();
  continueScan.release();
  EXPECT_TRUE(partial);
  EXPECT_TRUE(stillScanning);
  model.start(QStringLiteral("/synthetic"), false);
  EXPECT_TRUE(model.scanning());
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(scanCount.load(), 2);
}

TEST(PathFinderModel, MissingRootReportsErrorAndObsoleteQueryDoesNotReturn) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  ASSERT_TRUE(touch(directory.path() + "/alpha.txt"));
  ASSERT_TRUE(touch(directory.path() + "/beta.txt"));
  PathFinderModel model;
  model.start(directory.path(), false);
  model.setQuery("alpha");
  model.setQuery("beta");
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(hasResults(model));
  EXPECT_EQ(model.pathAt(0), directory.path() + "/beta.txt");
  model.setRoot(directory.path() + "/missing");
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(model.error().isEmpty());
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(PathFinderModel, DirectoryAcceptanceOpensTheDirectory) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  ASSERT_TRUE(QDir(directory.path()).mkdir("destination"));
  DirectoryController controller;
  controller.finder()->start(directory.path(), true);
  controller.finder()->setQuery("destination");
  ASSERT_TRUE(hasResults(*controller.finder()));
  ASSERT_TRUE(controller.acceptFinderResult(0));
  EXPECT_EQ(controller.currentPath(), directory.path() + "/destination");
}

TEST(PathFinderModel, LargeTreeColdScanAndWarmQuery) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  for (int folder = 0; folder < 25; ++folder) {
    const QString name = QStringLiteral("folder-%1").arg(folder);
    ASSERT_TRUE(QDir(directory.path()).mkdir(name));
    for (int file = 0; file < 100; ++file) {
      ASSERT_TRUE(touch(directory.path() + u'/' + name + QStringLiteral("/document-%1.txt").arg(file)));
    }
  }
  PathFinderModel model;
  QElapsedTimer timer;
  timer.start();
  model.start(directory.path(), false);
  model.setQuery("document-99");
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(hasResults(model));
  const auto coldMilliseconds = timer.elapsed();
  model.stop();
  timer.restart();
  model.start(directory.path(), false);
  model.setQuery("document-99");
  ASSERT_TRUE(hasResults(model));
  const auto warmMilliseconds = timer.elapsed();
  std::cout << "finder 2500-file cold scan: " << coldMilliseconds << " ms, warm cached query: " << warmMilliseconds
            << " ms\n";
  EXPECT_EQ(model.rowCount(), 25);
}

TEST(PathFinderModel, FinderPopupLoadsInApplicationQml) {
  DirectoryController controller;
  QQmlApplicationEngine engine;
  initializeFilesEngine(engine);
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  EXPECT_NE(engine.rootObjects().first()->findChild<QObject*>("pathFinderPopup"), nullptr);
}
