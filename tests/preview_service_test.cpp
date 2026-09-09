#include "preview_service.h"

#include "directory_fixtures.h"
#include "preview_fixtures.h"
#include "preview_service_test_access.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <gtest/gtest.h>
#include <sys/stat.h>
#include <unistd.h>

using files_test::fixturePattern;
using files_test::writeJpegWithExif;
using files_test::writeLargeText;
using files_test::writeSmallText;

namespace {
bool settled(const PreviewService& service) {
  return QTest::qWaitFor([&] { return !service.busy(); }, 5000);
}
void setTargetFromFile(PreviewService& service, const QString& path) {
  const QFileInfo info(path);
  service.setTarget(path, info.isDir(), info.isDir() ? -1 : info.size(), info.lastModified(), 0100644, false, {});
}
}  // namespace

TEST(PreviewService, SetTargetUpdatesGenericMetadataSynchronously) {
  QTemporaryDir dir(fixturePattern("preview-metadata"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeSmallText(dir);
  PreviewService service;
  setTargetFromFile(service, path);
  // No qWait: the metadata block must already be correct the instant setTarget() returns.
  EXPECT_TRUE(service.hasEntry());
  EXPECT_EQ(service.name(), QFileInfo(path).fileName());
  EXPECT_EQ(service.size(), QFileInfo(path).size());
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.hasText());
}

TEST(PreviewService, ClearResetsToThePlaceholderState) {
  QTemporaryDir dir(fixturePattern("preview-clear"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeSmallText(dir);
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  service.clear();
  EXPECT_FALSE(service.hasEntry());
  EXPECT_FALSE(service.hasText());
  EXPECT_FALSE(service.hasImage());
}

TEST(PreviewService, RapidRetargetingOnlyEverShowsTheLastTarget) {
  QTemporaryDir dir(fixturePattern("preview-rapid"));
  ASSERT_TRUE(dir.isValid());
  const auto pathA = writeSmallText(dir, "a.txt");
  const auto pathB = writeSmallText(dir, "b.txt");
  const auto pathC = writeSmallText(dir, "c.txt");
  PreviewService service;
  // Each setTarget() bumps the generation synchronously before any worker result can land, so
  // A's and B's results — however fast they complete — must never overwrite C's.
  setTargetFromFile(service, pathA);
  setTargetFromFile(service, pathB);
  setTargetFromFile(service, pathC);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.name(), QStringLiteral("c.txt"));
}

TEST(PreviewService, DecodeTimeoutSetsErrorAndIgnoresALateSuccessfulResult) {
  QTemporaryDir dir(fixturePattern("preview-timeout"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeJpegWithExif(dir);
  PreviewService service;
  PreviewServiceTestAccess::beforeDispatch(service, [] { QThread::msleep(3500); });
  setTargetFromFile(service, path);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return service.previewErrorKind() == PreviewService::PreviewErrorKind::DecodeTimeout; }, 4500));
  EXPECT_FALSE(service.busy());
  // The worker's slow-but-successful decode arrives ~3.5s after dispatch, well after the 3s
  // timeout already fired; it must not silently replace the timeout notice (REQ-F-016).
  QTest::qWait(500);
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::DecodeTimeout);
  EXPECT_FALSE(service.hasImage());
}

TEST(PreviewService, ANewTargetAfterATimeoutClearsTheTimedOutFlag) {
  QTemporaryDir dir(fixturePattern("preview-timeout-recovery"));
  ASSERT_TRUE(dir.isValid());
  const auto slow = writeJpegWithExif(dir, "slow.jpg");
  const auto fast = writeSmallText(dir, "fast.txt");
  PreviewService service;
  PreviewServiceTestAccess::beforeDispatch(service, [] { QThread::msleep(3500); });
  setTargetFromFile(service, slow);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return service.previewErrorKind() == PreviewService::PreviewErrorKind::DecodeTimeout; }, 4500));
  PreviewServiceTestAccess::beforeDispatch(service, {});
  setTargetFromFile(service, fast);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::None);
  EXPECT_TRUE(service.hasText());
}

TEST(PreviewService, DirectoryTargetSetsInodeDirectoryMimeTypeWithNoWorkerDispatch) {
  QTemporaryDir dir(fixturePattern("preview-dir"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("child"));
  PreviewService service;
  service.setTarget(dir.filePath("child"), true, -1, QFileInfo(dir.path()).lastModified(), 040755, false, {});
  EXPECT_EQ(service.mimeType(), QStringLiteral("inode/directory"));
  EXPECT_FALSE(service.busy());
}

TEST(PreviewService, StatFailedTargetIsReportedAsAnErrorWithoutWorkerDispatch) {
  PreviewService service;
  service.setTarget(QStringLiteral("/nonexistent/broken-link"), false, -1, {}, 0, true,
                    QStringLiteral("Broken symbolic link"));
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::BrokenSymlink);
  EXPECT_FALSE(service.busy());
}

TEST(PreviewService, EmptyPathClearsTheService) {
  QTemporaryDir dir(fixturePattern("preview-empty-path"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeSmallText(dir);
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  service.setTarget({}, false, -1, {}, 0, false, {});
  EXPECT_FALSE(service.hasEntry());
}

TEST(PreviewService, ThumbnailIsDeliveredWhileFullDecodeIsPending) {
  QTemporaryDir dir(fixturePattern("progressive"));
  const auto path = writeJpegWithExif(dir);
  PreviewService service;
  PreviewServiceTestAccess::beforeFullDecode(service, [] { QThread::msleep(400); });
  setTargetFromFile(service, path);
  ASSERT_TRUE(QTest::qWaitFor([&] { return service.hasImage(); }, 1000));
  EXPECT_TRUE(service.busy());
  EXPECT_LE(service.image().width(), 128);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.exifPresent());
}

TEST(PreviewService, TimeoutClearsAlreadyDeliveredThumbnail) {
  QTemporaryDir dir(fixturePattern("progressive-timeout"));
  const auto path = writeJpegWithExif(dir);
  PreviewService service;
  PreviewServiceTestAccess::beforeFullDecode(service, [] { QThread::msleep(3400); });
  setTargetFromFile(service, path);
  ASSERT_TRUE(QTest::qWaitFor([&] { return service.hasImage(); }, 1000));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !service.busy(); }, 4000));
  EXPECT_FALSE(service.hasImage());
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::DecodeTimeout);
  QTest::qWait(500);
  EXPECT_FALSE(service.hasImage());
}

TEST(PreviewService, PendingRequestsAreReplacedAndFullImagesAreReused) {
  QTemporaryDir dir(fixturePattern("pending-cache"));
  const auto path = writeJpegWithExif(dir);
  const auto text = writeSmallText(dir);
  PreviewService service;
  std::atomic_int starts = 0;
  std::atomic_int decodes = 0;
  PreviewServiceTestAccess::beforeDispatch(service, [&] {
    ++starts;
    QThread::msleep(150);
  });
  PreviewServiceTestAccess::beforeFullDecode(service, [&] { ++decodes; });
  setTargetFromFile(service, path);
  ASSERT_TRUE(QTest::qWaitFor([&] { return starts.load() == 1; }));
  for (int i = 0; i < 50; ++i) {
    setTargetFromFile(service, text);
    setTargetFromFile(service, path);
  }
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(starts.load(), 2);
  EXPECT_EQ(decodes.load(), 1);
  setTargetFromFile(service, text);
  ASSERT_TRUE(settled(service));
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(decodes.load(), 1);
}

TEST(PreviewService, GradualGrowthAndConsumerChangesUseActualRequiredPixels) {
  QTemporaryDir dir(fixturePattern("consumer-size"));
  const auto path = writeJpegWithExif(dir);
  PreviewService service;
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {200, 200});
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  const auto first = service.image().size();
  for (int size = 201; size <= 260; ++size) {
    service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {size, size});
  }
  ASSERT_TRUE(QTest::qWaitFor([&] { return service.image().width() > first.width(); }));
  service.setRequestedSize(PreviewService::PreviewConsumer::QuickLook, {600, 600});
  QTest::qWait(200);
  EXPECT_LE(service.image().width(), 260);
  service.setQuickLookActive(true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return service.image().width() == 600; }));
  service.setQuickLookActive(false);
  service.setRequestedSize(PreviewService::PreviewConsumer::QuickLook, {900, 900});
  QTest::qWait(200);
  EXPECT_EQ(service.image().width(), 600);
}

TEST(PreviewService, SpecialFilesAndReplacementCannotBlockWorkerOrShutdown) {
  if (!qEnvironmentVariableIsSet("FILES_FIFO_CHILD")) {
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("FILES_FIFO_CHILD", "1");
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(),
                {"--gtest_filter=PreviewService.SpecialFilesAndReplacementCannotBlockWorkerOrShutdown"});
    ASSERT_TRUE(child.waitForStarted());
    const bool finished = child.waitForFinished(10000);
    if (!finished) {
      child.kill();
      child.waitForFinished();
    }
    ASSERT_TRUE(finished) << "FIFO preview or shutdown blocked";
    EXPECT_EQ(child.exitCode(), 0) << child.readAllStandardOutput().toStdString();
    return;
  }
  QTemporaryDir dir(fixturePattern("fifo"));
  ASSERT_TRUE(dir.isValid());
  const auto fifo = dir.filePath("pipe.txt");
  ASSERT_EQ(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
  PreviewService service;
  service.setTarget(fifo, false, 0, {}, S_IFIFO | 0600, false, {});
  EXPECT_FALSE(service.busy());
  EXPECT_FALSE(service.hasText());
  const auto normal = writeSmallText(dir);
  setTargetFromFile(service, normal);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.hasText());
  const auto replaced = writeSmallText(dir, "replaced.txt");
  PreviewServiceTestAccess::beforeDispatch(service, [&] {
    QFile::remove(replaced);
    ::mkfifo(QFile::encodeName(replaced).constData(), 0600);
  });
  setTargetFromFile(service, replaced);
  ASSERT_TRUE(settled(service));
  EXPECT_FALSE(service.hasText());
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::Unsupported);
  PreviewServiceTestAccess::beforeDispatch(service, {});
  setTargetFromFile(service, normal);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.hasText());
  const auto imagePath = writeJpegWithExif(dir);
  PreviewServiceTestAccess::beforeFullDecode(service, [&] {
    QFile::remove(imagePath);
    ::mkfifo(QFile::encodeName(imagePath).constData(), 0600);
  });
  setTargetFromFile(service, imagePath);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.hasImage());
  EXPECT_TRUE(service.exifPresent());  // full decode and EXIF use the original descriptor
  QSignalSpy stopped(&service, &PreviewService::shutdownFinished);
  service.shutdown();
  ASSERT_TRUE(stopped.wait(2000));
}

TEST(PreviewService, FullResolutionCacheEnforcesEntryAndByteLimitsAndSourceRevision) {
  QTemporaryDir dir(fixturePattern("lru-budget"));
  const auto first = writeJpegWithExif(dir, "first.jpg");
  const auto second = writeJpegWithExif(dir, "second.jpg");
  const auto third = writeJpegWithExif(dir, "third.jpg");
  PreviewService service;
  std::atomic_int decodes = 0;
  PreviewServiceTestAccess::beforeFullDecode(service, [&] { ++decodes; });
  const auto preview = [&](const QString& path) {
    setTargetFromFile(service, path);
    EXPECT_TRUE(settled(service));
  };
  preview(first);
  preview(second);
  preview(third);
  preview(first);
  EXPECT_EQ(decodes.load(), 4);  // third entry evicts first
  service.clear();
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {4000, 4000});
  preview(first);
  preview(second);
  preview(first);
  EXPECT_EQ(decodes.load(), 7);  // each ~48MiB: two cannot both fit in 64MiB
  const auto oldMtime = QFileInfo(first).lastModified();
  const auto oldSize = QFileInfo(first).size();
  preview(second);
  QFile replacement(first);
  ASSERT_TRUE(replacement.open(QIODevice::ReadWrite));
  ASSERT_TRUE(replacement.seek(oldSize - 1));
  replacement.write("\0", 1);
  ASSERT_TRUE(replacement.setFileTime(oldMtime, QFileDevice::FileModificationTime));
  replacement.close();
  service.clear();
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {1024, 1024});
  preview(first);
  EXPECT_EQ(decodes.load(), 9);
  service.clear();
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {5000, 5000});
  preview(first);
  EXPECT_GT(service.image().sizeInBytes(), 64LL * 1024 * 1024);
  preview(writeSmallText(dir));
  preview(first);
  EXPECT_EQ(decodes.load(), 11);  // images larger than the budget display without retention
}

TEST(PreviewService, SameSizeAndMtimeReplacementInvalidatesBothCacheTiers) {
  QTemporaryDir dir(fixturePattern("cache-revision"));
  const auto path = dir.filePath("revision.bmp");
  QImage image(64, 48, QImage::Format_RGB32);
  image.fill(Qt::red);
  ASSERT_TRUE(image.save(path));
  const auto oldMtime = QFileInfo(path).lastModified();
  const auto oldSize = QFileInfo(path).size();
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.image().pixelColor(0, 0), QColor(Qt::red));
  setTargetFromFile(service, writeSmallText(dir));
  ASSERT_TRUE(settled(service));
  image.fill(Qt::blue);
  ASSERT_TRUE(image.save(path));
  QFile changed(path);
  ASSERT_TRUE(changed.open(QIODevice::ReadWrite));
  ASSERT_TRUE(changed.setFileTime(oldMtime, QFileDevice::FileModificationTime));
  changed.close();
  ASSERT_EQ(QFileInfo(path).size(), oldSize);
  bool sawThumbnail = false;
  QObject::connect(&service, &PreviewService::changed, &service, [&] {
    if (service.hasImage()) {
      sawThumbnail = sawThumbnail || service.busy();
      EXPECT_EQ(service.image().pixelColor(0, 0), QColor(Qt::blue));
    }
  });
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(sawThumbnail);
}

TEST(PreviewService, RegularSymlinkAndPermissionDeniedRecovery) {
  QTemporaryDir dir(fixturePattern("symlink-permission"));
  const auto path = writeSmallText(dir);
  const auto link = dir.filePath("link.txt");
  ASSERT_TRUE(QFile::link(path, link));
  PreviewService service;
  setTargetFromFile(service, link);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.hasText());
  if (::geteuid() == 0) {
    GTEST_SKIP() << "Permission denial requires non-root";
  }
  ASSERT_TRUE(QFile::setPermissions(path, {}));
  service.setTarget(path, false, QFileInfo(path).size(), QFileInfo(path).lastModified(), S_IFREG, false, {});
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::PermissionDenied);
  ASSERT_TRUE(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.hasText());
}
