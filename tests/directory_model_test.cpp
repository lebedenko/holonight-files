#include "directory_model.h"

#include "directory_fixtures.h"
#include "directory_model_test_access.h"

#include <QAbstractItemModelTester>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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

TEST(DirectoryModel, EmptyDirectoryLoadsWithNoRowsAndNoError) {
  QTemporaryDir dir(fixturePattern("empty"));
  ASSERT_TRUE(dir.isValid());
  DirectoryModel model;
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  model.load(dir.path());
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 0);
  EXPECT_TRUE(model.directoryError().isEmpty());
}

TEST(DirectoryModel, LoadIsAsyncAndDoesNotBlockCallingThread) {
  QTemporaryDir dir(fixturePattern("async"));
  ASSERT_TRUE(dir.isValid());
  populateEntries(dir, 500);
  DirectoryModel model;
  model.load(dir.path());
  // scanning() flips synchronously inside load(); the walk itself happens on the worker thread,
  // so rowCount() being 0 right after the call is what proves the caller was never blocked.
  EXPECT_TRUE(model.scanning());
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 500);
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
  EXPECT_EQ(model.rowCount(), 10000);
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
  EXPECT_EQ(model.rowCount(), 5);
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
  ASSERT_EQ(model.rowCount(), 2);
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
  QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
  ASSERT_TRUE(QFile::remove(dir.filePath("a.txt")));
  ASSERT_FALSE(writeFile(dir, "c.txt").isEmpty());
  model.refresh();
  ASSERT_TRUE(settled(model));
  EXPECT_EQ(model.rowCount(), 2);
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
  ASSERT_EQ(model.rowCount(), 1);
  EXPECT_EQ(model.data(model.index(0), DirectoryModel::NameRole).toString(), "from-b.txt");
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
  EXPECT_EQ(model.rowCount(), 275);
  EXPECT_FALSE(model.directoryError().isEmpty());
  DirectoryModelTestAccess::failReadAfter(model, -1);
  model.refresh();
  ASSERT_TRUE(settled(model));
  ASSERT_EQ(model.rowCount(), 600);
  QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
  DirectoryModelTestAccess::failReadAfter(model, 10);
  model.refresh();
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(model.directoryError().isEmpty());
  EXPECT_EQ(model.rowCount(), 600);
  EXPECT_TRUE(removed.isEmpty());
  DirectoryModelTestAccess::beforeOpen(model, [path = dir.path()] { QDir(path).removeRecursively(); });
  model.refresh();
  ASSERT_TRUE(settled(model));
  EXPECT_FALSE(model.directoryError().isEmpty());
  EXPECT_EQ(model.rowCount(), 600);
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
  const auto changedName = model.data(model.index(0), DirectoryModel::NameRole).toString();
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
  ASSERT_EQ(model.rowCount(), 4);
  for (int row = 0; row < model.rowCount(); ++row) {
    const auto index = model.index(row);
    const auto name = model.data(index, DirectoryModel::NameRole).toString();
    const bool folder = name.startsWith("folder");
    EXPECT_FALSE(model.data(index, DirectoryModel::StatFailedRole).toBool());
    EXPECT_EQ(model.data(index, DirectoryModel::IsDirRole).toBool(), folder);
    EXPECT_EQ(model.data(index, DirectoryModel::SizeRole).toLongLong(), folder ? -1 : 11);
  }
}
