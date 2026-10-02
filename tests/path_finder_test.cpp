#include "directory_controller.h"
#include "directory_fixtures.h"
#include "engine_setup.h"
#include "path_finder_model.h"
#include "path_index_store.h"

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

TEST(PathFinderModel, PopupClosureKeepsScanAndReopeningAttaches) {
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
  EXPECT_EQ(scanCount.load(), 1);
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

TEST(PathFinderModel, DirtyRefreshKeepsCompleteSnapshotAndRecreatesMissingPaths) {
  QTemporaryDir dir;
  ASSERT_TRUE(touch(dir.filePath("needle.txt")));
  QSemaphore release;
  std::atomic_int scans = 0;
  std::atomic_bool entered = false;
  PathFinderModel model([&](const QString& root, bool hidden, const SearchExclusionPolicy& policy,
                            const std::atomic_bool& cancelled, const PathBatchReady& ready) {
    if (++scans == 2) {
      entered.store(true);
      release.acquire();
    }
    return scanPaths(root, hidden, policy, cancelled, ready);
  });
  model.start(dir.path(), false);
  model.setQuery("needle");
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(hasResults(model));
  model.invalidatePaths({dir.path()});
  const bool refreshing = QTest::qWaitFor([&] { return entered.load(); }, 5000);
  const auto visibleDuringRefresh = model.pathAt(0);
  model.reportMissing(dir.filePath("needle.txt"));
  release.release();
  EXPECT_TRUE(refreshing);
  EXPECT_EQ(visibleDuringRefresh, dir.filePath("needle.txt"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return scans.load() >= 3 && !model.scanning(); }, 5000));
  ASSERT_TRUE(hasResults(model));
  EXPECT_EQ(model.pathAt(0), dir.filePath("needle.txt"));
}

TEST(PathFinderModel, FreshnessUsesInjectedTimeAndIdleChangesWaitForReopen) {
  qint64 now = 1000000;
  std::atomic_int scans = 0;
  PathFinderModel model(
      [&](const QString&, bool, const SearchExclusionPolicy&, const std::atomic_bool&, const PathBatchReady&) {
        ++scans;
        return true;
      });
  model.setClock([&] { return now; });
  model.start("/synthetic", false);
  ASSERT_TRUE(settled(model));
  model.stop();
  now += 299999;
  model.start("/synthetic", false);
  EXPECT_FALSE(model.scanning());
  EXPECT_EQ(scans.load(), 1);
  model.stop();
  ++now;
  model.start("/synthetic", false);
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(scans.load(), 2);
  model.stop();
  model.invalidatePaths({"/synthetic/child"});
  EXPECT_EQ(scans.load(), 2);
  model.start("/synthetic", false);
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(scans.load(), 3);
  model.stop();
  model.invalidatePaths({"/synthetic-other"});
  model.start("/synthetic", false);
  EXPECT_FALSE(model.scanning());
  EXPECT_EQ(scans.load(), 3);
}

TEST(PathFinderModel, PeriodicRefreshRetriesFailuresAtIntervalAndStopsWhenClosed) {
  std::atomic_int scans = 0;
  PathFinderModel model(
      [&](const QString&, bool, const SearchExclusionPolicy&, const std::atomic_bool&, const PathBatchReady&) {
        ++scans;
        return false;
      });
  model.setRefreshInterval(200);
  model.start("/unavailable", false);
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(scans.load(), 1);
  EXPECT_FALSE(model.error().isEmpty());
  ASSERT_TRUE(QTest::qWaitFor([&] { return scans.load() >= 2; }, 5000));
  model.stop();
  const int closedCount = scans.load();
  // Process enough event turns to cross the selected test interval.
  QElapsedTimer timer;
  timer.start();
  ASSERT_TRUE(QTest::qWaitFor([&] { return timer.elapsed() >= 250; }, 1000));
  EXPECT_EQ(scans.load(), closedCount);
}

TEST(PathFinderModel, InvalidationDebouncesAndRejectsCancelledGeneration) {
  QSemaphore release;
  std::atomic_int scans = 0;
  std::atomic_bool entered = false;
  PathFinderModel model([&](const QString& root, bool, const SearchExclusionPolicy&, const std::atomic_bool&,
                            const PathBatchReady& ready) {
    const auto call = ++scans;
    if (call == 1) {
      entered.store(true);
      release.acquire();
    }
    const QString name = call == 1 ? "obsolete.txt" : "replacement.txt";
    ready({{.path = root + u'/' + name, .relativePath = name, .directory = false}});
    return true;
  });
  model.start("/synthetic", false);
  const bool started = QTest::qWaitFor([&] { return entered.load(); }, 5000);
  model.invalidatePaths({"/synthetic/a"});
  model.invalidatePaths({"/synthetic/b"});
  model.invalidatePaths({"/synthetic/c"});
  release.release();
  EXPECT_TRUE(started);
  ASSERT_TRUE(settled(model));
  model.setQuery("obsolete");
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reset.isEmpty(); }, 5000));
  EXPECT_EQ(model.rowCount(), 0);
  model.setQuery("replacement");
  ASSERT_TRUE(hasResults(model));
  EXPECT_EQ(scans.load(), 2);
}

TEST(PathFinderModel, ShutdownIsAsynchronousAndDiscardsLateScanResults) {
  QSemaphore release;
  std::atomic_bool entered = false;
  PathFinderModel model([&](const QString& root, bool, const SearchExclusionPolicy&, const std::atomic_bool&,
                            const PathBatchReady& ready) {
    entered.store(true);
    release.acquire();
    ready({{.path = root + "/obsolete.txt", .relativePath = "obsolete.txt", .directory = false}});
    return true;
  });
  model.start("/synthetic", false);
  const bool started = QTest::qWaitFor([&] { return entered.load(); }, 5000);
  QSignalSpy finished(&model, &PathFinderModel::shutdownFinished);
  model.shutdown();
  EXPECT_TRUE(finished.isEmpty());
  release.release();
  EXPECT_TRUE(started);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !finished.isEmpty(); }, 5000));
  EXPECT_EQ(model.rowCount(), 0);
}

namespace {
struct FinderCacheEnvironment {
  QTemporaryDir directory;
  files_test::ScopedEnvironmentVariable cache{"XDG_CACHE_HOME", directory.path()};
};
}  // namespace

TEST(PathFinderModel, HomeWarmupAttachesAndPersistsAcrossModelRestart) {
  FinderCacheEnvironment environment;
  QSemaphore release;
  std::atomic_int scans = 0;
  std::atomic_bool entered = false;
  {
    PathFinderModel model([&](const QString& root, bool, const SearchExclusionPolicy&, const std::atomic_bool&,
                              const PathBatchReady& ready) {
      ++scans;
      entered.store(true);
      ready({
          {.path = root + "/project.txt", .relativePath = "project.txt", .directory = false},
          {.path = root + "/project", .relativePath = "project", .directory = true},
      });
      release.acquire();
      return true;
    });
    model.warmUp();
    model.warmUp();
    const bool started = QTest::qWaitFor([&] { return entered.load(); }, 5000);
    model.start(PathFinderModel::homePath(), false);
    model.setQuery("project");
    const bool progressive = hasResults(model);
    model.stop();
    release.release();
    model.start(PathFinderModel::homePath(), false);
    EXPECT_TRUE(started);
    EXPECT_TRUE(progressive);
    ASSERT_TRUE(settled(model));
    EXPECT_EQ(scans.load(), 1);
  }
  EXPECT_TRUE(QFile::exists(PathIndexStore::cachePath()));
  entered.store(false);
  PathFinderModel restored([&](const QString& root, bool, const SearchExclusionPolicy&, const std::atomic_bool&,
                               const PathBatchReady& ready) {
    ++scans;
    entered.store(true);
    release.acquire();
    ready({{.path = root + "/new.txt", .relativePath = "new.txt", .directory = false}});
    return true;
  });
  restored.warmUp();
  restored.start(PathFinderModel::homePath(), true);
  restored.setQuery("project");
  const bool reused = hasResults(restored);
  const bool directory = restored.isDirectoryAt(0);
  const bool refreshing = restored.scanning();
  release.release();
  EXPECT_TRUE(reused);
  EXPECT_TRUE(directory);
  EXPECT_TRUE(refreshing);
  ASSERT_TRUE(settled(restored));
  EXPECT_EQ(scans.load(), 2);
  restored.start(PathFinderModel::homePath(), false);
  restored.setQuery("new");
  ASSERT_TRUE(hasResults(restored));
  EXPECT_EQ(restored.pathAt(0), PathFinderModel::homePath() + "/new.txt");
}

TEST(PathFinderModel, ForegroundRootPreemptsHomeAndWarmupResumes) {
  FinderCacheEnvironment environment;
  QSemaphore release;
  std::atomic_int homeScans = 0;
  std::atomic_bool entered = false;
  std::atomic_bool homeFinished = false;
  PathFinderModel model([&](const QString& root, bool, const SearchExclusionPolicy&, const std::atomic_bool& cancelled,
                            const PathBatchReady& ready) {
    if (root == PathFinderModel::homePath()) {
      if (++homeScans == 1) {
        entered.store(true);
        release.acquire();
      } else {
        homeFinished.store(true);
      }
    }
    // Deliberately emit even after cancellation to exercise the generation guard.
    ready({{.path = root + "/needle.txt", .relativePath = "needle.txt", .directory = false}});
    return !cancelled.load();
  });
  model.warmUp();
  const bool started = QTest::qWaitFor([&] { return entered.load(); }, 5000);
  model.start("/foreground", false);
  model.setQuery("needle");
  release.release();
  EXPECT_TRUE(started);
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(hasResults(model));
  EXPECT_EQ(model.pathAt(0), "/foreground/needle.txt");
  ASSERT_TRUE(QTest::qWaitFor([&] { return homeFinished.load(); }, 5000));
  EXPECT_EQ(homeScans.load(), 2);
}

TEST(PathFinderModel, UnavailableCachedRootClearsResultsOnReopen) {
  QTemporaryDir parent;
  ASSERT_TRUE(QDir(parent.path()).mkdir("root"));
  const auto root = parent.filePath("root");
  ASSERT_TRUE(touch(root + "/needle.txt"));
  PathFinderModel model;
  model.start(root, false);
  model.setQuery("needle");
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(hasResults(model));
  model.stop();
  ASSERT_TRUE(QDir(parent.path()).rename("root", "removed"));
  model.start(root, false);
  model.setQuery("needle");
  ASSERT_TRUE(QTest::qWaitFor([&] { return !model.error().isEmpty(); }, 5000));
  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_EQ(model.indexedCount(), 0);
}
