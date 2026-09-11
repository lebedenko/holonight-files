#include "file_operation_service.h"

#include "directory_fixtures.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

using files_test::fixturePattern;
using files_test::writeFile;

namespace {
std::shared_ptr<std::atomic_bool> freshCancel() { return std::make_shared<std::atomic_bool>(false); }
}  // namespace

TEST(FileOperationService, DescribeErrnoMatchesReqF035Examples) {
  EXPECT_EQ(FileOperationService::describeErrno(EACCES), "Permission denied");
  EXPECT_EQ(FileOperationService::describeErrno(ENOSPC), "No space left on device");
  EXPECT_EQ(FileOperationService::describeErrno(EIO), "I/O error");
}

TEST(FileOperationService, DestinationExistsDetectsCollisionsAndDanglingSymlinks) {
  QTemporaryDir dir(fixturePattern("fos-exists"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "present.txt");
  QFile::link(dir.filePath("missing-target"), dir.filePath("dangling-link"));
  EXPECT_TRUE(FileOperationService::destinationExists(dir.path(), "present.txt"));
  EXPECT_TRUE(FileOperationService::destinationExists(dir.path(), "dangling-link"));
  EXPECT_FALSE(FileOperationService::destinationExists(dir.path(), "absent.txt"));
}

TEST(FileOperationService, AutoRenameCandidateFindsFirstFreeSuffix) {
  QTemporaryDir dir(fixturePattern("fos-autorename"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "report.pdf");
  writeFile(dir, "report (2).pdf");
  EXPECT_EQ(FileOperationService::autoRenameCandidate(dir.path(), "report.pdf"), "report (3).pdf");
}

TEST(FileOperationService, AutoRenameCandidateHandlesDotfilesWithoutExtension) {
  QTemporaryDir dir(fixturePattern("fos-autorename-dotfile"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, ".bashrc");
  EXPECT_EQ(FileOperationService::autoRenameCandidate(dir.path(), ".bashrc"), ".bashrc (2)");
}

TEST(FileOperationService, CopyEntryStreamsFileContentAndPreservesPermissions) {
  QTemporaryDir dir(fixturePattern("fos-copy-file"));
  ASSERT_TRUE(dir.isValid());
  const auto src = writeFile(dir, "source.txt", "the quick brown fox");
  ASSERT_FALSE(src.isEmpty());
  ::chmod(QFile::encodeName(src).constData(), 0640);
  const auto dest = dir.filePath("dest.txt");
  const auto result = FileOperationService::copyEntry(src, dest, /*overwrite=*/false, freshCancel());
  EXPECT_FALSE(result.failed);
  QFile destFile(dest);
  ASSERT_TRUE(destFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(destFile.readAll(), QByteArray("the quick brown fox"));
  struct stat info{};
  ASSERT_EQ(::stat(QFile::encodeName(dest).constData(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0640U);
  EXPECT_TRUE(QFile::exists(src));  // copy leaves the original untouched
}

TEST(FileOperationService, CopyEntryRecursesIntoNestedDirectories) {
  QTemporaryDir dir(fixturePattern("fos-copy-dir"));
  ASSERT_TRUE(dir.isValid());
  QDir(dir.path()).mkpath("tree/child");
  writeFile(dir, "tree/top.txt");
  writeFile(dir, "tree/child/leaf.txt");
  const auto dest = dir.filePath("copied-tree");
  const auto result = FileOperationService::copyEntry(dir.filePath("tree"), dest, /*overwrite=*/false, freshCancel());
  EXPECT_FALSE(result.failed);
  EXPECT_TRUE(result.nestedFailures.isEmpty());
  EXPECT_TRUE(QFile::exists(dest + "/top.txt"));
  EXPECT_TRUE(QFile::exists(dest + "/child/leaf.txt"));
  EXPECT_TRUE(QFile::exists(dir.filePath("tree/top.txt")));  // original tree untouched
}

TEST(FileOperationService, CopyEntryNestedCollisionAutoSkipsWithoutFailure) {
  QTemporaryDir dir(fixturePattern("fos-nested-collision"));
  ASSERT_TRUE(dir.isValid());
  QDir(dir.path()).mkpath("src");
  QDir(dir.path()).mkpath("dest");
  writeFile(dir, "src/keep-existing.txt", "SOURCE");
  writeFile(dir, "src/fresh.txt");
  writeFile(dir, "dest/keep-existing.txt", "PRE-EXISTING");
  const auto result =
      FileOperationService::copyEntry(dir.filePath("src"), dir.filePath("dest"), /*overwrite=*/true, freshCancel());
  EXPECT_FALSE(result.failed);
  EXPECT_TRUE(result.nestedFailures.isEmpty());  // REQ-F-048: a nested collision is not a failure
  QFile preserved(dir.filePath("dest/keep-existing.txt"));
  ASSERT_TRUE(preserved.open(QIODevice::ReadOnly));
  EXPECT_EQ(preserved.readAll(), QByteArray("PRE-EXISTING"));  // left untouched, not overwritten
  EXPECT_TRUE(QFile::exists(dir.filePath("dest/fresh.txt")));  // rest of the tree still copies
}

TEST(FileOperationService, CopyEntryOverwriteReplacesDestinationFile) {
  QTemporaryDir dir(fixturePattern("fos-overwrite"));
  ASSERT_TRUE(dir.isValid());
  const auto src = writeFile(dir, "file.txt", "NEW");
  const auto dest = dir.filePath("existing.txt");
  writeFile(dir, "existing.txt", "OLD");
  const auto result = FileOperationService::copyEntry(src, dest, /*overwrite=*/true, freshCancel());
  EXPECT_FALSE(result.failed);
  QFile destFile(dest);
  ASSERT_TRUE(destFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(destFile.readAll(), QByteArray("NEW"));
}

TEST(FileOperationService, CopySymlinkPreservesTargetStringWithoutDereferencing) {
  QTemporaryDir dir(fixturePattern("fos-copy-symlink"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QFile::link("/nonexistent/target/path", dir.filePath("link")));
  const auto dest = dir.filePath("link-copy");
  const auto result = FileOperationService::copyEntry(dir.filePath("link"), dest, /*overwrite=*/false, freshCancel());
  EXPECT_FALSE(result.failed);
  QFileInfo info(dest);
  ASSERT_TRUE(info.isSymLink());
  EXPECT_EQ(info.symLinkTarget(), QStringLiteral("/nonexistent/target/path"));
}

TEST(FileOperationService, CopyEntryAlreadyCancelledAbortsImmediately) {
  QTemporaryDir dir(fixturePattern("fos-precancelled"));
  ASSERT_TRUE(dir.isValid());
  const auto src = writeFile(dir, "source.txt");
  const auto dest = dir.filePath("dest.txt");
  auto cancel = std::make_shared<std::atomic_bool>(true);
  const auto result = FileOperationService::copyEntry(src, dest, /*overwrite=*/false, cancel);
  EXPECT_TRUE(result.cancelled);
  EXPECT_FALSE(QFile::exists(dest));
}

TEST(FileOperationService, MoveEntryUsesRenameOnSameFilesystem) {
  QTemporaryDir dir(fixturePattern("fos-move-rename"));
  ASSERT_TRUE(dir.isValid());
  const auto src = writeFile(dir, "source.txt", "payload");
  const auto dest = dir.filePath("dest.txt");
  const auto result = FileOperationService::moveEntry(src, dest, /*overwrite=*/false, freshCancel());
  EXPECT_FALSE(result.failed);
  EXPECT_FALSE(QFile::exists(src));
  EXPECT_TRUE(QFile::exists(dest));
}

TEST(FileOperationService, MoveSymlinkRelocatesLinkPreservingTarget) {
  QTemporaryDir dir(fixturePattern("fos-move-symlink"));
  ASSERT_TRUE(dir.isValid());
  const auto target = writeFile(dir, "real.txt");
  ASSERT_TRUE(QFile::link(target, dir.filePath("link")));
  const auto dest = dir.filePath("moved-link");
  const auto result = FileOperationService::moveEntry(dir.filePath("link"), dest, /*overwrite=*/false, freshCancel());
  EXPECT_FALSE(result.failed);
  EXPECT_FALSE(QFileInfo::exists(dir.filePath("link")));
  QFileInfo info(dest);
  ASSERT_TRUE(info.isSymLink());
  EXPECT_EQ(info.symLinkTarget(), target);
  EXPECT_TRUE(QFile::exists(target));  // the symlink's target itself is never touched
}

TEST(FileOperationService, CopyFromUnreadableSourceReportsPermissionDenied) {
  if (files_test::runningAsRoot()) {
    GTEST_SKIP() << "root bypasses the permission bits this fixture relies on";
  }
  QTemporaryDir dir(fixturePattern("fos-permission-denied"));
  ASSERT_TRUE(dir.isValid());
  const auto fixture = files_test::buildPermissionFixture(dir);
  const auto dest = dir.filePath("dest.txt");
  // The symlink itself is never dereferenced (REQ-F-012), so it can't exercise this — the source
  // has to be a path that actually requires traversing the chmod-000 directory to reach.
  const auto result =
      FileOperationService::copyEntry(fixture.blocked_dir + "/secret.txt", dest, /*overwrite=*/false, freshCancel());
  files_test::restorePermissionFixture(fixture);
  EXPECT_TRUE(result.failed);
  EXPECT_EQ(result.reason, "Permission denied");
  EXPECT_FALSE(QFile::exists(dest));
}

TEST(FileOperationService, SameEntryHardLinkAndDescendantDestinationsAreRejectedBeforeMutation) {
  QTemporaryDir dir(fixturePattern("fos-identity"));
  ASSERT_TRUE(dir.isValid());
  const auto source = writeFile(dir, "source", "original");
  EXPECT_TRUE(FileOperationService::copyEntry(source, source, true, freshCancel()).failed);
  const auto alias = dir.filePath("alias");
  ASSERT_EQ(::link(QFile::encodeName(source).constData(), QFile::encodeName(alias).constData()), 0);
  EXPECT_TRUE(FileOperationService::copyEntry(source, alias, true, freshCancel()).failed);
  EXPECT_TRUE(FileOperationService::moveEntry(source, alias, true, freshCancel()).failed);
  ASSERT_TRUE(QDir(dir.path()).mkpath("tree/sub"));
  writeFile(dir, "tree/keep");
  const auto tree = dir.filePath("tree");
  EXPECT_TRUE(FileOperationService::moveEntry(tree, tree, true, freshCancel()).failed);
  EXPECT_TRUE(FileOperationService::copyEntry(tree, tree + "/new", false, freshCancel()).failed);
  ASSERT_TRUE(QFile::link(tree + "/sub", dir.filePath("parent-link")));
  EXPECT_TRUE(FileOperationService::moveEntry(tree, dir.filePath("parent-link/new"), false, freshCancel()).failed);
  EXPECT_FALSE(QFile::exists(tree + "/new"));
  QFile original(source);
  ASSERT_TRUE(original.open(QIODevice::ReadOnly));
  EXPECT_EQ(original.readAll(), "original");
  EXPECT_TRUE(QFile::exists(tree + "/keep"));
}

TEST(FileOperationService, OverwriteReplacesDestinationSymlinkWithoutTouchingTarget) {
  QTemporaryDir dir(fixturePattern("fos-safe-overwrite"));
  ASSERT_TRUE(dir.isValid());
  const auto source = writeFile(dir, "source", "new");
  const auto target = writeFile(dir, "target", "keep");
  const auto dest = dir.filePath("destination");
  ASSERT_TRUE(QFile::link(target, dest));
  EXPECT_TRUE(FileOperationService::copyEntry(source, dest, true, freshCancel()).complete());
  EXPECT_FALSE(QFileInfo(dest).isSymLink());
  QFile untouched(target);
  ASSERT_TRUE(untouched.open(QIODevice::ReadOnly));
  EXPECT_EQ(untouched.readAll(), "keep");
  QFile copied(dest);
  ASSERT_TRUE(copied.open(QIODevice::ReadOnly));
  EXPECT_EQ(copied.readAll(), "new");
}

TEST(FileOperationService, IncompleteMergeRetainsEverySourceChild) {
  QTemporaryDir dir(fixturePattern("fos-retain-merge"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkpath("source/nested"));
  ASSERT_TRUE(QDir(dir.path()).mkpath("dest"));
  writeFile(dir, "source/skip", "source");
  writeFile(dir, "source/nested/copied", "copied");
  writeFile(dir, "dest/skip", "old");
  const auto result =
      FileOperationService::moveEntry(dir.filePath("source"), dir.filePath("dest"), true, freshCancel());
  EXPECT_FALSE(result.complete());
  EXPECT_FALSE(result.failed);
  EXPECT_TRUE(result.sourceRetained);
  EXPECT_EQ(result.skippedChildren.size(), 1);
  EXPECT_TRUE(result.nestedFailures.isEmpty());
  EXPECT_TRUE(result.reason.contains("source retained; some destination copies exist"));
  EXPECT_TRUE(QFile::exists(dir.filePath("source/skip")));
  EXPECT_TRUE(QFile::exists(dir.filePath("source/nested/copied")));
  EXPECT_TRUE(QFile::exists(dir.filePath("dest/nested/copied")));
}

TEST(FileOperationService, UnsupportedFifoCopyDoesNotOpenOrBlockAndMoveCanRenameIt) {
  QTemporaryDir dir(fixturePattern("fos-fifo"));
  ASSERT_TRUE(dir.isValid());
  const auto source = dir.filePath("fifo");
  ASSERT_EQ(::mkfifo(QFile::encodeName(source).constData(), 0600), 0);
  EXPECT_TRUE(FileOperationService::copyEntry(source, dir.filePath("copy"), false, freshCancel()).failed);
  EXPECT_TRUE(FileOperationService::moveEntry(source, dir.filePath("moved"), false, freshCancel()).complete());
}

TEST(FileOperationService, FailedOrCancelledOverwritePreservesDestinationAndNonOverwriteMoveRefusesIt) {
  QTemporaryDir dir(fixturePattern("fos-preserve"));
  ASSERT_TRUE(dir.isValid());
  const auto source = writeFile(dir, "source", "new");
  const auto dest = writeFile(dir, "dest", "old");
  auto cancel = freshCancel();
  cancel->store(true);
  EXPECT_TRUE(FileOperationService::copyEntry(source, dest, true, cancel).cancelled);
  EXPECT_TRUE(FileOperationService::copyEntry(dir.filePath("missing"), dest, true, freshCancel()).failed);
  EXPECT_TRUE(FileOperationService::moveEntry(source, dest, false, freshCancel()).failed);
  ASSERT_TRUE(QDir(dir.path()).mkdir("directory"));
  EXPECT_TRUE(FileOperationService::copyEntry(source, dir.filePath("directory"), true, freshCancel()).failed);
  EXPECT_TRUE(FileOperationService::moveEntry(dir.filePath("directory"), dest, true, freshCancel()).failed);
  QFile previous(dest);
  ASSERT_TRUE(previous.open(QIODevice::ReadOnly));
  EXPECT_EQ(previous.readAll(), "old");
  EXPECT_TRUE(QFile::exists(source));
  EXPECT_EQ(QDir(dir.path()).entryList({".holonight-transfer-*"}, QDir::Files | QDir::Hidden).size(), 0);
}

TEST(FileOperationService, CancellationDuringStagedOverwritePreservesOldDestination) {
  QTemporaryDir dir(fixturePattern("fos-cancel-stage"));
  ASSERT_TRUE(dir.isValid());
  const auto source = dir.filePath("source");
  QFile sparse(source);
  ASSERT_TRUE(sparse.open(QIODevice::WriteOnly));
  ASSERT_TRUE(sparse.resize(qint64{1} << 30));
  sparse.close();
  const auto destination = writeFile(dir, "destination", "previous");
  const auto cancel = freshCancel();
  auto transfer = std::async(std::launch::async,
                             [&] { return FileOperationService::copyEntry(source, destination, true, cancel); });
  QElapsedTimer timer;
  timer.start();
  bool observed = false;
  while (timer.elapsed() < 5000 && transfer.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    if (!QDir(dir.path()).entryList({".holonight-transfer-*"}, QDir::Files | QDir::Hidden).isEmpty()) {
      observed = true;
      break;
    }
    QThread::msleep(1);
  }
  cancel->store(true);
  const auto result = transfer.get();
  EXPECT_TRUE(observed);
  EXPECT_TRUE(result.cancelled);
  QFile previous(destination);
  ASSERT_TRUE(previous.open(QIODevice::ReadOnly));
  EXPECT_EQ(previous.readAll(), "previous");
  EXPECT_TRUE(QDir(dir.path()).entryList({".holonight-transfer-*"}, QDir::Files | QDir::Hidden).isEmpty());
}

TEST(FileOperationService, SocketAndDeviceCopiesAreRejectedWithoutReading) {
  QTemporaryDir dir(fixturePattern("fos-special"));
  ASSERT_TRUE(dir.isValid());
  EXPECT_TRUE(FileOperationService::copyEntry("/dev/null", dir.filePath("device"), false, freshCancel()).failed);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int parentFd = ::open(QFile::encodeName(dir.path()).constData(), O_RDONLY | O_DIRECTORY);
  ASSERT_GE(parentFd, 0);
  const int socketFd = ::socket(AF_UNIX, SOCK_STREAM, 0);
  ASSERT_GE(socketFd, 0);
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  const auto shortPath = QStringLiteral("/proc/self/fd/%1/socket").arg(parentFd).toLocal8Bit();
  std::ranges::copy(shortPath, std::begin(address.sun_path));
  // sockaddr_un is passed through POSIX bind's generic sockaddr interface.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  const int bound = ::bind(socketFd, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
  EXPECT_EQ(bound, 0) << FileOperationService::describeErrno(errno).toStdString();
  if (bound == 0) {
    EXPECT_TRUE(
        FileOperationService::copyEntry(dir.filePath("socket"), dir.filePath("copy"), false, freshCancel()).failed);
  }
  ::close(socketFd);
  ::close(parentFd);
}

TEST(FileOperationService, DirectoryMergeWithNestedErrorRetainsAllSourceEntries) {
  QTemporaryDir dir(fixturePattern("fos-merge-error"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("source"));
  ASSERT_TRUE(QDir(dir.path()).mkdir("destination"));
  writeFile(dir, "source/good");
  const auto fifo = dir.filePath("source/fifo");
  ASSERT_EQ(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  const auto result =
      FileOperationService::moveEntry(dir.filePath("source"), dir.filePath("destination"), true, freshCancel());
  EXPECT_FALSE(result.complete());
  EXPECT_TRUE(result.sourceRetained);
  EXPECT_EQ(result.nestedFailures.size(), 1);
  EXPECT_TRUE(result.skippedChildren.isEmpty());
  EXPECT_TRUE(QFile::exists(fifo));
  EXPECT_TRUE(QFile::exists(dir.filePath("source/good")));
  EXPECT_TRUE(QFile::exists(dir.filePath("destination/good")));
}

TEST(FileOperationService, CopyReadOnlyDirectoryPreservesDirectoryMetadata) {
  QTemporaryDir dir(fixturePattern("fos-readonly-tree"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkpath("source/child"));
  ASSERT_FALSE(writeFile(dir, "source/child/file.txt", "payload").isEmpty());
  const auto source = QFile::encodeName(dir.filePath("source"));
  const auto child = QFile::encodeName(dir.filePath("source/child"));
  const std::array<timespec, 2> times{
      {{.tv_sec = 1234567890, .tv_nsec = 123456789}, {.tv_sec = 1234567891, .tv_nsec = 987654321}}};
  ASSERT_EQ(::utimensat(AT_FDCWD, child.constData(), times.data(), 0), 0);
  ASSERT_EQ(::utimensat(AT_FDCWD, source.constData(), times.data(), 0), 0);
  ASSERT_EQ(::chmod(child.constData(), 0550), 0);
  ASSERT_EQ(::chmod(source.constData(), 0550), 0);
  const auto result =
      FileOperationService::copyEntry(dir.filePath("source"), dir.filePath("copy"), false, freshCancel());
  EXPECT_TRUE(result.complete());
  EXPECT_TRUE(QFile::exists(dir.filePath("copy/child/file.txt")));
  for (const auto& path : {dir.filePath("copy"), dir.filePath("copy/child")}) {
    struct stat info{};
    EXPECT_EQ(::stat(QFile::encodeName(path).constData(), &info), 0);
    EXPECT_EQ(info.st_mode & 07777, 0550U);
    EXPECT_EQ(info.st_mtim.tv_sec, times[1].tv_sec);
    EXPECT_EQ(info.st_mtim.tv_nsec, times[1].tv_nsec);
    EXPECT_EQ(::chmod(QFile::encodeName(path).constData(), 0700), 0);
  }
  EXPECT_EQ(::chmod(source.constData(), 0700), 0);
  EXPECT_EQ(::chmod(child.constData(), 0700), 0);
}
