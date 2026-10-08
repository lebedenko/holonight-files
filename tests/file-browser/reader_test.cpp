#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <HolonightFileBrowser/directory_proxy_model.h>
#include <HolonightFileBrowser/directory_reader.h>
#include <HolonightFileBrowser/places/place_list.h>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

using HolonightFileBrowser::DirectoryReader;
namespace {
bool writeFile(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7;
}
QStringList names(const QAbstractItemModel& model) {
  QStringList result;
  for (int row = 0; row < model.rowCount(); ++row) {
    result.append(model.index(row, 0).data(DirectoryRoles::NameRole).toString());
  }
  return result;
}
}  // namespace
TEST(FileBrowser, MetadataSortingHiddenAndUnicode) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  for (const auto& name : {"file10.txt", "file2.txt", ".hidden", "caf\xc3\xa9 #%?.txt"}) {
    ASSERT_TRUE(writeFile(dir.filePath(QString::fromUtf8(name))));
  }
  ASSERT_TRUE(QFile::link(dir.filePath("missing"), dir.filePath("broken")));
  DirectoryReader reader;
  reader.setPlaceIcons(PlaceList::iconMap(PlaceList::standardPlaces(dir.path(), dir.filePath("user-dirs.dirs"))));
  DirectoryProxyModel sorted;
  sorted.setSourceModel(&reader);
  reader.load(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  EXPECT_TRUE(reader.directoryError().isEmpty());
  const auto listing = names(sorted);
  EXPECT_EQ(listing.first(), "..");
  EXPECT_EQ(listing.at(1), "folder");
  EXPECT_LT(listing.indexOf("file2.txt"), listing.indexOf("file10.txt"));
  EXPECT_FALSE(listing.contains(".hidden"));
  EXPECT_TRUE(listing.contains(QString::fromUtf8("caf\xc3\xa9 #%?.txt")));
  sorted.setHiddenVisible(true);
  EXPECT_TRUE(names(sorted).contains(".hidden"));
  const auto broken = sorted.index(static_cast<int>(names(sorted).indexOf("broken")), 0);
  EXPECT_TRUE(broken.data(DirectoryRoles::StatFailedRole).toBool());
  EXPECT_TRUE(broken.data(DirectoryRoles::IsSymlinkRole).toBool());
  EXPECT_FALSE(broken.data(DirectoryRoles::IconNameRole).toString().isEmpty());
}
TEST(FileBrowser, NativePathsSurviveNonUtf8FolderAndFilename) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  const QByteArray nativeFolder = QFile::encodeName(dir.path()) + "/folder-" + static_cast<char>(0xff);
  ASSERT_EQ(::mkdir(nativeFolder.constData(), 0700), 0);
  const QByteArray nativeFile = nativeFolder + "/file-" + static_cast<char>(0xfe) + ".txt";
  const int descriptor = ::creat(nativeFile.constData(), 0600);
  ASSERT_GE(descriptor, 0);
  ASSERT_EQ(::close(descriptor), 0);
  DirectoryReader reader;
  reader.loadNative(nativeFolder);
  const bool finished = QTest::qWaitFor([&] { return !reader.scanning(); });
  EXPECT_TRUE(finished);
  EXPECT_TRUE(reader.directoryError().isEmpty());
  EXPECT_EQ(reader.nativeDirectoryPath(), nativeFolder);
  EXPECT_EQ(reader.rowCount(), 2);
  const auto file = reader.index(1, 0);
  EXPECT_EQ(file.data(DirectoryRoles::NativePathRole).toByteArray(), nativeFile);
  EXPECT_FALSE(file.data(DirectoryRoles::StatFailedRole).toBool());
  EXPECT_EQ(reader.index(0, 0).data(DirectoryRoles::NativePathRole).toByteArray(), QFile::encodeName(dir.path()));
  // Qt's text-path cleanup cannot be relied on to round-trip these fixture names.
  EXPECT_EQ(::unlink(nativeFile.constData()), 0);
  EXPECT_EQ(::rmdir(nativeFolder.constData()), 0);
}
TEST(FileBrowser, ParentRowKeepsTargetMetadataThroughASymlinkParent) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkpath("real/child"));
  ASSERT_TRUE(QFile::link(dir.filePath("real"), dir.filePath("alias")));
  DirectoryReader reader;
  reader.load(dir.filePath("alias/child"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  const auto parent = reader.index(0, 0);
  EXPECT_EQ(parent.data(DirectoryRoles::NameRole).toString(), "..");
  EXPECT_TRUE(parent.data(DirectoryRoles::IsDirRole).toBool());
  EXPECT_FALSE(parent.data(DirectoryRoles::IsSymlinkRole).toBool());
  EXPECT_EQ(parent.data(DirectoryRoles::NativePathRole).toByteArray(), QFile::encodeName(dir.filePath("alias")));
}
TEST(FileBrowser, NewLoadRejectsOldBatches) {
  QTemporaryDir oldDir;
  QTemporaryDir newDir;
  ASSERT_TRUE(oldDir.isValid());
  ASSERT_TRUE(newDir.isValid());
  for (int i = 0; i < 800; ++i) {
    ASSERT_TRUE(writeFile(oldDir.filePath(QString::number(i))));
  }
  ASSERT_TRUE(writeFile(newDir.filePath("new.txt")));
  DirectoryReader reader;
  reader.load(oldDir.path());
  // A second generation is established before any queued GUI deliveries can run.
  reader.load(newDir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  EXPECT_EQ(names(reader), (QStringList{"..", "new.txt"}));
  EXPECT_EQ(reader.directoryPath(), newDir.path());
}
TEST(FileBrowser, MissingAndRelativeLocationsReportErrorAndRecover) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  DirectoryReader reader;
  reader.load("relative");
  EXPECT_FALSE(reader.scanning());
  EXPECT_FALSE(reader.directoryError().isEmpty());
  reader.load(dir.filePath("missing"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  EXPECT_FALSE(reader.directoryError().isEmpty());
  EXPECT_EQ(reader.rowCount(), 0);
  reader.load(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  EXPECT_TRUE(reader.directoryError().isEmpty());
  EXPECT_EQ(names(reader), (QStringList{".."}));
}
TEST(FileBrowser, ShutdownDiscardsDeliveriesAndCompletesOnce) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 800; ++i) {
    ASSERT_TRUE(writeFile(dir.filePath(QString::number(i))));
  }
  DirectoryReader reader;
  QSignalSpy finished(&reader, &DirectoryReader::shutdownFinished);
  reader.load(dir.path());
  reader.shutdown();
  reader.shutdown();
  reader.load(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !finished.isEmpty(); }));
  EXPECT_EQ(finished.size(), 1);
  EXPECT_FALSE(reader.scanning());
  EXPECT_EQ(reader.rowCount(), 0);
}
TEST(FileBrowser, ShutdownAfterFirstBatchRejectsPendingRows) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 800; ++i) {
    ASSERT_TRUE(writeFile(dir.filePath(QString::number(i))));
  }
  DirectoryReader reader;
  QSignalSpy finished(&reader, &DirectoryReader::shutdownFinished);
  reader.load(dir.path());
  QObject::connect(&reader, &DirectoryReader::changed, &reader, [&] {
    if (reader.scanning() && reader.rowCount() > 0) {
      reader.shutdown();
    }
  });
  ASSERT_TRUE(QTest::qWaitFor([&] { return !finished.isEmpty(); }));
  EXPECT_GT(reader.rowCount(), 0);
  EXPECT_LT(reader.rowCount(), 801);
  const int retainedRows = reader.rowCount();
  QCoreApplication::processEvents();
  EXPECT_EQ(reader.rowCount(), retainedRows);
}
TEST(FileBrowser, ChangedListenerCanReplaceLoadWithoutDuplicateDelivery) {
  QTemporaryDir first;
  QTemporaryDir second;
  ASSERT_TRUE(first.isValid());
  ASSERT_TRUE(second.isValid());
  ASSERT_TRUE(writeFile(second.filePath("second.txt")));
  DirectoryReader reader;
  QObject::connect(&reader, &DirectoryReader::changed, &reader, [&] {
    if (reader.directoryPath() == first.path()) {
      reader.load(second.path());
    }
  });
  reader.load(first.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !reader.scanning(); }));
  EXPECT_EQ(names(reader), (QStringList{"..", "second.txt"}));
}
TEST(FileBrowser, StandardPlacesDeduplicateAndRetainHomeIcon) {
  QTemporaryDir dir;
  ASSERT_TRUE(dir.isValid());
  QFile config(dir.filePath("user-dirs.dirs"));
  ASSERT_TRUE(config.open(QIODevice::WriteOnly));
  config.write("XDG_DESKTOP_DIR=\"$HOME/Desktop\"\nXDG_DOWNLOAD_DIR=\"$HOME/Desktop\"\n");
  config.close();
  const auto places = PlaceList::standardPlaces(dir.path(), config.fileName());
  ASSERT_EQ(places.size(), 2U);
  EXPECT_TRUE(places.front().is_home);
  EXPECT_EQ(PlaceList::iconMap(places)->value(dir.path()), "user-home");
}
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
