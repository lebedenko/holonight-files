#include "task_manager.h"

#include "directory_fixtures.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::ScopedXdgDataHome;
using files_test::writeFile;

namespace {
bool waitUntilIdle(const TaskManager& tasks) {
  return QTest::qWaitFor([&] { return !tasks.busy(); });
}
}  // namespace

TEST(TaskManager, EnqueueCopySucceedsAndReportsProgress) {
  QTemporaryDir src(fixturePattern("tm-copy-src"));
  QTemporaryDir dst(fixturePattern("tm-copy-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto file = writeFile(src, "a.txt", "payload");
  TaskManager tasks;
  EXPECT_EQ(tasks.currentOperation(), TaskManager::TaskKind::Copy);
  tasks.enqueueCopy({file}, dst.path());
  EXPECT_TRUE(tasks.busy());
  EXPECT_EQ(tasks.itemsTotal(), 1);
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_EQ(tasks.itemsDone(), 1);
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_TRUE(QFile::exists(file));  // copy: original remains
  EXPECT_EQ(tasks.lastSummary().succeeded, 1);
  EXPECT_EQ(tasks.lastSummary().failed, 0);
}

TEST(TaskManager, EnqueueMoveRemovesOriginal) {
  QTemporaryDir src(fixturePattern("tm-move-src"));
  QTemporaryDir dst(fixturePattern("tm-move-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto file = writeFile(src, "a.txt");
  TaskManager tasks;
  tasks.enqueueMove({file}, dst.path());
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_FALSE(QFile::exists(file));
}

TEST(TaskManager, MultiFileRegisterIsOneSequentialTask) {
  QTemporaryDir src(fixturePattern("tm-multi-src"));
  QTemporaryDir dst(fixturePattern("tm-multi-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto sourceA = writeFile(src, "a.txt");
  const auto sourceB = writeFile(src, "b.txt");
  const auto sourceC = writeFile(src, "c.txt");
  TaskManager tasks;
  tasks.enqueueCopy({sourceA, sourceB, sourceC}, dst.path());
  EXPECT_EQ(tasks.itemsTotal(), 3);
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_EQ(tasks.itemsDone(), 3);
  EXPECT_EQ(tasks.lastSummary().succeeded, 3);
  for (const auto* name : {"a.txt", "b.txt", "c.txt"}) {
    EXPECT_TRUE(QFile::exists(dst.filePath(name)));
  }
}

TEST(TaskManager, SecondTaskWaitsUntilFirstCompletesSequentially) {
  QTemporaryDir src(fixturePattern("tm-seq-src"));
  QTemporaryDir dst(fixturePattern("tm-seq-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto sourceA = writeFile(src, "a.txt");
  const auto sourceB = writeFile(src, "b.txt");
  TaskManager tasks;
  tasks.enqueueCopy({sourceA}, dst.path());
  tasks.enqueueCopy({sourceB}, dst.path());  // queued behind the first (REQ-F-027/REQ-C-001)
  EXPECT_EQ(tasks.itemsTotal(), 1);          // first task's count only; second hasn't dispatched yet
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_TRUE(QFile::exists(dst.filePath("b.txt")));
}

TEST(TaskManager, ConflictPromptSkipLeavesDestinationUntouched) {
  QTemporaryDir src(fixturePattern("tm-conflict-skip-src"));
  QTemporaryDir dst(fixturePattern("tm-conflict-skip-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto file = writeFile(src, "file.txt", "NEW");
  writeFile(dst, "file.txt", "OLD");
  TaskManager tasks;
  tasks.enqueueCopy({file}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  EXPECT_EQ(tasks.promptKind(), TaskManager::PromptKind::Conflict);
  EXPECT_EQ(tasks.conflictSourceName(), "file.txt");
  tasks.resolveConflict(TaskManager::ConflictResolution::Skip, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
  QFile destFile(dst.filePath("file.txt"));
  ASSERT_TRUE(destFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(destFile.readAll(), QByteArray("OLD"));
  EXPECT_EQ(tasks.lastSummary().succeeded, 0);
  EXPECT_EQ(tasks.lastSummary().failed, 0);  // REQ-F-022: skipped items don't appear in the summary
}

TEST(TaskManager, ConflictPromptOverwriteReplacesDestination) {
  QTemporaryDir src(fixturePattern("tm-conflict-overwrite-src"));
  QTemporaryDir dst(fixturePattern("tm-conflict-overwrite-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto file = writeFile(src, "file.txt", "NEW");
  writeFile(dst, "file.txt", "OLD");
  TaskManager tasks;
  tasks.enqueueCopy({file}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  tasks.resolveConflict(TaskManager::ConflictResolution::Overwrite, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
  QFile destFile(dst.filePath("file.txt"));
  ASSERT_TRUE(destFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(destFile.readAll(), QByteArray("NEW"));
  EXPECT_EQ(tasks.lastSummary().succeeded, 1);
}

TEST(TaskManager, ConflictPromptAutoRenameGeneratesFreeSuffix) {
  QTemporaryDir src(fixturePattern("tm-conflict-rename-src"));
  QTemporaryDir dst(fixturePattern("tm-conflict-rename-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto file = writeFile(src, "report.pdf");
  writeFile(dst, "report.pdf");
  writeFile(dst, "report (2).pdf");
  TaskManager tasks;
  tasks.enqueueCopy({file}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  tasks.resolveConflict(TaskManager::ConflictResolution::AutoRename, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_TRUE(QFile::exists(dst.filePath("report (3).pdf")));
}

TEST(TaskManager, ConflictPromptCancelAbortsRemainingQueue) {
  QTemporaryDir src(fixturePattern("tm-conflict-cancel-src"));
  QTemporaryDir dst(fixturePattern("tm-conflict-cancel-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto sourceA = writeFile(src, "a.txt");
  const auto sourceB = writeFile(src, "b.txt");  // collides
  const auto sourceC = writeFile(src, "c.txt");
  writeFile(dst, "b.txt", "OLD");
  TaskManager tasks;
  tasks.enqueueCopy({sourceA, sourceB, sourceC}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  EXPECT_EQ(tasks.conflictSourceName(), "b.txt");
  tasks.resolveConflict(TaskManager::ConflictResolution::Cancel, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));   // processed before the conflict
  EXPECT_FALSE(QFile::exists(dst.filePath("c.txt")));  // dropped along with the rest of the queue
}

TEST(TaskManager, ConflictPromptResolutionIsPerItemNotBatch) {
  QTemporaryDir src(fixturePattern("tm-conflict-peritem-src"));
  QTemporaryDir dst(fixturePattern("tm-conflict-peritem-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto sourceA = writeFile(src, "a.txt", "NEW-A");
  const auto sourceB = writeFile(src, "b.txt", "NEW-B");
  writeFile(dst, "a.txt", "OLD-A");
  writeFile(dst, "b.txt", "OLD-B");
  TaskManager tasks;
  tasks.enqueueCopy({sourceA, sourceB}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  tasks.resolveConflict(TaskManager::ConflictResolution::Skip, tasks.promptId());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  tasks.resolveConflict(TaskManager::ConflictResolution::Overwrite, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
  QFile fileA(dst.filePath("a.txt"));
  ASSERT_TRUE(fileA.open(QIODevice::ReadOnly));
  EXPECT_EQ(fileA.readAll(), QByteArray("OLD-A"));  // skipped
  QFile fileB(dst.filePath("b.txt"));
  ASSERT_TRUE(fileB.open(QIODevice::ReadOnly));
  EXPECT_EQ(fileB.readAll(), QByteArray("NEW-B"));  // overwritten
}

TEST(TaskManager, TrashConfirmationIsMandatoryAndDeclineLeavesFileInPlace) {
  QTemporaryDir home(fixturePattern("tm-trash-decline-home"));
  QTemporaryDir src(fixturePattern("tm-trash-decline-src"));
  ASSERT_TRUE(home.isValid() && src.isValid());
  const ScopedXdgDataHome guard(home.path());
  const auto file = writeFile(src, "keep.txt");
  TaskManager tasks;
  tasks.requestTrashConfirmation({file});
  ASSERT_TRUE(tasks.hasPrompt());
  EXPECT_EQ(tasks.promptKind(), TaskManager::PromptKind::TrashConfirm);
  EXPECT_EQ(tasks.trashConfirmCount(), 1);
  tasks.respondToTrashConfirm(false, tasks.promptId());
  EXPECT_FALSE(tasks.hasPrompt());
  EXPECT_FALSE(tasks.busy());
  EXPECT_TRUE(QFile::exists(file));
}

TEST(TaskManager, TrashConfirmationConfirmMovesFileToHomeTrash) {
  QTemporaryDir home(fixturePattern("tm-trash-confirm-home"));
  QTemporaryDir src(fixturePattern("tm-trash-confirm-src"));
  ASSERT_TRUE(home.isValid() && src.isValid());
  const ScopedXdgDataHome guard(home.path());
  const auto file = writeFile(src, "gone.txt");
  TaskManager tasks;
  tasks.requestTrashConfirmation({file});
  ASSERT_TRUE(tasks.hasPrompt());
  tasks.respondToTrashConfirm(true, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_FALSE(QFile::exists(file));
  EXPECT_TRUE(QFile::exists(home.filePath("Trash/files/gone.txt")));
  EXPECT_TRUE(QFile::exists(home.filePath("Trash/info/gone.txt.trashinfo")));
  EXPECT_EQ(tasks.lastSummary().succeeded, 1);
}

TEST(TaskManager, CancelCurrentTaskDropsQueueAndStopsBusy) {
  QTemporaryDir src(fixturePattern("tm-cancel-src"));
  QTemporaryDir dst(fixturePattern("tm-cancel-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto sourceA = writeFile(src, "a.txt");
  const auto sourceB = writeFile(src, "b.txt");
  TaskManager tasks;
  tasks.enqueueCopy({sourceA}, dst.path());
  tasks.enqueueCopy({sourceB}, dst.path());
  tasks.cancelCurrentTask();
  ASSERT_TRUE(waitUntilIdle(tasks));
  // Ctrl+C is not required to have completed item "a" — only that nothing further was queued.
  EXPECT_FALSE(QFile::exists(dst.filePath("b.txt")));
}

TEST(TaskManager, CancelCurrentTaskWhileConflictPromptOpenClearsThePrompt) {
  QTemporaryDir src(fixturePattern("tm-cancel-prompt-src"));
  QTemporaryDir dst(fixturePattern("tm-cancel-prompt-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto file = writeFile(src, "file.txt");
  writeFile(dst, "file.txt", "OLD");
  TaskManager tasks;
  tasks.enqueueCopy({file}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  tasks.cancelCurrentTask();
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_FALSE(tasks.hasPrompt());  // aborted out from under the prompt, not left stale
  EXPECT_EQ(tasks.promptKind(), TaskManager::PromptKind::None);
}

TEST(TaskManager, CancelCurrentTaskIsNoOpWhenNothingIsRunning) {
  TaskManager tasks;
  tasks.cancelCurrentTask();
  EXPECT_FALSE(tasks.busy());
}

TEST(TaskManager, DirectoryPasteCountsAsOneItemRegardlessOfNestedFileCount) {
  QTemporaryDir src(fixturePattern("tm-dir-progress-src"));
  QTemporaryDir dst(fixturePattern("tm-dir-progress-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  QDir(src.path()).mkpath("bigdir/nested");
  for (int i = 0; i < 25; ++i) {
    writeFile(src, QStringLiteral("bigdir/f%1.txt").arg(i));
    writeFile(src, QStringLiteral("bigdir/nested/g%1.txt").arg(i));
  }
  const auto other1 = writeFile(src, "top1.txt");
  const auto other2 = writeFile(src, "top2.txt");
  TaskManager tasks;
  tasks.enqueueCopy({src.filePath("bigdir"), other1, other2}, dst.path());
  EXPECT_EQ(tasks.itemsTotal(), 3);  // REQ-F-047: one entry per top-level item, not per file
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_EQ(tasks.itemsDone(), 3);
  EXPECT_EQ(tasks.lastSummary().succeeded, 3);
  EXPECT_TRUE(QDir(dst.filePath("bigdir")).exists());
  EXPECT_TRUE(QFile::exists(dst.filePath("bigdir/f0.txt")));
  EXPECT_TRUE(QFile::exists(dst.filePath("bigdir/nested/g24.txt")));
}

TEST(TaskManager, HandlesQueueOfSeveralHundredFilesWithoutStallingOrLosingItems) {
  QTemporaryDir src(fixturePattern("tm-large-queue-src"));
  QTemporaryDir dst(fixturePattern("tm-large-queue-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  constexpr int kCount = 500;
  QStringList sources;
  sources.reserve(kCount);
  for (int i = 0; i < kCount; ++i) {
    sources.append(writeFile(src, QStringLiteral("f%1.txt").arg(i, 4, 10, QLatin1Char('0'))));
  }
  TaskManager tasks;
  tasks.enqueueCopy(sources, dst.path());
  EXPECT_EQ(tasks.itemsTotal(), kCount);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !tasks.busy(); }, 30000));
  EXPECT_EQ(tasks.itemsDone(), kCount);
  EXPECT_EQ(tasks.lastSummary().succeeded, kCount);
  EXPECT_EQ(tasks.lastSummary().failed, 0);
  QDir destDir(dst.path());
  EXPECT_EQ(destDir.entryList(QDir::Files).size(), kCount);
}

TEST(TaskManager, PartialFailureReportsSuccessAndFailureCountsWithReasons) {
  if (files_test::runningAsRoot()) {
    GTEST_SKIP() << "root bypasses the permission bits this fixture relies on";
  }
  QTemporaryDir src(fixturePattern("tm-partial-src"));
  QTemporaryDir dst(fixturePattern("tm-partial-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto fixture = files_test::buildPermissionFixture(src);
  const auto good1 = writeFile(src, "good1.txt");
  const auto good2 = writeFile(src, "good2.txt");
  TaskManager tasks;
  // The symlink itself is never dereferenced (REQ-F-012), so it can't exercise this — the source
  // has to be a path that actually requires traversing the chmod-000 directory to reach.
  tasks.enqueueCopy({good1, fixture.blocked_dir + "/secret.txt", good2}, dst.path());
  ASSERT_TRUE(waitUntilIdle(tasks));
  files_test::restorePermissionFixture(fixture);
  EXPECT_EQ(tasks.lastSummary().succeeded, 2);
  EXPECT_EQ(tasks.lastSummary().failed, 1);
  ASSERT_EQ(tasks.lastSummary().failures.size(), 1);
  EXPECT_EQ(tasks.lastSummary().failures.first().reason, "Permission denied");
  EXPECT_TRUE(tasks.lastSummaryText().contains("2/3"));
  EXPECT_TRUE(QFile::exists(dst.filePath("good1.txt")));
  EXPECT_TRUE(QFile::exists(dst.filePath("good2.txt")));
}

TEST(TaskManager, ConflictPromptAppearsWithinFiftyMilliseconds) {
  // REQ-NF-004: the collision check is a single lstat() (FileOperationService::destinationExists)
  // done before the first byte of I/O, so detection latency shouldn't scale with anything about
  // the operation itself — this pins that down with a real timer rather than just architecture.
  QTemporaryDir src(fixturePattern("tm-conflict-latency-src"));
  QTemporaryDir dst(fixturePattern("tm-conflict-latency-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto file = writeFile(src, "file.txt");
  writeFile(dst, "file.txt");
  TaskManager tasks;
  QElapsedTimer timer;
  timer.start();
  tasks.enqueueCopy({file}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }, 1000));
  EXPECT_LT(timer.elapsed(), 50);
  tasks.resolveConflict(TaskManager::ConflictResolution::Skip, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
}

TEST(TaskManager, CancelWhileBlockedOnPromptRespondsWithinAPollInterval) {
  // REQ-NF-002: the worker's wait loop polls the semaphore every 5ms against the cancellation
  // flag (mirroring DirectoryModel::acquireBatchSlot) — cancelling should not wait out anywhere
  // near a full I/O timescale, only a small, bounded number of poll intervals.
  QTemporaryDir src(fixturePattern("tm-cancel-latency-src"));
  QTemporaryDir dst(fixturePattern("tm-cancel-latency-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto file = writeFile(src, "file.txt");
  writeFile(dst, "file.txt");
  TaskManager tasks;
  tasks.enqueueCopy({file}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  QElapsedTimer timer;
  timer.start();
  tasks.cancelCurrentTask();
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_LT(timer.elapsed(), 100);
}

struct TaskManagerTestAccess {
  static quint64 taskId(const TaskManager& tasks) { return tasks.active_task_id_; }
  static bool waitForPostedPrompt(const TaskManager& tasks) {
    QElapsedTimer timer;
    timer.start();
    while (tasks.prompt_serial_->load() == 0 && timer.elapsed() < 2000) {
      QThread::msleep(1);
    }
    return tasks.prompt_serial_->load() > 0;
  }
};

TEST(TaskManager, CancellationBeforePromptDeliveryDiscardsQueuedCallbacks) {
  QTemporaryDir src(fixturePattern("tm-undelivered-src"));
  QTemporaryDir dst(fixturePattern("tm-undelivered-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto source = writeFile(src, "file", "new");
  writeFile(dst, "file", "old");
  TaskManager tasks;
  tasks.enqueueCopy({source}, dst.path());
  // Wait on the worker's atomic publication marker without processing GUI callbacks.
  ASSERT_TRUE(TaskManagerTestAccess::waitForPostedPrompt(tasks));
  EXPECT_FALSE(tasks.hasPrompt());
  tasks.cancelCurrentTask();
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_FALSE(tasks.hasPrompt());
  QFile destination(dst.filePath("file"));
  ASSERT_TRUE(destination.open(QIODevice::ReadOnly));
  EXPECT_EQ(destination.readAll(), "old");
}

TEST(TaskManager, StaleResponseCannotResolveNextPromptOrReusePreviousAnswer) {
  QTemporaryDir src(fixturePattern("tm-stale-src"));
  QTemporaryDir dst(fixturePattern("tm-stale-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto first = writeFile(src, "first", "new");
  const auto second = writeFile(src, "second", "new");
  writeFile(dst, "first", "old");
  writeFile(dst, "second", "old");
  TaskManager tasks;
  tasks.enqueueCopy({first, second}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  const auto oldId = tasks.promptId();
  tasks.resolveConflict(TaskManager::ConflictResolution::Skip, oldId);
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  EXPECT_NE(tasks.promptId(), oldId);
  tasks.resolveConflict(TaskManager::ConflictResolution::Overwrite, oldId);
  EXPECT_TRUE(tasks.hasPrompt());
  tasks.cancelCurrentTask();
  tasks.resolveConflict(TaskManager::ConflictResolution::Overwrite, oldId);
  ASSERT_TRUE(waitUntilIdle(tasks));
  QFile destination(dst.filePath("second"));
  ASSERT_TRUE(destination.open(QIODevice::ReadOnly));
  EXPECT_EQ(destination.readAll(), "old");
}

TEST(TaskManager, TrashConfirmationWaitsBehindConflictAndCompletionCannotClearIt) {
  QTemporaryDir src(fixturePattern("tm-trash-queued-src"));
  QTemporaryDir dst(fixturePattern("tm-trash-queued-dst"));
  QTemporaryDir home(fixturePattern("tm-trash-queued-home"));
  ASSERT_TRUE(src.isValid() && dst.isValid() && home.isValid());
  const ScopedXdgDataHome guard(home.path());
  const auto source = writeFile(src, "file");
  writeFile(dst, "file");
  TaskManager tasks;
  tasks.enqueueCopy({source}, dst.path());
  const auto firstTaskId = TaskManagerTestAccess::taskId(tasks);
  tasks.requestTrashConfirmation({source});
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  EXPECT_EQ(tasks.promptKind(), TaskManager::PromptKind::Conflict);
  EXPECT_FALSE(QFile::exists(home.filePath("Trash")));
  tasks.resolveConflict(TaskManager::ConflictResolution::Skip, tasks.promptId());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.promptKind() == TaskManager::PromptKind::TrashConfirm; }));
  tasks.onTaskFinished(firstTaskId, {}, {});  // stale completed-task callback
  EXPECT_EQ(tasks.promptKind(), TaskManager::PromptKind::TrashConfirm);
  EXPECT_FALSE(QFile::exists(home.filePath("Trash")));
  tasks.respondToTrashConfirm(false, tasks.promptId());
  EXPECT_TRUE(QFile::exists(source));
}

TEST(TaskManager, ConflictCancelDropsSeparateQueuedTasksAndConfirmations) {
  QTemporaryDir src(fixturePattern("tm-drop-src"));
  QTemporaryDir dst(fixturePattern("tm-drop-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  const auto source = writeFile(src, "file");
  const auto next = writeFile(src, "next");
  writeFile(dst, "file");
  TaskManager tasks;
  tasks.enqueueCopy({source}, dst.path());
  tasks.enqueueCopy({next}, dst.path());
  tasks.requestTrashConfirmation({source});
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  tasks.resolveConflict(TaskManager::ConflictResolution::Cancel, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_FALSE(tasks.hasPrompt());
  EXPECT_FALSE(QFile::exists(dst.filePath("next")));
  EXPECT_TRUE(QFile::exists(source));
}

TEST(TaskManager, IncompleteDirectoryMoveCountsOneItemAndReportsSkipsSeparately) {
  QTemporaryDir src(fixturePattern("tm-incomplete-src"));
  QTemporaryDir dst(fixturePattern("tm-incomplete-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  ASSERT_TRUE(QDir(src.path()).mkdir("tree"));
  ASSERT_TRUE(QDir(dst.path()).mkdir("tree"));
  writeFile(src, "tree/skip");
  writeFile(src, "tree/copied");
  writeFile(dst, "tree/skip");
  TaskManager tasks;
  tasks.enqueueMove({src.filePath("tree")}, dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return tasks.hasPrompt(); }));
  tasks.resolveConflict(TaskManager::ConflictResolution::Overwrite, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_EQ(tasks.lastSummary().succeeded, 0);
  EXPECT_EQ(tasks.lastSummary().incomplete, 1);
  EXPECT_EQ(tasks.lastSummary().failed, 0);
  EXPECT_EQ(tasks.lastSummary().skippedChildren.size(), 1);
  EXPECT_TRUE(tasks.lastSummaryText().contains("source retained; some destination copies exist"));
  EXPECT_TRUE(QFile::exists(src.filePath("tree/copied")));
}

TEST(TaskManager, TrashMixedSuccessLeavesFailedSourceUntouchedWithoutAnotherPrompt) {
  QTemporaryDir src(fixturePattern("tm-trash-mixed-src"));
  QTemporaryDir home(fixturePattern("tm-trash-mixed-home"));
  ASSERT_TRUE(src.isValid() && home.isValid());
  const ScopedXdgDataHome guard(home.path());
  const auto good = writeFile(src, "good");
  TaskManager tasks;
  tasks.requestTrashConfirmation({src.filePath("missing"), good});
  tasks.respondToTrashConfirm(true, tasks.promptId());
  ASSERT_TRUE(waitUntilIdle(tasks));
  EXPECT_FALSE(tasks.hasPrompt());
  EXPECT_EQ(tasks.lastSummary().succeeded, 1);
  EXPECT_EQ(tasks.lastSummary().failed, 1);
  EXPECT_TRUE(tasks.lastSummaryText().contains("Source lookup"));
  EXPECT_TRUE(QFile::exists(home.filePath("Trash/files/good")));
}
