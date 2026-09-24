#include "directory_model.h"

#include "directory_fixtures.h"
#include "directory_model_test_access.h"
#include "settings_fixtures.h"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <gtest/gtest.h>

using files_test::buildPermissionFixture;
using files_test::fixturePattern;
using files_test::populateEntries;
using files_test::restorePermissionFixture;
using files_test::runningAsRoot;
using files_test::writeFile;

namespace {
bool settled(const DirectoryModel& model) {
  return QTest::qWaitFor([&] { return !model.scanning(); });
}
}  // namespace

TEST(DirectoryModel, EmptyDirectoryLoadsParentRowAndNoError) {
  QTemporaryDir dir(fixturePattern("empty"));
  ASSERT_TRUE(dir.isValid());
  DirectoryModel model;
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_EQ(model.data(model.index(0), DirectoryModel::NameRole).toString(), "..");
  EXPECT_TRUE(model.data(model.index(0), DirectoryModel::IsParentRole).toBool());
  EXPECT_FALSE(model.data(model.index(0), DirectoryModel::IsHiddenRole).toBool());
  EXPECT_TRUE(model.directoryError().isEmpty());
}

TEST(DirectoryModel, FsRootLoadsWithoutParentRow) {
  DirectoryModel model;
  model.load(QStringLiteral("/"));
  ASSERT_TRUE(settled(model));
  EXPECT_GT(model.rowCount(), 0);
  for (int row = 0; row < model.rowCount(); ++row) {
    EXPECT_NE(model.data(model.index(row), DirectoryModel::NameRole).toString(), "..");
    EXPECT_FALSE(model.data(model.index(row), DirectoryModel::IsParentRole).toBool());
  }
}

TEST(DirectoryModel, LoadIsAsyncAndDoesNotBlockCallingThread) {
  QTemporaryDir dir(fixturePattern("async"));
  ASSERT_TRUE(dir.isValid());
  populateEntries(dir, 500);
  DirectoryModel model;
  model.load(dir.path());
  // scanning() flips synchronously inside load(); the walk itself happens on the worker thread,
  // so rowCount() being 1 (the synthetic parent entry) right after the call is what proves the caller was never
  // blocked.
  EXPECT_TRUE(model.scanning());
  EXPECT_EQ(model.rowCount(), 1);
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 501);
}

TEST(DirectoryModel, LargeDirectoryInsertsIncrementally) {
  QTemporaryDir dir(fixturePattern("large"));
  ASSERT_TRUE(dir.isValid());
  populateEntries(dir, 10000);
  DirectoryModel model;
  QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
  QElapsedTimer elapsed;
  elapsed.start();
  model.load(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return inserted.count() > 0; }, 5000));
  // Render timing belongs to the opt-in production-window benchmark.
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 10001);
  // Batching (kBatchEntryThreshold=250 / kBatchTimeThresholdMs=25) means 10,000 entries cannot
  // land in a single insert — proves incremental insertion rather than one big flush at the end.
  EXPECT_GT(inserted.count(), 1);
}

TEST(DirectoryModel, PerEntryStatFailuresAreIncludedNotDropped) {
  QTemporaryDir dir(fixturePattern("stat-fail"));
  ASSERT_TRUE(dir.isValid());
  const auto fixture = buildPermissionFixture(dir);
  const auto restore = qScopeGuard([&] { restorePermissionFixture(fixture); });
  writeFile(dir, "ordinary.txt");
  DirectoryModel model;
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  EXPECT_TRUE(model.directoryError().isEmpty());
  int danglingRow = -1;
  int brokenPermRow = -1;
  for (int row = 0; row < model.rowCount(); ++row) {
    const auto name = model.data(model.index(row), DirectoryModel::NameRole).toString();
    if (name == "dangling-link") {
      danglingRow = row;
    } else if (name == "broken-perm-link") {
      brokenPermRow = row;
    }
  }
  ASSERT_GE(danglingRow, 0);
  EXPECT_TRUE(model.data(model.index(danglingRow), DirectoryModel::StatFailedRole).toBool());
  EXPECT_EQ(model.data(model.index(danglingRow), DirectoryModel::StatErrorRole).toString(), "Broken symbolic link");
  if (!runningAsRoot()) {
    ASSERT_GE(brokenPermRow, 0);
    EXPECT_TRUE(model.data(model.index(brokenPermRow), DirectoryModel::StatFailedRole).toBool());
    EXPECT_FALSE(model.data(model.index(brokenPermRow), DirectoryModel::StatErrorRole).toString().isEmpty());
    EXPECT_NE(model.data(model.index(brokenPermRow), DirectoryModel::StatErrorRole).toString(), "Broken symbolic link");
  }
}

TEST(DirectoryModel, UnreadableDirectoryItselfReportsInlineError) {
  if (runningAsRoot()) {
    GTEST_SKIP() << "root bypasses the chmod 000 permission bit this fixture relies on";
  }
  QTemporaryDir dir(fixturePattern("blocked-dir"));
  ASSERT_TRUE(dir.isValid());
  const auto fixture = buildPermissionFixture(dir);
  const auto restore = qScopeGuard([&] { restorePermissionFixture(fixture); });
  DirectoryModel model;
  model.load(fixture.blocked_dir);
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(model.directoryError().isEmpty());
  EXPECT_EQ(model.rowCount(), 0);
}

TEST(DirectoryModel, UnicodeNamedEntriesLoadCorrectly) {
  QTemporaryDir dir(fixturePattern("unicode"));
  ASSERT_TRUE(dir.isValid());
  files_test::populateUnicodeEntries(dir);
  DirectoryModel model;
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 6);
  QStringList names;
  for (int row = 0; row < model.rowCount(); ++row) {
    names.append(model.data(model.index(row), DirectoryModel::NameRole).toString());
  }
  EXPECT_TRUE(names.contains(QStringLiteral("café.txt")));
  EXPECT_TRUE(names.contains(QString::fromUtf8("файл.txt")));
  EXPECT_TRUE(names.contains(QString::fromUtf8("ファイル.txt")));
  EXPECT_TRUE(names.contains(QString::fromUtf8("📁emoji-folder.txt")));
}

TEST(DirectoryModel, RefreshDiffsInsteadOfResettingAndPicksUpFilesystemChanges) {
  QTemporaryDir dir(fixturePattern("refresh"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  writeFile(dir, "b.txt");
  DirectoryModel model;
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  ASSERT_EQ(model.rowCount(), 3);
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
  QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
  ASSERT_TRUE(QFile::remove(dir.filePath("a.txt")));
  ASSERT_FALSE(writeFile(dir, "c.txt").isEmpty());
  model.refresh();
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 3);
  EXPECT_EQ(reset.count(), 0);  // refresh() diffs; it must not trigger a full model reset
  EXPECT_GT(inserted.count(), 0);
  EXPECT_GT(removed.count(), 0);
  QStringList names;
  for (int row = 0; row < model.rowCount(); ++row) {
    names.append(model.data(model.index(row), DirectoryModel::NameRole).toString());
  }
  EXPECT_TRUE(names.contains(QStringLiteral("b.txt")));
  EXPECT_TRUE(names.contains(QStringLiteral("c.txt")));
  EXPECT_FALSE(names.contains(QStringLiteral("a.txt")));
}

TEST(DirectoryModel, StaleGenerationIsDiscardedWhenLoadIsCalledAgainBeforeSettling) {
  QTemporaryDir first(fixturePattern("gen-a"));
  QTemporaryDir second(fixturePattern("gen-b"));
  ASSERT_TRUE(first.isValid());
  ASSERT_TRUE(second.isValid());
  writeFile(first, "from-a.txt");
  writeFile(second, "from-b.txt");
  DirectoryModel model;
  model.load(first.path());
  model.load(second.path());  // supersedes the first walk before it can finish
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.directoryPath(), second.path());
  ASSERT_EQ(model.rowCount(), 2);
  EXPECT_EQ(model.data(model.index(1), DirectoryModel::NameRole).toString(), "from-b.txt");
}

TEST(DirectoryModel, ShutdownEmitsShutdownFinished) {
  QTemporaryDir dir(fixturePattern("shutdown"));
  ASSERT_TRUE(dir.isValid());
  DirectoryModel model;
  model.load(dir.path());
  QSignalSpy done(&model, &DirectoryModel::shutdownFinished);
  model.shutdown();
  ASSERT_TRUE(QTest::qWaitFor([&] { return done.count() == 1; }));
}

TEST(DirectoryModel, DirectoryRemovedImmediatelyBeforeOpenReportsError) {
  QTemporaryDir dir(fixturePattern("open-race"));
  ASSERT_TRUE(dir.isValid());
  const QString child = dir.filePath("child");
  ASSERT_TRUE(QDir().mkdir(child));
  DirectoryModel model;
  DirectoryModelTestAccess::beforeOpen(model, [child] { QDir().rmdir(child); });
  model.load(child);
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.directoryPath(), child);
  EXPECT_FALSE(model.directoryError().isEmpty());
}

TEST(DirectoryModel, PermissionsRevokedImmediatelyBeforeOpenReportError) {
  if (runningAsRoot()) {
    GTEST_SKIP() << "root bypasses directory permission checks";
  }
  QTemporaryDir dir(fixturePattern("permission-race"));
  ASSERT_TRUE(dir.isValid());
  const auto restore = qScopeGuard([&] { ::chmod(QFile::encodeName(dir.path()).constData(), 0700); });
  DirectoryModel model;
  DirectoryModelTestAccess::beforeOpen(model, [path = dir.path()] { ::chmod(QFile::encodeName(path).constData(), 0); });
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.directoryPath(), dir.path());
  EXPECT_FALSE(model.directoryError().isEmpty());
}

TEST(DirectoryModel, ReadFailurePreservesPartialResultsAndUnseenRefreshRows) {
  QTemporaryDir dir(fixturePattern("read-error"));
  ASSERT_TRUE(dir.isValid());
  populateEntries(dir, 600);
  DirectoryModel model;
  DirectoryModelTestAccess::failReadAfter(model, 275);
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 276);
  EXPECT_FALSE(model.directoryError().isEmpty());
  DirectoryModelTestAccess::failReadAfter(model, -1);
  model.refresh();
  ASSERT_TRUE(settled(model));
  ASSERT_EQ(model.rowCount(), 601);
  QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
  DirectoryModelTestAccess::failReadAfter(model, 10);
  model.refresh();
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(model.directoryError().isEmpty());
  EXPECT_EQ(model.rowCount(), 601);
  EXPECT_TRUE(removed.isEmpty());
  DirectoryModelTestAccess::beforeOpen(model, [path = dir.path()] { QDir(path).removeRecursively(); });
  model.refresh();
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(model.directoryError().isEmpty());
  EXPECT_EQ(model.rowCount(), 601);
  EXPECT_TRUE(removed.isEmpty());
}

TEST(DirectoryModel, RefreshOnlySignalsChangedMetadataAndBatchesInsertionsAndRemovals) {
  QTemporaryDir dir(fixturePattern("diff-signals"));
  ASSERT_TRUE(dir.isValid());
  populateEntries(dir, 20);
  DirectoryModel model;
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
  QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
  model.refresh();
  ASSERT_TRUE(settled(model));
  EXPECT_TRUE(changed.isEmpty());
  EXPECT_TRUE(inserted.isEmpty());
  EXPECT_TRUE(removed.isEmpty());
  const auto changedName = model.data(model.index(1), DirectoryModel::NameRole).toString();
  ASSERT_FALSE(writeFile(dir, changedName, "longer content").isEmpty());
  populateEntries(dir, 30, "new");
  model.refresh();
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(changed.count(), 1);
  ASSERT_EQ(inserted.count(), 1);
  EXPECT_EQ(inserted.first().at(2).toInt() - inserted.first().at(1).toInt() + 1, 30);
  // Select a contiguous model range independently of filesystem enumeration order.
  for (int row = 4; row <= 12; ++row) {
    ASSERT_TRUE(QFile::remove(model.data(model.index(row), DirectoryModel::PathRole).toString()));
  }
  model.refresh();
  ASSERT_TRUE(settled(model));
  ASSERT_EQ(removed.count(), 1);
  EXPECT_EQ(removed.first().at(1).toInt(), 4);
  EXPECT_EQ(removed.first().at(2).toInt(), 12);
}

TEST(DirectoryModel, DestructionCancelsWorkerWaitingForBatchDelivery) {
  QTemporaryDir dir(fixturePattern("cancel-bounded"));
  ASSERT_TRUE(dir.isValid());
  populateEntries(dir, 1000);
  QElapsedTimer elapsed;
  elapsed.start();
  {
    DirectoryModel model;
    model.load(dir.path());
    // Do not pump GUI events: the worker fills the bounded delivery queue and waits.
    QTest::qSleep(100);
  }
  EXPECT_LT(elapsed.elapsed(), 1000);
}

TEST(DirectoryModel, MetadataFollowsValidSymlinkTargetsAndCanStatUnreadableFile) {
  QTemporaryDir dir(fixturePattern("target-metadata"));
  ASSERT_TRUE(dir.isValid());
  const auto target = writeFile(dir, "target", "target data");
  ASSERT_FALSE(target.isEmpty());
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  ASSERT_TRUE(QFile::link(target, dir.filePath("file-link")));
  ASSERT_TRUE(QFile::link(dir.filePath("folder"), dir.filePath("folder-link")));
  // Reading a file and statting its metadata have different permission requirements.
  ASSERT_EQ(::chmod(QFile::encodeName(target).constData(), 0), 0);
  const auto restore = qScopeGuard([&] { ::chmod(QFile::encodeName(target).constData(), 0600); });
  DirectoryModel model;
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  ASSERT_EQ(model.rowCount(), 5);
  for (int row = 0; row < model.rowCount(); ++row) {
    const auto index = model.index(row);
    if (model.data(index, DirectoryModel::IsParentRole).toBool()) {
      continue;
    }
    const auto name = model.data(index, DirectoryModel::NameRole).toString();
    const bool folder = name.startsWith("folder");
    EXPECT_FALSE(model.data(index, DirectoryModel::StatFailedRole).toBool());
    EXPECT_EQ(model.data(index, DirectoryModel::IsDirRole).toBool(), folder);
    EXPECT_EQ(model.data(index, DirectoryModel::SizeRole).toLongLong(), folder ? -1 : 11);
  }
}

TEST(DirectoryModel, SuspensionRejectsQueuedInitialAndRefreshDeliveriesThenReconciles) {
  QTemporaryDir dir(fixturePattern("suspended-walk"));
  populateEntries(dir, 600);
  DirectoryModel model;
  for (bool refresh : {false, true}) {
    std::atomic_bool entered = false;
    std::atomic_bool release = false;
    DirectoryModelTestAccess::beforeOpen(model, [&] {
      entered = true;
      while (!release.load()) {
        QThread::msleep(1);
      }
    });
    if (refresh) {
      model.refresh();
    } else {
      model.load(dir.path());
    }
    const auto unblock = qScopeGuard([&] { release = true; });
    ASSERT_TRUE(QTest::qWaitFor([&] { return entered.load(); }));
    model.suspendUpdates();
    const int before = model.rowCount();
    const int placeholder = model.insertPlaceholderRow();
    model.refresh();
    release = true;
    QTest::qWait(60);
    EXPECT_EQ(model.rowCount(), before + 1);
    model.removePlaceholderRow(placeholder);
    DirectoryModelTestAccess::beforeOpen(model, {});
    model.resumeUpdates();
    ASSERT_TRUE(settled(model));
    EXPECT_EQ(model.rowCount(), 601);
  }
}

TEST(DirectoryModel, ShutdownCompletesWhileSuspendedWithQueuedDeliveries) {
  QTemporaryDir dir(fixturePattern("suspended-shutdown"));
  populateEntries(dir, 1000);
  DirectoryModel model;
  QSignalSpy finished(&model, &DirectoryModel::shutdownFinished);
  model.load(dir.path());
  QThread::msleep(30);  // Fill bounded delivery slots without processing GUI events.
  model.suspendUpdates();
  model.shutdown();
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.count() == 1; }));
  EXPECT_EQ(model.rowCount(), 1);
  EXPECT_TRUE(model.data(model.index(0), DirectoryModel::IsParentRole).toBool());
}

TEST(DirectoryModel, IconNameRoleCarriesTheWorkerDerivedCandidateChain) {
  QTemporaryDir dir(fixturePattern("icon-names"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(writeFile(dir, "notes.txt").isEmpty());
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  ASSERT_TRUE(QFile::link(dir.filePath("folder"), dir.filePath("folder-link")));
  ASSERT_TRUE(QFile::link(dir.filePath("missing.jpg"), dir.filePath("dangling.jpg")));
  DirectoryModel model;
  EXPECT_EQ(model.roleNames().value(DirectoryModel::IconNameRole), QByteArray("iconName"));
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  ASSERT_EQ(model.rowCount(), 5);
  QHash<QString, QString> iconNames;
  for (int row = 0; row < model.rowCount(); ++row) {
    const auto index = model.index(row);
    iconNames.insert(model.data(index, DirectoryModel::NameRole).toString(),
                     model.data(index, DirectoryModel::IconNameRole).toString());
  }
  EXPECT_EQ(iconNames.value("notes.txt"), "text-plain/text-x-generic/application-x-generic");
  EXPECT_EQ(iconNames.value("folder"), "folder/inode-directory");
  EXPECT_EQ(iconNames.value("folder-link"), "folder/inode-directory");  // stat() follows the link
  EXPECT_EQ(iconNames.value("dangling.jpg"), "application-x-generic");  // not image-jpeg

  const int placeholder = model.insertPlaceholderRow();
  EXPECT_EQ(model.data(model.index(placeholder), DirectoryModel::IconNameRole).toString(), "application-x-generic");
}

TEST(DirectoryModel, RefreshEmitsDataChangedWhenAnEntrysIconChanges) {
  QTemporaryDir dir(fixturePattern("icon-refresh"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(writeFile(dir, "entry").isEmpty());
  DirectoryModel model;
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  ASSERT_EQ(model.rowCount(), 2);
  ASSERT_EQ(model.data(model.index(1), DirectoryModel::IconNameRole).toString(), "application-x-generic");
  ASSERT_TRUE(QFile::remove(dir.filePath("entry")));
  ASSERT_TRUE(QDir(dir.path()).mkdir("entry"));
  QSignalSpy changedRows(&model, &QAbstractItemModel::dataChanged);
  model.refresh();
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(changedRows.count(), 1);
  EXPECT_EQ(model.data(model.index(1), DirectoryModel::IconNameRole).toString(), "folder/inode-directory");
}

namespace {
using Classification = LocationClassifier::Classification;

std::optional<RestoreOutcome> validate(DirectoryModel& model, const QString& path) {
  QSignalSpy validated(&model, &DirectoryModel::restoreValidated);
  model.validateForRestore(path);
  if (!validated.wait(5000)) {
    return std::nullopt;
  }
  EXPECT_EQ(validated.first().at(0).toString(), path);
  return validated.first().at(1).value<RestoreOutcome>();
}
}  // namespace

// SPEC.md REQ-F-013/016
TEST(DirectoryModel, ValidateForRestoreReportsEachOutcomeFromTheWorkerThread) {
  QTemporaryDir dir(fixturePattern("restore-validate"));
  ASSERT_TRUE(dir.isValid());
  DirectoryModel model;
  const auto local = std::make_shared<files_test::FakeLocationClassifier>(Classification::Local);
  DirectoryModelTestAccess::setLocationClassifier(model, local);
  EXPECT_EQ(validate(model, dir.path()), RestoreOutcome::Ok);
  EXPECT_EQ(validate(model, dir.filePath("missing")), RestoreOutcome::DoesNotExist);
  EXPECT_EQ(validate(model, writeFile(dir, "file.txt")), RestoreOutcome::NotDirectory);
  ASSERT_EQ(local->threads().size(), 1U);
  EXPECT_NE(local->threads().front(), QThread::currentThread());
  for (const auto classification : {Classification::Network, Classification::Removable}) {
    DirectoryModelTestAccess::setLocationClassifier(
        model, std::make_shared<files_test::FakeLocationClassifier>(classification));
    EXPECT_EQ(validate(model, dir.path()), RestoreOutcome::NotLocal);
  }
  if (!runningAsRoot()) {
    ASSERT_TRUE(QDir(dir.path()).mkdir("blocked"));
    const auto blocked = dir.filePath("blocked");
    const auto restore = qScopeGuard([&] { ::chmod(QFile::encodeName(blocked).constData(), 0700); });
    ASSERT_EQ(::chmod(QFile::encodeName(blocked).constData(), 0), 0);
    EXPECT_EQ(validate(model, blocked), RestoreOutcome::NotReadable);
  }
}

// SPEC.md REQ-F-016/REQ-NF-002: a hung classification never stalls the GUI event loop.
TEST(DirectoryModel, SlowClassificationKeepsTheEventLoopResponsive) {
  QTemporaryDir dir(fixturePattern("restore-slow"));
  ASSERT_TRUE(dir.isValid());
  DirectoryModel model;
  DirectoryModelTestAccess::setLocationClassifier(
      model, std::make_shared<files_test::FakeLocationClassifier>(Classification::Local, std::chrono::seconds(2)));
  int ticks = 0;
  QTimer timer;
  timer.setInterval(50);
  QObject::connect(&timer, &QTimer::timeout, [&] { ++ticks; });
  timer.start();
  QElapsedTimer elapsed;
  elapsed.start();
  EXPECT_EQ(validate(model, dir.path()), RestoreOutcome::Ok);
  EXPECT_GE(elapsed.elapsed(), 2000);
  // ~40 ticks expected; a blocked GUI thread would deliver at most one.
  EXPECT_GE(ticks, 20);
}

// SPEC.md REQ-F-019: only a successful, current load() reports its classification.
TEST(DirectoryModel, LoadSucceededFiresOnlyForSuccessfulCurrentLoads) {
  QTemporaryDir dir(fixturePattern("load-succeeded"));
  ASSERT_TRUE(dir.isValid());
  populateEntries(dir, 5);
  QDir(dir.path()).mkdir("second");
  DirectoryModel model;
  const auto classifier = std::make_shared<files_test::FakeLocationClassifier>(Classification::Network);
  DirectoryModelTestAccess::setLocationClassifier(model, classifier);
  QSignalSpy succeeded(&model, &DirectoryModel::loadSucceeded);

  model.load(dir.path());
  ASSERT_TRUE(succeeded.wait(5000));
  ASSERT_EQ(succeeded.count(), 1);
  EXPECT_EQ(succeeded.first().at(0).toString(), dir.path());
  EXPECT_EQ(succeeded.first().at(1).value<Classification>(), Classification::Network);
  ASSERT_EQ(classifier->threads().size(), 1U);
  EXPECT_NE(classifier->threads().front(), QThread::currentThread());

  model.refresh();
  ASSERT_TRUE(settled(model));
  QTest::qWait(100);
  EXPECT_EQ(succeeded.count(), 1);

  model.load(dir.filePath("missing"));
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(model.directoryError().isEmpty());
  DirectoryModelTestAccess::failReadAfter(model, 2);
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(model.directoryError().isEmpty());
  DirectoryModelTestAccess::failReadAfter(model, -1);
  QTest::qWait(100);
  EXPECT_EQ(succeeded.count(), 1);

  std::atomic_bool entered = false;
  std::atomic_bool release = false;
  DirectoryModelTestAccess::beforeOpen(model, [&] {
    entered = true;
    while (!release.load()) {
      QThread::msleep(1);
    }
  });
  model.load(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return entered.load(); }));
  DirectoryModelTestAccess::beforeOpen(model, nullptr);
  model.load(dir.filePath("second"));
  release = true;
  ASSERT_TRUE(settled(model));
  ASSERT_TRUE(QTest::qWaitFor([&] { return succeeded.count() == 2; }));
  QTest::qWait(100);
  ASSERT_EQ(succeeded.count(), 2);
  EXPECT_EQ(succeeded.at(1).at(0).toString(), dir.filePath("second"));
}

// REQ-F-019: accepting the final batch makes this load eligible even when its classification
// finishes after a refresh or another navigation. Superseded, unaccepted loads remain excluded.
TEST(DirectoryModel, AcceptedLoadClassificationSurvivesRefreshAndNavigation) {
  for (bool navigate : {false, true}) {
    SCOPED_TRACE(navigate);
    QTemporaryDir first(fixturePattern("tracking-first"));
    QTemporaryDir second(fixturePattern("tracking-second"));
    ASSERT_TRUE(first.isValid() && second.isValid());
    DirectoryModel model;
    const auto classifier = std::make_shared<files_test::GatedLocationClassifier>();
    DirectoryModelTestAccess::setLocationClassifier(model, classifier);
    const auto release = qScopeGuard([&] { classifier->release.release(); });
    QSignalSpy succeeded(&model, &DirectoryModel::loadSucceeded);
    model.load(first.path());
    ASSERT_TRUE(QTest::qWaitFor([&] { return classifier->entered.load() && !model.scanning(); }));
    ASSERT_TRUE(model.directoryError().isEmpty());
    ASSERT_EQ(succeeded.count(), 0);
    if (navigate) {
      model.load(second.path());
    } else {
      model.refresh();
    }
    classifier->release.release();
    ASSERT_TRUE(QTest::qWaitFor([&] { return !model.scanning() && succeeded.count() == (navigate ? 2 : 1); }));
    EXPECT_EQ(succeeded.first().at(0).toString(), first.path());
    if (navigate) {
      EXPECT_EQ(succeeded.last().at(0).toString(), second.path());
    }
    EXPECT_EQ(classifier->calls.load(), navigate ? 2 : 1);
  }
}
