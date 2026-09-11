#include "trash_service.h"

#include "directory_fixtures.h"
#include "file_operation_service.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>
#include <sys/stat.h>

using files_test::fixturePattern;
using files_test::ScopedXdgDataHome;
using files_test::writeFile;

TEST(TrashService, HomeTrashDirectoryIsCreatedWithFilesAndInfoAt0700) {
  QTemporaryDir home(fixturePattern("trash-home"));
  ASSERT_TRUE(home.isValid());
  const ScopedXdgDataHome guard(home.path());
  QTemporaryDir source(fixturePattern("trash-home-src"));
  ASSERT_TRUE(source.isValid());
  const auto path = writeFile(source, "doomed.txt");

  const auto dir = TrashService::selectTrashDir(path);
  ASSERT_FALSE(dir.error.has_value());
  EXPECT_FALSE(dir.useRelativePath);
  EXPECT_EQ(dir.filesDir, home.filePath("Trash/files"));
  EXPECT_EQ(dir.infoDir, home.filePath("Trash/info"));
  struct stat filesStat{};
  ASSERT_EQ(::stat(QFile::encodeName(dir.filesDir).constData(), &filesStat), 0);
  EXPECT_EQ(filesStat.st_mode & 0777, 0700U);
  struct stat infoStat{};
  ASSERT_EQ(::stat(QFile::encodeName(dir.infoDir).constData(), &infoStat), 0);
  EXPECT_EQ(infoStat.st_mode & 0777, 0700U);
}

TEST(TrashService, WriteTrashInfoUsesAbsolutePathForHomeTrash) {
  QTemporaryDir home(fixturePattern("trash-info-home"));
  ASSERT_TRUE(home.isValid());
  const ScopedXdgDataHome guard(home.path());
  QTemporaryDir source(fixturePattern("trash-info-home-src"));
  ASSERT_TRUE(source.isValid());
  const auto path = writeFile(source, "doomed.txt");

  const auto dir = TrashService::selectTrashDir(path);
  ASSERT_FALSE(dir.error.has_value());
  ASSERT_FALSE(TrashService::writeTrashInfo(dir, "doomed.txt", path));
  QFile info(dir.infoDir + "/doomed.txt.trashinfo");
  ASSERT_TRUE(info.open(QIODevice::ReadOnly));
  const auto lines = QString::fromUtf8(info.readAll()).split('\n', Qt::SkipEmptyParts);
  ASSERT_GE(lines.size(), 3);
  EXPECT_EQ(lines[0], "[Trash Info]");
  EXPECT_EQ(lines[1], QStringLiteral("Path=%1").arg(path));  // REQ-F-018: absolute for home trash
  EXPECT_TRUE(lines[2].startsWith("DeletionDate="));
  const auto date = lines[2].mid(QStringLiteral("DeletionDate=").size());
  EXPECT_TRUE(QDateTime::fromString(date, Qt::ISODate).isValid());
}

TEST(TrashService, UniqueTrashNameAvoidsCollidingWithExistingFilesOrInfo) {
  QTemporaryDir home(fixturePattern("trash-unique"));
  ASSERT_TRUE(home.isValid());
  const ScopedXdgDataHome guard(home.path());
  QTemporaryDir source(fixturePattern("trash-unique-src"));
  ASSERT_TRUE(source.isValid());
  const auto path = writeFile(source, "doc.txt");
  const auto dir = TrashService::selectTrashDir(path);
  ASSERT_FALSE(dir.error.has_value());
  QFile preexisting(QDir(dir.filesDir).filePath("doc.txt"));  // pretend an earlier trash used this name
  ASSERT_TRUE(preexisting.open(QIODevice::WriteOnly));
  preexisting.close();
  const auto name = TrashService::uniqueTrashName(dir, "doc.txt");
  EXPECT_NE(name, "doc.txt");
  EXPECT_FALSE(FileOperationService::destinationExists(dir.filesDir, name));
}

TEST(TrashService, RemoveTrashInfoDeletesOnlyTheSidecar) {
  QTemporaryDir home(fixturePattern("trash-rollback"));
  ASSERT_TRUE(home.isValid());
  const ScopedXdgDataHome guard(home.path());
  QTemporaryDir source(fixturePattern("trash-rollback-src"));
  ASSERT_TRUE(source.isValid());
  const auto path = writeFile(source, "doomed.txt");
  const auto dir = TrashService::selectTrashDir(path);
  ASSERT_FALSE(dir.error.has_value());
  ASSERT_FALSE(TrashService::writeTrashInfo(dir, "doomed.txt", path));
  TrashService::removeTrashInfo(dir, "doomed.txt");
  EXPECT_FALSE(QFile::exists(dir.infoDir + "/doomed.txt.trashinfo"));
  EXPECT_TRUE(QFile::exists(path));  // only the sidecar is touched, never the original
}

TEST(TrashService, ExistingNonPrivateOrSymlinkTrashIsRejectedWithoutRepair) {
  QTemporaryDir home(fixturePattern("trash-invalid-home"));
  QTemporaryDir src(fixturePattern("trash-invalid-src"));
  ASSERT_TRUE(home.isValid() && src.isValid());
  const ScopedXdgDataHome guard(home.path());
  const auto source = writeFile(src, "keep");
  const auto trash = home.filePath("Trash");
  ASSERT_TRUE(QDir().mkdir(trash));
  ASSERT_EQ(::chmod(QFile::encodeName(trash).constData(), 0755), 0);
  const auto directory = TrashService::selectTrashDir(source);
  ASSERT_TRUE(directory.error.has_value());
  EXPECT_EQ(directory.error->kind, TrashService::FailureKind::Validation);
  EXPECT_EQ(directory.error->path, trash);
  struct stat info{};
  ASSERT_EQ(::lstat(QFile::encodeName(trash).constData(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0755);
  EXPECT_TRUE(TrashService::trashEntry(source, std::make_shared<std::atomic_bool>(false)).failed);
  EXPECT_TRUE(QFile::exists(source));
  ASSERT_TRUE(QDir().rmdir(trash));
  ASSERT_TRUE(QFile::link(src.path(), trash));
  EXPECT_TRUE(TrashService::selectTrashDir(source).error.has_value());
  EXPECT_FALSE(QFile::exists(src.filePath("files")));
}

TEST(TrashService, MetadataFailureAndCancelledTrashLeaveSourceAndExistingMetadataUntouched) {
  QTemporaryDir home(fixturePattern("trash-metadata-home"));
  QTemporaryDir src(fixturePattern("trash-metadata-src"));
  ASSERT_TRUE(home.isValid() && src.isValid());
  const ScopedXdgDataHome guard(home.path());
  const auto source = writeFile(src, "keep");
  const auto directory = TrashService::selectTrashDir(source);
  ASSERT_FALSE(directory.error.has_value());
  ASSERT_FALSE(TrashService::writeTrashInfo(directory, "keep", source).has_value());
  const auto duplicate = TrashService::writeTrashInfo(directory, "keep", source);
  ASSERT_TRUE(duplicate.has_value());
  EXPECT_EQ(duplicate->kind, TrashService::FailureKind::Metadata);
  EXPECT_TRUE(QFile::exists(QDir(directory.infoDir).filePath("keep.trashinfo")));
  auto cancel = std::make_shared<std::atomic_bool>(true);
  EXPECT_TRUE(TrashService::trashEntry(source, cancel).cancelled);
  EXPECT_TRUE(QFile::exists(source));
}

TEST(TrashService, FailedMoveRollsBackOnlyItsOwnMetadata) {
  QTemporaryDir home(fixturePattern("trash-move-failure-home"));
  ASSERT_TRUE(home.isValid());
  const ScopedXdgDataHome guard(home.path());
  const auto source = writeFile(home, "source");
  const auto directory = TrashService::selectTrashDir(source);
  ASSERT_FALSE(directory.error.has_value());
  ASSERT_FALSE(TrashService::writeTrashInfo(directory, "unrelated", source).has_value());
  // A directory cannot move into its own descendant. Metadata has already been completed
  // when the kernel rejects this rename, so rollback must leave unrelated metadata alone.
  const auto result = TrashService::trashEntry(home.filePath("Trash"), std::make_shared<std::atomic_bool>(false));
  ASSERT_TRUE(result.failed);
  ASSERT_TRUE(result.error.has_value());
  EXPECT_EQ(result.error->kind, TrashService::FailureKind::Move);
  EXPECT_TRUE(QDir(home.filePath("Trash")).exists());
  EXPECT_TRUE(QFile::exists(QDir(directory.infoDir).filePath("unrelated.trashinfo")));
  EXPECT_FALSE(QFile::exists(QDir(directory.infoDir).filePath("Trash.trashinfo")));
}
