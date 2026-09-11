#include "file_operation_service.h"
#include "fs_isolation.h"
#include "trash_service.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <sys/stat.h>

namespace {

// Function-local statics rather than namespace-scope globals, set once from main() before any
// test runs and read-only from then on.
fs_isolation::SetupResult& setupResult() {
  static fs_isolation::SetupResult result;
  return result;
}
QString& xdgDataHomeForTest() {
  static QString path;
  return path;
}

// GTEST_SKIP() must expand directly inside the TEST() body to return from it — a helper function
// can only return from itself, not the caller — so a macro is the idiom gtest itself uses.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define SKIP_IF_UNAVAILABLE()                                      \
  if (!setupResult().available) {                                  \
    GTEST_SKIP() << setupResult().unavailableReason.toStdString(); \
  }

std::shared_ptr<std::atomic_bool> freshCancel() { return std::make_shared<std::atomic_bool>(false); }

QString writeFile(const QString& path, const QByteArray& content = "hello") {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
    return {};
  }
  return path;
}

dev_t deviceOf(const QString& path) {
  struct stat info{};
  ::stat(QFile::encodeName(path).constData(), &info);
  return info.st_dev;
}

}  // namespace

TEST(CrossFilesystem, DistinctMountsHaveDistinctDevices) {
  SKIP_IF_UNAVAILABLE();
  const auto first = fs_isolation::mountFreshTmpfs();
  const auto second = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(first.isEmpty());
  ASSERT_FALSE(second.isEmpty());
  QTemporaryDir outer;
  EXPECT_NE(deviceOf(first), deviceOf(second));
  EXPECT_NE(deviceOf(first), deviceOf(outer.path()));
}

TEST(CrossFilesystem, MoveFallsBackToCopyOnEXDEV) {
  SKIP_IF_UNAVAILABLE();
  QTemporaryDir outer;
  ASSERT_TRUE(outer.isValid());
  const auto src = outer.filePath("file.txt");
  ASSERT_FALSE(writeFile(src, "cross-device payload").isEmpty());
  const auto foreign = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(foreign.isEmpty());
  const auto dest = foreign + "/file.txt";

  const auto cancel = freshCancel();
  const auto result = FileOperationService::moveEntry(src, dest, /*overwrite=*/false, cancel);
  EXPECT_FALSE(result.failed) << result.reason.toStdString();
  EXPECT_FALSE(QFile::exists(src));  // REQ-F-038: source removed only after the copy is confirmed
  ASSERT_TRUE(QFile::exists(dest));
  QFile check(dest);
  ASSERT_TRUE(check.open(QIODevice::ReadOnly));
  EXPECT_EQ(check.readAll(), QByteArray("cross-device payload"));
}

TEST(CrossFilesystem, MoveDirectoryFallsBackToCopyOnEXDEVAndPreservesTree) {
  SKIP_IF_UNAVAILABLE();
  QTemporaryDir outer;
  ASSERT_TRUE(outer.isValid());
  QDir(outer.path()).mkpath("tree/nested");
  ASSERT_FALSE(writeFile(outer.filePath("tree/top.txt")).isEmpty());
  ASSERT_FALSE(writeFile(outer.filePath("tree/nested/deep.txt")).isEmpty());
  const auto foreign = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(foreign.isEmpty());
  const auto dest = foreign + "/tree";

  const auto cancel = freshCancel();
  const auto result = FileOperationService::moveEntry(outer.filePath("tree"), dest, /*overwrite=*/false, cancel);
  EXPECT_FALSE(result.failed);
  EXPECT_TRUE(result.nestedFailures.isEmpty());
  EXPECT_FALSE(QDir(outer.filePath("tree")).exists());
  EXPECT_TRUE(QFile::exists(dest + "/top.txt"));
  EXPECT_TRUE(QFile::exists(dest + "/nested/deep.txt"));
}

TEST(CrossFilesystem, PerPartitionTrashUsesTrashUidWhenStickyBitSet) {
  SKIP_IF_UNAVAILABLE();
  const auto topdir = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(topdir.isEmpty());
  const auto trashPath = topdir + "/.Trash";
  QDir().mkdir(trashPath);
  ASSERT_EQ(::chmod(QFile::encodeName(trashPath).constData(), 01777), 0);  // world-writable + sticky

  QDir(topdir).mkpath("sub");
  const auto sourcePath = topdir + "/sub/doomed.txt";
  ASSERT_FALSE(writeFile(sourcePath).isEmpty());

  const auto dir = TrashService::selectTrashDir(sourcePath);
  ASSERT_FALSE(dir.error.has_value());
  EXPECT_TRUE(dir.useRelativePath);
  EXPECT_EQ(dir.topdir, topdir);
  EXPECT_TRUE(dir.filesDir.startsWith(trashPath + "/" + QString::number(::getuid())));

  const auto name = TrashService::uniqueTrashName(dir, QStringLiteral("doomed.txt"));
  ASSERT_FALSE(TrashService::writeTrashInfo(dir, name, sourcePath));
  QFile info(dir.infoDir + "/" + name + ".trashinfo");
  ASSERT_TRUE(info.open(QIODevice::ReadOnly));
  const auto contents = QString::fromUtf8(info.readAll());
  EXPECT_TRUE(contents.contains("[Trash Info]"));
  EXPECT_TRUE(contents.contains("Path=sub/doomed.txt"));  // REQ-F-045: relative to $topdir
  EXPECT_TRUE(contents.contains("DeletionDate="));
}

TEST(CrossFilesystem, PerPartitionTrashRejectsSymlinkedTrashDirectory) {
  SKIP_IF_UNAVAILABLE();
  const auto topdir = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(topdir.isEmpty());
  QDir(topdir).mkdir("elsewhere");
  ASSERT_TRUE(QFile::link(topdir + "/elsewhere", topdir + "/.Trash"));  // REQ-F-042: never used

  const auto sourcePath = topdir + "/victim.txt";
  ASSERT_FALSE(writeFile(sourcePath).isEmpty());
  const auto dir = TrashService::selectTrashDir(sourcePath);
  ASSERT_FALSE(dir.error.has_value());  // falls through to .Trash-$uid instead
  EXPECT_TRUE(dir.filesDir.contains(".Trash-" + QString::number(::getuid())));
}

TEST(CrossFilesystem, PerPartitionTrashRejectsTrashDirectoryWithoutStickyBit) {
  SKIP_IF_UNAVAILABLE();
  const auto topdir = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(topdir.isEmpty());
  const auto trashPath = topdir + "/.Trash";
  QDir().mkdir(trashPath);
  ASSERT_EQ(::chmod(QFile::encodeName(trashPath).constData(), 0777), 0);  // no sticky bit

  const auto sourcePath = topdir + "/victim.txt";
  ASSERT_FALSE(writeFile(sourcePath).isEmpty());
  const auto dir = TrashService::selectTrashDir(sourcePath);
  ASSERT_FALSE(dir.error.has_value());
  EXPECT_TRUE(dir.filesDir.contains(".Trash-" + QString::number(::getuid())));  // REQ-F-043 fallback
}

TEST(CrossFilesystem, PerPartitionTrashFallsBackToTrashDashUidWhenTrashMissing) {
  SKIP_IF_UNAVAILABLE();
  const auto topdir = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(topdir.isEmpty());
  const auto sourcePath = topdir + "/victim.txt";
  ASSERT_FALSE(writeFile(sourcePath).isEmpty());

  const auto dir = TrashService::selectTrashDir(sourcePath);
  ASSERT_FALSE(dir.error.has_value());
  EXPECT_TRUE(QDir(dir.filesDir).exists());
  EXPECT_TRUE(QDir(dir.infoDir).exists());
  struct stat info{};
  ASSERT_EQ(::stat(QFile::encodeName(topdir + "/.Trash-" + QString::number(::getuid())).constData(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0700U);
}

TEST(CrossFilesystem, FailedTrashLeavesSourceUntouched) {
  SKIP_IF_UNAVAILABLE();
  // A namespace we mount ourselves grants us CAP_DAC_OVERRIDE over it (we appear as "root" for
  // objects it owns), so chmod-based permission denial can't simulate "topdir unwritable" here —
  // unlike files-smoke's chmod-000 fixtures, which live on the pre-existing outer filesystem this
  // namespace does not own. Capping the mount's inode count instead is the reliable equivalent:
  // once it's exhausted, mkdir() fails with ENOSPC regardless of capability — exactly the "mkdir
  // call failing for any reason" DESIGN.md names as a trigger for this fallback.
  const auto topdir = fs_isolation::mountFreshTmpfs("nr_inodes=2");
  ASSERT_FALSE(topdir.isEmpty());
  const auto sourcePath = topdir + "/victim.txt";
  ASSERT_FALSE(writeFile(sourcePath).isEmpty());  // consumes the mount's one remaining inode

  const auto dir = TrashService::selectTrashDir(sourcePath);
  EXPECT_TRUE(dir.error.has_value());  // REQ-F-044: never falls back to a cross-device home-trash copy

  const auto removeResult = TrashService::trashEntry(sourcePath, freshCancel());
  EXPECT_TRUE(removeResult.failed) << removeResult.reason.toStdString();
  EXPECT_TRUE(QFile::exists(sourcePath));
}

TEST(CrossFilesystem, HomeTrashUsedWhenOnSameDeviceAsXdgDataHome) {
  SKIP_IF_UNAVAILABLE();
  QTemporaryDir outer;
  ASSERT_TRUE(outer.isValid());
  const auto sourcePath = outer.filePath("home-trash-me.txt");
  ASSERT_FALSE(writeFile(sourcePath).isEmpty());
  const auto dir = TrashService::selectTrashDir(sourcePath);
  ASSERT_FALSE(dir.error.has_value());
  EXPECT_FALSE(dir.useRelativePath);
  EXPECT_TRUE(dir.filesDir.startsWith(xdgDataHomeForTest()));
}

TEST(CrossFilesystem, IndependentTrashDirectoriesAreNotMerged) {
  SKIP_IF_UNAVAILABLE();
  QTemporaryDir outer;
  ASSERT_TRUE(outer.isValid());
  const auto homeSource = outer.filePath("home-item.txt");
  ASSERT_FALSE(writeFile(homeSource).isEmpty());
  const auto topdir = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(topdir.isEmpty());
  const auto partitionSource = topdir + "/partition-item.txt";
  ASSERT_FALSE(writeFile(partitionSource).isEmpty());

  const auto homeDir = TrashService::selectTrashDir(homeSource);
  const auto partitionDir = TrashService::selectTrashDir(partitionSource);
  ASSERT_FALSE(homeDir.error.has_value());
  ASSERT_FALSE(partitionDir.error.has_value());
  EXPECT_NE(homeDir.filesDir, partitionDir.filesDir);
  EXPECT_FALSE(homeDir.useRelativePath);
  EXPECT_TRUE(partitionDir.useRelativePath);
}

TEST(CrossFilesystem, CopyToCapacityLimitedFilesystemReportsNoSpaceAndCleansUpPartial) {
  SKIP_IF_UNAVAILABLE();
  const auto small = fs_isolation::mountFreshTmpfs("size=65536");
  ASSERT_FALSE(small.isEmpty());
  QTemporaryDir outer;
  ASSERT_TRUE(outer.isValid());
  const auto src = outer.filePath("too-big.bin");
  {
    QFile file(src);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    const QByteArray chunk(1 << 16, 'x');  // 64 KiB chunks, well past the 64 KiB tmpfs cap
    for (int i = 0; i < 8; ++i) {
      file.write(chunk);
    }
  }
  const auto dest = small + "/too-big.bin";
  const auto cancel = freshCancel();
  const auto result = FileOperationService::copyEntry(src, dest, /*overwrite=*/false, cancel);
  EXPECT_TRUE(result.failed);
  EXPECT_EQ(result.reason, QObject::tr("No space left on device"));
  EXPECT_FALSE(QFile::exists(dest));  // REQ-F-032: no partial destination left behind
}

int main(int argc, char* argv[]) {
  setupResult() = fs_isolation::setUp();
  const QCoreApplication app(argc, argv);
  const auto fixtureRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/fixtures/fsops");
  if (!QDir().mkpath(fixtureRoot)) {
    return 1;
  }
  qputenv("TMPDIR", QFile::encodeName(fixtureRoot));
  static QTemporaryDir home;  // static duration: lives for the process, cleaned up at exit
  if (setupResult().available) {
    xdgDataHomeForTest() = home.path();
    qputenv("XDG_DATA_HOME", xdgDataHomeForTest().toLocal8Bit());
  }
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

TEST(CrossFilesystem, NestedErrorsRetainCompleteSourceAndExistingDestination) {
  SKIP_IF_UNAVAILABLE();
  const auto destination = fs_isolation::mountFreshTmpfs("size=65536");
  ASSERT_FALSE(destination.isEmpty());
  QTemporaryDir source;
  ASSERT_TRUE(source.isValid());
  ASSERT_TRUE(QDir(source.path()).mkdir("tree"));
  const auto tree = source.filePath("tree");
  ASSERT_FALSE(writeFile(tree + "/good", "small").isEmpty());
  ASSERT_FALSE(writeFile(tree + "/large", QByteArray(1 << 20, 'x')).isEmpty());
  const auto result = FileOperationService::moveEntry(tree, destination + "/tree", false, freshCancel());
  EXPECT_FALSE(result.complete());
  EXPECT_TRUE(result.sourceRetained);
  EXPECT_FALSE(result.nestedFailures.isEmpty());
  EXPECT_TRUE(QFile::exists(tree + "/good"));
  EXPECT_TRUE(QFile::exists(tree + "/large"));
  EXPECT_FALSE(QFile::exists(destination + "/tree/large"));
  const auto old = destination + "/existing";
  ASSERT_FALSE(writeFile(old, "old").isEmpty());
  EXPECT_TRUE(FileOperationService::copyEntry(tree + "/large", old, true, freshCancel()).failed);
  QFile previous(old);
  ASSERT_TRUE(previous.open(QIODevice::ReadOnly));
  EXPECT_EQ(previous.readAll(), "old");
}

TEST(CrossFilesystem, SkippedMergeChildrenAndUnsupportedFallbackRetainSource) {
  SKIP_IF_UNAVAILABLE();
  const auto destination = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(destination.isEmpty());
  QTemporaryDir source;
  ASSERT_TRUE(source.isValid());
  ASSERT_TRUE(QDir(source.path()).mkdir("tree"));
  ASSERT_TRUE(QDir(destination).mkdir("tree"));
  const auto tree = source.filePath("tree");
  ASSERT_FALSE(writeFile(tree + "/skip").isEmpty());
  ASSERT_FALSE(writeFile(tree + "/copy").isEmpty());
  ASSERT_FALSE(writeFile(destination + "/tree/skip", "old").isEmpty());
  const auto result = FileOperationService::moveEntry(tree, destination + "/tree", true, freshCancel());
  EXPECT_FALSE(result.complete());
  EXPECT_TRUE(result.sourceRetained);
  EXPECT_EQ(result.skippedChildren.size(), 1);
  EXPECT_TRUE(result.nestedFailures.isEmpty());
  EXPECT_TRUE(QFile::exists(tree + "/copy"));
  EXPECT_TRUE(QFile::exists(tree + "/skip"));
  EXPECT_TRUE(QFile::exists(destination + "/tree/copy"));
  const auto fifo = source.filePath("fifo");
  ASSERT_EQ(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  EXPECT_TRUE(FileOperationService::moveEntry(fifo, destination + "/fifo", false, freshCancel()).failed);
  EXPECT_TRUE(QFile::exists(fifo));
}

TEST(CrossFilesystem, InvalidSharedUserDirectoryFallsBack) {
  SKIP_IF_UNAVAILABLE();
  const auto top = fs_isolation::mountFreshTmpfs();
  ASSERT_FALSE(top.isEmpty());
  const auto shared = top + "/.Trash";
  ASSERT_TRUE(QDir().mkdir(shared));
  ASSERT_EQ(::chmod(QFile::encodeName(shared).constData(), 01777), 0);
  const auto uidPath = shared + "/" + QString::number(::getuid());
  ASSERT_TRUE(QDir().mkdir(uidPath));
  ASSERT_EQ(::chmod(QFile::encodeName(uidPath).constData(), 0755), 0);
  const auto source = writeFile(top + "/source");
  ASSERT_FALSE(source.isEmpty());
  const auto directory = TrashService::selectTrashDir(source);
  ASSERT_FALSE(directory.error.has_value());
  EXPECT_TRUE(directory.filesDir.startsWith(top + "/.Trash-"));
}

TEST(CrossFilesystem, CancellationDuringDirectoryCopyRetainsEntireSourceTree) {
  SKIP_IF_UNAVAILABLE();
  const auto destination = fs_isolation::mountFreshTmpfs("size=2147483648");
  ASSERT_FALSE(destination.isEmpty());
  QTemporaryDir source;
  ASSERT_TRUE(source.isValid());
  ASSERT_TRUE(QDir(source.path()).mkdir("tree"));
  const auto tree = source.filePath("tree");
  ASSERT_FALSE(writeFile(tree + "/small").isEmpty());
  QFile large(tree + "/large");
  ASSERT_TRUE(large.open(QIODevice::WriteOnly));
  ASSERT_TRUE(large.resize(qint64{1} << 30));
  large.close();
  const auto cancel = freshCancel();
  auto transfer = std::async(
      std::launch::async, [&] { return FileOperationService::moveEntry(tree, destination + "/tree", false, cancel); });
  QElapsedTimer timer;
  timer.start();
  bool observed = false;
  while (timer.elapsed() < 5000 && transfer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    if (!QDir(destination + "/tree").entryList({".holonight-transfer-*"}, QDir::Files | QDir::Hidden).isEmpty()) {
      observed = true;
      break;
    }
    QThread::msleep(1);
  }
  cancel->store(true);
  const auto result = transfer.get();
  EXPECT_TRUE(observed);
  EXPECT_TRUE(result.cancelled);
  EXPECT_TRUE(result.sourceRetained);
  EXPECT_TRUE(QFile::exists(tree + "/small"));
  EXPECT_TRUE(QFile::exists(tree + "/large"));
  EXPECT_TRUE(QDir(destination + "/tree").entryList({".holonight-transfer-*"}, QDir::Files | QDir::Hidden).isEmpty());
}

TEST(CrossFilesystem, MetadataDiskFullLeavesSourceAndNoOrphanInfo) {
  SKIP_IF_UNAVAILABLE();
  const auto top = fs_isolation::mountFreshTmpfs("size=4096");
  ASSERT_FALSE(top.isEmpty());
  const auto source = writeFile(top + "/source", QByteArray(4096, 'x'));
  ASSERT_FALSE(source.isEmpty());
  const auto result = TrashService::trashEntry(source, freshCancel());
  ASSERT_TRUE(result.failed);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(result.error->kind, TrashService::FailureKind::Metadata);
  EXPECT_TRUE(QFile::exists(source));
  const auto directory = TrashService::selectTrashDir(source);
  ASSERT_FALSE(directory.error.has_value());
  EXPECT_TRUE(QDir(directory.filesDir).entryList(QDir::Files).isEmpty());
  EXPECT_TRUE(QDir(directory.infoDir).entryList(QDir::Files).isEmpty());
}
