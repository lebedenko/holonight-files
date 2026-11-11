#include "trash_service.h"

#include "directory_fixtures.h"
#include "file_operation_service.h"
#include "native_operation_path.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <fcntl.h>
#include <future>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

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
  EXPECT_FALSE(dir.use_relative_path);
  EXPECT_EQ(dir.files_dir, home.filePath("Trash/files"));
  EXPECT_EQ(dir.info_dir, home.filePath("Trash/info"));
  struct stat filesStat{};
  ASSERT_EQ(::stat(QFile::encodeName(dir.files_dir).constData(), &filesStat), 0);
  EXPECT_EQ(filesStat.st_mode & 0777, 0700U);
  struct stat infoStat{};
  ASSERT_EQ(::stat(QFile::encodeName(dir.info_dir).constData(), &infoStat), 0);
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
  QFile info(dir.info_dir + "/doomed.txt.trashinfo");
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
  QFile preexisting(QDir(dir.files_dir).filePath("doc.txt"));  // pretend an earlier trash used this name
  ASSERT_TRUE(preexisting.open(QIODevice::WriteOnly));
  preexisting.close();
  const auto name = TrashService::uniqueTrashName(dir, "doc.txt");
  EXPECT_NE(name, "doc.txt");
  EXPECT_FALSE(FileOperationService::destinationExists(dir.files_dir, name));
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
  EXPECT_FALSE(QFile::exists(dir.info_dir + "/doomed.txt.trashinfo"));
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
  EXPECT_TRUE(QFile::exists(QDir(directory.info_dir).filePath("keep.trashinfo")));
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
  EXPECT_TRUE(QFile::exists(QDir(directory.info_dir).filePath("unrelated.trashinfo")));
  EXPECT_FALSE(QFile::exists(QDir(directory.info_dir).filePath("Trash.trashinfo")));
}

TEST(TrashService, NativeNamesPreserveBytesInPayloadAndTrashInfo) {
  QTemporaryDir home;
  QTemporaryDir source;
  ASSERT_TRUE(home.isValid() && source.isValid());
  const ScopedXdgDataHome guard(home.path());
  const QByteArray name = QByteArray("bad-") + static_cast<char>(0xff);
  const auto path = QFile::encodeName(source.path()) + '/' + name;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int descriptor = ::open(path.constData(), O_WRONLY | O_CREAT | O_EXCL, 0600);
  ASSERT_GE(descriptor, 0);
  ASSERT_EQ(::close(descriptor), 0);
  const auto result = TrashService::trashEntry(operationPath(path), std::make_shared<std::atomic_bool>(false));
  ASSERT_TRUE(result.complete()) << result.reason.toStdString();
  EXPECT_NE(::access(path.constData(), F_OK), 0);
  const auto payload = QFile::encodeName(home.filePath("Trash/files")) + '/' + name;
  EXPECT_EQ(::access(payload.constData(), F_OK), 0);
  const auto info = QFile::encodeName(home.filePath("Trash/info")) + '/' + name + ".trashinfo";
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int infoDescriptor = ::open(info.constData(), O_RDONLY | O_CLOEXEC);
  ASSERT_GE(infoDescriptor, 0);
  QFile metadata;
  ASSERT_TRUE(metadata.open(infoDescriptor, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle));
  EXPECT_TRUE(metadata.readAll().contains("bad-%FF"));
}

TEST(TrashService, SetgidTrashAcceptsConcurrentFilesDirectoriesAndSymlinks) {
  const auto mount = qEnvironmentVariable("FILES_TRASH_TEST_MOUNT");
  const auto fixture = mount.isEmpty() ? qEnvironmentVariable("FILES_TRASH_TEST_ROOT", QDir::tempPath()) : mount;
  QTemporaryDir home(QDir(fixture).filePath("trash-setgid-XXXXXX"));
  QTemporaryDir localData;
  ASSERT_TRUE(home.isValid() && localData.isValid());
  const ScopedXdgDataHome guard(mount.isEmpty() ? home.path() : localData.path());
  ASSERT_EQ(::chmod(QFile::encodeName(home.path()).constData(), 02700), 0);
  ASSERT_TRUE(QDir(home.path()).mkdir("first"));
  ASSERT_TRUE(QDir(home.path()).mkdir("second"));
  ASSERT_TRUE(QDir(home.path()).mkdir("third"));
  const auto first = home.filePath("first/item");
  const auto second = home.filePath("second/item");
  const auto third = home.filePath("third/item");
  ASSERT_FALSE(writeFile(home, "first/item", "regular").isEmpty());
  ASSERT_TRUE(QDir().mkdir(second));
  ASSERT_FALSE(writeFile(home, "second/item/child").isEmpty());
  ASSERT_TRUE(QFile::link(home.filePath("missing"), third));

  const auto dir = TrashService::selectTrashDir(first);
  ASSERT_FALSE(dir.error.has_value());
  const auto root = QFileInfo(dir.files_dir).absolutePath();
  if (!mount.isEmpty()) {
    EXPECT_TRUE(dir.use_relative_path);
    EXPECT_EQ(dir.topdir, mount);
    EXPECT_EQ(root, QDir(mount).filePath(QStringLiteral(".Trash-%1").arg(::getuid())));
  }
  for (const auto& path : {root, dir.files_dir, dir.info_dir}) {
    struct stat info{};
    ASSERT_EQ(::lstat(QFile::encodeName(path).constData(), &info), 0);
    EXPECT_EQ(info.st_mode & 07777, 02700U);
  }
  QFile existing(QDir(dir.files_dir).filePath("item"));
  ASSERT_TRUE(existing.open(QIODevice::WriteOnly));
  ASSERT_EQ(existing.write("existing"), 8);
  existing.close();

  const auto trash = [](const QString& source) {
    return TrashService::trashEntry(source, std::make_shared<std::atomic_bool>(false));
  };
  auto file = std::async(std::launch::async, trash, first);
  auto directory = std::async(std::launch::async, trash, second);
  auto symlink = std::async(std::launch::async, trash, third);
  for (auto* pending : {&file, &directory, &symlink}) {
    const auto result = pending->get();
    EXPECT_TRUE(result.complete()) << result.reason.toStdString();
  }
  ASSERT_TRUE(existing.open(QIODevice::ReadOnly));
  EXPECT_EQ(existing.readAll(), "existing");
  EXPECT_FALSE(QFile::exists(first));
  EXPECT_FALSE(QFile::exists(second));
  EXPECT_FALSE(QFileInfo(third).isSymLink());
  const auto payloads =
      QDir(dir.files_dir)
          .entryInfoList(QStringList{QStringLiteral("item_*")}, QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
  ASSERT_EQ(payloads.size(), 3);
  int files = 0;
  int directories = 0;
  int links = 0;
  for (const auto& payload : payloads) {
    files += static_cast<int>(payload.isFile() && !payload.isSymLink());
    directories += static_cast<int>(payload.isDir() && QFile::exists(QDir(payload.filePath()).filePath("child")));
    links += static_cast<int>(payload.isSymLink() && payload.symLinkTarget() == home.filePath("missing"));
    EXPECT_TRUE(QFile::exists(QDir(dir.info_dir).filePath(payload.fileName() + ".trashinfo")));
  }
  EXPECT_EQ(files, 1);
  EXPECT_EQ(directories, 1);
  EXPECT_EQ(links, 1);

  const auto failed = trash(root);
  EXPECT_TRUE(failed.failed);
  EXPECT_TRUE(QDir(root).exists());
  EXPECT_FALSE(QFile::exists(QDir(dir.info_dir).filePath(QFileInfo(root).fileName() + ".trashinfo")));
}
