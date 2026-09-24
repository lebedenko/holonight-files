#include "preview_service.h"

#include "directory_fixtures.h"
#include "preview_fixtures.h"
#include "preview_service_test_access.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

#include <gtest/gtest.h>
#include <iostream>
#include <sys/stat.h>
#include <unistd.h>

using files_test::fixturePattern;
using files_test::writeBytes;
using files_test::writeFile;
using files_test::writeJpegWithExif;
using files_test::writeJpegWithExifBlob;
using files_test::writeJpegWithoutExif;
using files_test::writeLargeText;
using files_test::writeNumberedLines;
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
  // Directories never reach the worker, where the description is resolved (DESIGN.md §5.2).
  EXPECT_TRUE(service.mimeTypeDescription().isEmpty());
  EXPECT_FALSE(service.busy());
}

TEST(PreviewService, MimeTypeDescriptionIsPopulatedAfterAnImageDecodeCompletes) {
  QTemporaryDir dir(fixturePattern("preview-mime-description"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeJpegWithExif(dir);
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.mimeType(), QStringLiteral("image/jpeg"));
  EXPECT_FALSE(service.mimeTypeDescription().isEmpty());
  EXPECT_NE(service.mimeTypeDescription(), service.mimeType());
}

TEST(PreviewService, MimeTypeDescriptionIsTheEnglishCommentUnderAPinnedLocale) {
  QTemporaryDir dir(fixturePattern("preview-mime-description-en"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeJpegWithExif(dir);
  const auto previous = QLocale();
  QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
  const auto restore = qScopeGuard([&] { QLocale::setDefault(previous); });
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.mimeTypeDescription(), QStringLiteral("JPEG image"));
}

TEST(PreviewService, MimeTypeDescriptionIsNonEmptyUnderAFrenchLocale) {
  QTemporaryDir dir(fixturePattern("preview-mime-description-fr"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeJpegWithExif(dir);
  const auto previous = QLocale();
  QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
  const auto restore = qScopeGuard([&] { QLocale::setDefault(previous); });
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  // Only non-emptiness: the translated string depends on the installed shared-mime-info.
  EXPECT_FALSE(service.mimeTypeDescription().isEmpty());
}

TEST(PreviewService, ExifLensModelAndApertureSurfaceFromTheExtendedSummary) {
  QTemporaryDir dir(fixturePattern("preview-exif-lens"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeJpegWithExif(dir);
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.exifPresent());
  EXPECT_EQ(service.exifLensModel(), QStringLiteral("HN 24-70mm F2.8"));
  EXPECT_EQ(service.exifAperture(), QStringLiteral("f/8.0"));
}

TEST(PreviewService, ExifLensModelAndApertureStayEmptyWhenTheTagsAreAbsent) {
  QTemporaryDir dir(fixturePattern("preview-exif-no-lens"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeJpegWithExifBlob(dir, "no-lens.jpg", files_test::buildSampleExifBlobWithoutLens());
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.exifPresent());
  EXPECT_TRUE(service.exifLensModel().isEmpty());
  EXPECT_TRUE(service.exifAperture().isEmpty());
}

TEST(PreviewService, ExifLensModelAndApertureClearOnRetarget) {
  QTemporaryDir dir(fixturePattern("preview-exif-lens-clear"));
  ASSERT_TRUE(dir.isValid());
  const auto image = writeJpegWithExif(dir);
  const auto text = writeSmallText(dir);
  PreviewService service;
  setTargetFromFile(service, image);
  ASSERT_TRUE(settled(service));
  ASSERT_FALSE(service.exifLensModel().isEmpty());
  setTargetFromFile(service, text);
  EXPECT_TRUE(service.exifLensModel().isEmpty());
  EXPECT_TRUE(service.exifAperture().isEmpty());
  EXPECT_TRUE(service.mimeTypeDescription().isEmpty());
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.exifLensModel().isEmpty());
  EXPECT_TRUE(service.exifAperture().isEmpty());
}

TEST(PreviewService, StatFailedTargetIsReportedAsAnErrorWithoutWorkerDispatch) {
  PreviewService service;
  service.setTarget(QStringLiteral("/nonexistent/broken-link"), false, -1, {}, 0, true,
                    QStringLiteral("Broken symbolic link"));
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::BrokenSymlink);
  EXPECT_FALSE(service.busy());
}

TEST(PreviewService, IconNameIsTheListingChainVerbatimAndClearsWithTheTarget) {
  QTemporaryDir dir(fixturePattern("preview-icon-name"));
  ASSERT_TRUE(dir.isValid());
  // Content is plain text, but the pane must show the listing's extension-derived icon (REQ-F-015).
  const auto path = writeFile(dir, "script.py", "hello\n");
  ASSERT_FALSE(path.isEmpty());
  const QFileInfo info(path);
  PreviewService service;
  QSignalSpy changed(&service, &PreviewService::changed);
  service.setTarget(path, false, info.size(), info.lastModified(), S_IFREG | 0644, false, {},
                    QStringLiteral("text-x-python/text-x-generic/application-x-generic"));
  EXPECT_EQ(service.iconName(), QStringLiteral("text-x-python/text-x-generic/application-x-generic"));
  EXPECT_GE(changed.count(), 1);
  ASSERT_TRUE(settled(service));

  const auto otherPath = writeFile(dir, "photo.jpg", "not really a jpeg");
  ASSERT_FALSE(otherPath.isEmpty());
  service.setTarget(otherPath, false, 17, QFileInfo(otherPath).lastModified(), S_IFREG | 0644, false, {},
                    QStringLiteral("image-jpeg/image-x-generic/application-x-generic"));
  EXPECT_EQ(service.iconName(), QStringLiteral("image-jpeg/image-x-generic/application-x-generic"));
  ASSERT_TRUE(settled(service));

  changed.clear();
  service.clear();
  EXPECT_TRUE(service.iconName().isEmpty());
  EXPECT_EQ(changed.count(), 1);
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

TEST(PreviewService, NoImageAppearsUntilAdequateDecodeCompletes) {
  QTemporaryDir dir(fixturePattern("progressive"));
  const auto path = writeFile(
      dir, "large.jpg",
      files_test::spliceJpegExif(files_test::renderJpegBytes({2000, 1500}), files_test::buildSampleExifBlob()));
  PreviewService service;
  PreviewServiceTestAccess::beforeFullDecode(service, [] { QThread::msleep(400); });
  setTargetFromFile(service, path);
  QTest::qWait(150);
  EXPECT_FALSE(service.hasImage());
  EXPECT_TRUE(service.busy());
  qint64 imageKey = 0;
  QObject::connect(&service, &PreviewService::changed, &service, [&] {
    if (service.hasImage()) {
      EXPECT_EQ(service.image().size(), QSize(1024, 768));
      if (imageKey == 0) {
        imageKey = service.image().cacheKey();
      }
      EXPECT_EQ(service.image().cacheKey(), imageKey);
    }
  });
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.exifPresent());
}

TEST(PreviewService, TimeoutDuringAdequateDecodeNeverPublishesPixels) {
  QTemporaryDir dir(fixturePattern("progressive-timeout"));
  const auto path = writeJpegWithExif(dir);
  PreviewService service;
  PreviewServiceTestAccess::beforeFullDecode(service, [] { QThread::msleep(3400); });
  setTargetFromFile(service, path);
  QTest::qWait(150);
  EXPECT_FALSE(service.hasImage());
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
  const auto path = writeFile(dir, "large.jpg", files_test::renderJpegBytes({2000, 1500}));
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
  EXPECT_EQ(service.image().width(), 512);
  service.setQuickLookActive(true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return service.image().width() == 1024; }));
  service.setQuickLookActive(false);
  service.setRequestedSize(PreviewService::PreviewConsumer::QuickLook, {900, 900});
  QTest::qWait(200);
  EXPECT_EQ(service.image().width(), 1024);
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
  const auto first = writeFile(dir, "first.jpg", files_test::renderJpegBytes({6000, 4500}));
  const auto second = writeFile(dir, "second.jpg", files_test::renderJpegBytes({6000, 4500}));
  const auto third = writeFile(dir, "third.jpg", files_test::renderJpegBytes({6000, 4500}));
  PreviewService service;
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {1200, 1200});
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

class PreviewOrientation : public testing::TestWithParam<int> {};

TEST_P(PreviewOrientation, WarmDiskAndMemoryReuseAvoidSourceDecodeAndUpgradeRetainsPixels) {
  QTemporaryDir cacheHome(fixturePattern("orientation-preview-cache"));
  const auto previousCache = qgetenv("XDG_CACHE_HOME");
  const auto restoreCache = qScopeGuard([&] {
    if (previousCache.isNull()) {
      qunsetenv("XDG_CACHE_HOME");
    } else {
      qputenv("XDG_CACHE_HOME", previousCache);
    }
  });
  qputenv("XDG_CACHE_HOME", cacheHome.path().toLocal8Bit());
  QTemporaryDir dir(fixturePattern("warm-tier"));
  const auto path = writeFile(dir, "image.jpg", files_test::orientationJpeg({2400, 1600}, GetParam()));
  const QSize oriented = GetParam() >= 5 ? QSize(1600, 2400) : QSize(2400, 1600);
  std::atomic_int decodes = 0;
  const auto start = [&](PreviewService& service) {
    PreviewServiceTestAccess::beforeFullDecode(service, [&] { ++decodes; });
    service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {300, 200});
    QElapsedTimer timer;
    timer.start();
    QObject::connect(&service, &PreviewService::changed, &service, [&service, oriented] {
      if (service.hasImage()) {
        EXPECT_EQ(service.sourcePixelSize(), oriented);
      }
    });
    setTargetFromFile(service, path);
    EXPECT_TRUE(settled(service));
    EXPECT_EQ(service.sourcePixelSize(), oriented);
    std::cout << "Preview latency ms: " << timer.elapsed() << "; source decodes: " << decodes.load() << '\n';
  };
  {
    PreviewService cold;
    start(cold);
  }
  EXPECT_EQ(decodes.load(), 1);
  PreviewService service;
  start(service);  // A fresh service has no memory entries.
  EXPECT_EQ(decodes.load(), 1);
  const auto retained = service.image().cacheKey();
  service.clear();
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {200, 100});
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.image().cacheKey(), retained);
  PreviewServiceTestAccess::beforeFullDecode(service, [&] {
    ++decodes;
    QThread::msleep(300);
  });
  service.setRequestedSize(PreviewService::PreviewConsumer::QuickLook, {1500, 1800});
  service.setQuickLookActive(true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return decodes.load() == 2; }));
  EXPECT_EQ(service.image().cacheKey(), retained);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.image().size(), (GetParam() >= 5 ? QSize(1200, 1800) : QSize(1500, 1000)));
  service.setQuickLookActive(false);
  QTest::qWait(200);
  EXPECT_EQ(service.image().size(), (GetParam() >= 5 ? QSize(1200, 1800) : QSize(1500, 1000)));
}

INSTANTIATE_TEST_SUITE_P(ExifValues, PreviewOrientation, testing::Values(2, 6, 7));

namespace {
bool canReadDespiteNoPermissions(const QString& path) {
  QFile probe(path);
  return probe.open(QIODevice::ReadOnly);
}
}  // namespace

TEST(PreviewService, CurrentLineStartsAtTheFirstLineOnceTextLoads) {
  QTemporaryDir dir(fixturePattern("preview-line-init"));
  ASSERT_TRUE(dir.isValid());
  PreviewService service;
  EXPECT_EQ(service.currentLineIndex(), -1);
  setTargetFromFile(service, writeNumberedLines(dir, "a.txt", 5));
  EXPECT_EQ(service.currentLineIndex(), -1);  // nothing loaded yet
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.textLineCount(), 5);
  EXPECT_EQ(service.currentLineIndex(), 0);
}

TEST(PreviewService, MoveCurrentLineClampsAtBothEnds) {
  QTemporaryDir dir(fixturePattern("preview-line-clamp"));
  ASSERT_TRUE(dir.isValid());
  PreviewService service;
  setTargetFromFile(service, writeNumberedLines(dir, "a.txt", 5));
  ASSERT_TRUE(settled(service));
  service.moveCurrentLineUp();
  EXPECT_EQ(service.currentLineIndex(), 0);
  for (int i = 0; i < 4; ++i) {
    service.moveCurrentLineDown();
    EXPECT_EQ(service.currentLineIndex(), i + 1);
  }
  for (int i = 0; i < 5; ++i) {
    service.moveCurrentLineDown();
  }
  EXPECT_EQ(service.currentLineIndex(), 4);
  for (int i = 0; i < 10; ++i) {
    service.moveCurrentLine(-1);
  }
  EXPECT_EQ(service.currentLineIndex(), 0);
}

TEST(PreviewService, CurrentLineSignalFiresOncePerActualChange) {
  QTemporaryDir dir(fixturePattern("preview-line-signal"));
  ASSERT_TRUE(dir.isValid());
  PreviewService service;
  setTargetFromFile(service, writeNumberedLines(dir, "a.txt", 3));
  ASSERT_TRUE(settled(service));
  QSignalSpy spy(&service, &PreviewService::currentLineIndexChanged);
  service.moveCurrentLineUp();  // already first: no change
  EXPECT_EQ(spy.count(), 0);
  service.moveCurrentLineDown();
  EXPECT_EQ(spy.count(), 1);
  service.moveCurrentLineDown();
  EXPECT_EQ(spy.count(), 2);
  service.moveCurrentLineDown();  // already last: no change
  EXPECT_EQ(spy.count(), 2);
  service.moveCurrentLine(0);
  EXPECT_EQ(spy.count(), 2);
}

TEST(PreviewService, EmptyFileShowsOneEmptyLineThatCannotMove) {
  QTemporaryDir dir(fixturePattern("preview-line-empty"));
  ASSERT_TRUE(dir.isValid());
  PreviewService service;
  setTargetFromFile(service, files_test::writeEmptyText(dir));
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.textLineCount(), 1);
  EXPECT_EQ(service.currentLineIndex(), 0);
  QSignalSpy spy(&service, &PreviewService::currentLineIndexChanged);
  service.moveCurrentLineDown();
  service.moveCurrentLineUp();
  EXPECT_EQ(service.currentLineIndex(), 0);
  EXPECT_EQ(spy.count(), 0);
}

TEST(PreviewService, NoLinesMeansMinusOneAndMovesAreIgnored) {
  QTemporaryDir dir(fixturePattern("preview-line-none"));
  ASSERT_TRUE(dir.isValid());
  PreviewService service;
  setTargetFromFile(service, writeNumberedLines(dir, "a.txt", 3));
  ASSERT_TRUE(settled(service));
  service.moveCurrentLineDown();
  ASSERT_EQ(service.currentLineIndex(), 1);
  QSignalSpy spy(&service, &PreviewService::currentLineIndexChanged);
  setTargetFromFile(service, dir.path());  // a directory has no text
  EXPECT_EQ(service.currentLineIndex(), -1);
  EXPECT_EQ(service.textLineCount(), 0);
  EXPECT_EQ(spy.count(), 1);
  service.moveCurrentLineDown();
  service.moveCurrentLineUp();
  EXPECT_EQ(service.currentLineIndex(), -1);
  EXPECT_EQ(spy.count(), 1);
  service.clear();
  EXPECT_EQ(service.currentLineIndex(), -1);
}

TEST(PreviewService, UnreadableTextFileHasNoLinesButStaysEligible) {
  QTemporaryDir dir(fixturePattern("preview-line-denied"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeBytes(dir, "denied.txt", "secret");
  ASSERT_TRUE(QFile::setPermissions(path, QFileDevice::Permissions()));
  if (canReadDespiteNoPermissions(path)) {
    GTEST_SKIP() << "running with privileges that bypass file permissions";
  }
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::PermissionDenied);
  EXPECT_EQ(service.currentLineIndex(), -1);
  EXPECT_TRUE(service.quickLookEligible());  // REQ-F-021: the overlay opens and shows the error
  service.moveCurrentLineDown();
  EXPECT_EQ(service.currentLineIndex(), -1);
}

TEST(PreviewService, OpeningQuickLookResetsTheCurrentLineToTheFirst) {
  QTemporaryDir dir(fixturePattern("preview-line-open"));
  ASSERT_TRUE(dir.isValid());
  PreviewService service;
  setTargetFromFile(service, writeNumberedLines(dir, "a.txt", 6));
  ASSERT_TRUE(settled(service));
  service.moveCurrentLine(3);
  ASSERT_EQ(service.currentLineIndex(), 3);
  QSignalSpy spy(&service, &PreviewService::currentLineIndexChanged);
  service.setQuickLookActive(true);
  EXPECT_EQ(service.currentLineIndex(), 0);
  EXPECT_EQ(spy.count(), 1);
  service.moveCurrentLine(2);
  service.setQuickLookActive(true);  // already active: not an open transition
  EXPECT_EQ(service.currentLineIndex(), 2);
  service.setQuickLookActive(false);
  EXPECT_EQ(service.currentLineIndex(), 2);
  service.setQuickLookActive(true);
  EXPECT_EQ(service.currentLineIndex(), 0);
}

TEST(PreviewService, SamePathReloadKeepsAndClampsTheCurrentLineButANewPathResetsIt) {
  QTemporaryDir dir(fixturePattern("preview-line-reload"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeNumberedLines(dir, "a.txt", 10);
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  service.moveCurrentLine(7);
  ASSERT_EQ(service.currentLineIndex(), 7);

  writeNumberedLines(dir, "a.txt", 12);  // external edit: same path, new size
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.textLineCount(), 12);
  EXPECT_EQ(service.currentLineIndex(), 7);

  writeNumberedLines(dir, "a.txt", 4);  // shrinks below the retained line
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.currentLineIndex(), 3);

  setTargetFromFile(service, writeNumberedLines(dir, "b.txt", 9));
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.currentLineIndex(), 0);
}

TEST(PreviewService, QuickLookEligibilityFollowsTheMimeGate) {
  QTemporaryDir dir(fixturePattern("preview-gate"));
  ASSERT_TRUE(dir.isValid());
  PreviewService service;
  EXPECT_FALSE(service.quickLookEligible());  // no entry

  const auto eligible = [&](const QString& path) {
    setTargetFromFile(service, path);
    EXPECT_TRUE(settled(service));
    return service.quickLookEligible();
  };
  const auto text = writeNumberedLines(dir, "a.txt", 2);
  setTargetFromFile(service, text);
  EXPECT_FALSE(service.quickLookEligible());  // MIME not resolved yet
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.quickLookEligible());

  EXPECT_TRUE(eligible(writeJpegWithoutExif(dir)));
  EXPECT_TRUE(eligible(writeBytes(dir, "empty-no-extension", {})));  // application/x-zerosize
  EXPECT_FALSE(eligible(writeBytes(dir, "data.json", "{\"a\": 1}\n")));
  EXPECT_FALSE(eligible(writeBytes(dir, "notes.md", "# title\n")));
  EXPECT_FALSE(eligible(writeBytes(dir, "run.sh", "#!/bin/sh\necho hi\n")));
  EXPECT_FALSE(eligible(dir.path()));  // directory
  service.clear();
  EXPECT_FALSE(service.quickLookEligible());
}

TEST(PreviewService, StatFailedEntryIsNotQuickLookEligible) {
  PreviewService service;
  service.setTarget(QStringLiteral("/nonexistent/x.txt"), false, -1, {}, 0120777, true,
                    QStringLiteral("Broken symbolic link"));
  EXPECT_FALSE(service.quickLookEligible());
}

TEST(PreviewService, LoadingTextNeverBlocksTheUiThread) {
  QTemporaryDir dir(fixturePattern("preview-line-offthread"));
  ASSERT_TRUE(dir.isValid());
  PreviewService service;
  // The hook runs on the worker: the UI thread must keep dispatching timer events while it blocks.
  PreviewServiceTestAccess::beforeDispatch(service, [] { QThread::msleep(400); });
  int ticks = 0;
  QTimer timer;
  timer.setInterval(10);
  QObject::connect(&timer, &QTimer::timeout, [&ticks] { ++ticks; });
  timer.start();
  setTargetFromFile(service, writeNumberedLines(dir, "a.txt", 3));
  ASSERT_TRUE(QTest::qWaitFor([&] { return ticks >= 5; }, 2000));
  EXPECT_TRUE(service.busy());  // the load is still pending while the UI thread stayed responsive
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.textLineCount(), 3);
}

// A fitted integer size is not a safe bound for a second aspect-ratio fit.
// 2400x1600 -> 1100x733 -> 1099x733 used to miss adequacy and decode repeatedly.
class PreviewRoundedBounds : public testing::TestWithParam<int> {};

TEST_P(PreviewRoundedBounds, FullDecodeFitsOriginalBoundsOnceAndReusesAdequatePixels) {
  QTemporaryDir dir(fixturePattern("rounded-preview"));
  const bool png = GetParam() == 0;
  const auto bytes =
      png ? files_test::renderPngBytes({2400, 1600}) : files_test::orientationJpeg({2400, 1600}, GetParam());
  const auto path = writeFile(dir, png ? "image.png" : "image.jpg", bytes);
  const QSize source = GetParam() >= 5 ? QSize(1600, 2400) : QSize(2400, 1600);
  std::atomic_int attempts = 0;
  PreviewService service;
  PreviewServiceTestAccess::beforeFullDecode(service, [&] { ++attempts; });
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {1100, 1100});
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  ASSERT_EQ(service.sourcePixelSize(), source);
  ASSERT_EQ(service.image().size(), source.scaled({1100, 1100}, Qt::KeepAspectRatio));
  ASSERT_EQ(attempts.load(), 1);
  const auto retained = service.image().cacheKey();
  service.clear();
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.image().cacheKey(), retained);
  EXPECT_EQ(attempts.load(), 1);
}

INSTANTIATE_TEST_SUITE_P(PngAndExif, PreviewRoundedBounds, testing::Values(0, 2, 6, 7));

namespace {
class PreviewThumbnailCancellation : public testing::TestWithParam<std::tuple<ThumbnailService::Stage, int>> {};
}  // namespace

TEST_P(PreviewThumbnailCancellation, StopsObsoleteWorkAndCompletesNewestRequest) {
  QTemporaryDir dir(fixturePattern("preview-cancellation"));
  QTemporaryDir cacheHome(fixturePattern("preview-cancellation-cache"));
  const auto previous = qgetenv("XDG_CACHE_HOME");
  qputenv("XDG_CACHE_HOME", cacheHome.path().toUtf8());
  const auto restoreCache = qScopeGuard([&] {
    if (previous.isNull()) {
      qunsetenv("XDG_CACHE_HOME");
    } else {
      qputenv("XDG_CACHE_HOME", previous);
    }
  });
  const auto path = writeFile(dir, "obsolete.jpg", files_test::renderJpegBytes({1200, 600}));
  const auto latest = writeFile(dir, "latest.jpg", files_test::renderJpegBytes({800, 400}));
  const auto [boundary, action] = GetParam();
  QSemaphore entered;
  QSemaphore release;
  std::atomic_int laterStages = 0;
  std::atomic_bool reached = false;
  std::atomic_bool waitExpired = false;
  PreviewService service;
  // Establish MIME for the real resize debounce path, then request a larger uncached tier.
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {64, 64});
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  PreviewServiceTestAccess::thumbnailStage(service, [&](ThumbnailService::Stage stage) {
    if (reached.load()) {
      ++laterStages;
    } else if (stage == boundary) {
      reached = true;
      entered.release();
      waitExpired = !release.tryAcquire(1, 5000);
    }
  });
  // Release before service destruction even when an ASSERT aborts the test.
  const auto unblock = qScopeGuard([&] { release.release(); });
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {256, 256});
  ASSERT_TRUE(QTest::qWaitFor([&] { return entered.tryAcquire(); }, 2000));
  PreviewServiceTestAccess::thumbnailStage(service, {});  // next job gets a fresh callback snapshot
  QSignalSpy stopped(&service, &PreviewService::shutdownFinished);
  int obsoletePublications = 0;
  const auto observer = QObject::connect(&service, &PreviewService::changed, &service, [&] {
    if (service.image().width() == 256 || service.previewErrorKind() != PreviewService::PreviewErrorKind::None) {
      ++obsoletePublications;
    }
  });
  if (action == 0) {
    service.clear();
  } else if (action == 1) {
    for (int i = 0; i < 8; ++i) {
      setTargetFromFile(service, i % 2 == 0 ? latest : path);
    }
    service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {512, 512});
    setTargetFromFile(service, latest);
  } else if (action == 2) {
    service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {512, 512});
    ASSERT_TRUE(QTest::qWaitFor([&] { return !PreviewServiceTestAccess::resizePending(service); }, 1000));
  } else {
    service.shutdown();
  }
  release.release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !PreviewServiceTestAccess::activeJob(service); }, 5000));
  EXPECT_FALSE(waitExpired.load());
  EXPECT_EQ(laterStages.load(), 0);
  EXPECT_EQ(obsoletePublications, 0);
  QObject::disconnect(observer);
  EXPECT_TRUE(QDir(cacheHome.path() + "/thumbnails/large").entryList({"*.png"}, QDir::Files).isEmpty());
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::None);
  if (action == 1 || action == 2) {
    ASSERT_TRUE(settled(service));
    EXPECT_TRUE(service.hasImage());
    EXPECT_EQ(service.name(), action == 1 ? "latest.jpg" : "obsolete.jpg");
    EXPECT_GE(service.image().width(), action == 1 ? 256 : 512);
  } else if (action == 0) {
    EXPECT_FALSE(service.hasImage());
    // Revisiting must decode: cancelled work must not have entered the memory cache.
    std::atomic_int decodes = 0;
    PreviewServiceTestAccess::beforeFullDecode(service, [&] { ++decodes; });
    setTargetFromFile(service, path);
    ASSERT_TRUE(settled(service));
    EXPECT_EQ(decodes.load(), 1);
  } else {
    ASSERT_TRUE(QTest::qWaitFor([&] { return !stopped.isEmpty(); }, 2000));
    EXPECT_EQ(service.image().width(), 128);  // only the original successful preview remains
  }
}

INSTANTIATE_TEST_SUITE_P(ThumbnailBoundaries, PreviewThumbnailCancellation,
                         testing::Combine(testing::Values(ThumbnailService::Stage::OriginalDecoded,
                                                          ThumbnailService::Stage::BeforeCommit),
                                          testing::Values(0, 1, 2, 3)));

TEST(PreviewService, RasterOutcomesHaveDistinctPresentationAndSilentCancellation) {
  using HolonightImages::Outcome;
  using Kind = PreviewService::PreviewErrorKind;
  const std::array cases = {
      std::pair{Outcome::Success, Kind::None},         std::pair{Outcome::Unsupported, Kind::Unsupported},
      std::pair{Outcome::Damaged, Kind::DecodeFailed}, std::pair{Outcome::ResourceLimit, Kind::ResourceLimit},
      std::pair{Outcome::IoFailure, Kind::IoFailure},  std::pair{Outcome::Cancelled, Kind::None}};
  QStringList messages;
  for (const auto& [outcome, kind] : cases) {
    const auto error = PreviewServiceTestAccess::rasterError(outcome);
    EXPECT_EQ(error.kind, kind);
    EXPECT_EQ(error.message.isEmpty(), kind == Kind::None);
    if (kind != Kind::None) {
      EXPECT_FALSE(messages.contains(error.message));
      messages.append(error.message);
    }
  }
}

TEST(PreviewService, MetadataLimitDoesNotFailPixelsAndSelectionResetsStatus) {
  QTemporaryDir dir(fixturePattern("preview-metadata-outcomes"));
  const auto bytes = files_test::splicePngExifChunk(files_test::renderPngBytes(),
                                                    QByteArray("Exif\0\0", 6) + QByteArray(2 * 1024 * 1024, 'x'));
  const auto path = files_test::writeFile(dir, "limited.png", bytes);
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.hasImage());
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::None);
  EXPECT_EQ(PreviewServiceTestAccess::exif(service).outcome, HolonightImages::Outcome::ResourceLimit);
  service.clear();
  EXPECT_FALSE(PreviewServiceTestAccess::exif(service).outcome);
}

namespace {
QByteArray smallSvg(const QByteArray& body = "<rect width='12' height='12' fill='red'/>") {
  return "<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24'>" + body + "</svg>";
}
}  // namespace
TEST(PreviewService, SvgEnlargesForPaneAndQuickLookAndStopsRedispatching) {
  QTemporaryDir dir(fixturePattern("svg-preview"));
  const auto path = writeFile(dir, "small.svg", smallSvg());
  PreviewService service;
  std::atomic_int starts = 0;
  PreviewServiceTestAccess::beforeDispatch(service, [&] { ++starts; });
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {300, 300});
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.vectorImage());
  EXPECT_EQ(service.documentSize(), QSizeF(24, 24));
  EXPECT_EQ(service.image().size(), QSize(512, 512));
  EXPECT_EQ(service.image().pixelColor(20, 20), QColor(Qt::red));
  EXPECT_EQ(service.image().pixelColor(400, 400).alpha(), 0);
  EXPECT_FALSE(service.exifPresent());
  EXPECT_TRUE(service.quickLookEligible());
  service.setRequestedSize(PreviewService::PreviewConsumer::QuickLook, {1401, 1401});
  service.setQuickLookActive(true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return service.image().width() == 1401; }));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !PreviewServiceTestAccess::resizePending(service) && !service.busy(); }));
  EXPECT_EQ(starts.load(), 2);
}
TEST(PreviewService, SvgFractionalGeometryUsesOriginalPixelBoundExactlyOnce) {
  QTemporaryDir dir(fixturePattern("svg-fractional"));
  const auto path =
      writeFile(dir, "fraction.svg", "<svg xmlns='http://www.w3.org/2000/svg' width='7.5' height='3.5'/>");
  PreviewService service;
  std::atomic_int starts = 0;
  PreviewServiceTestAccess::beforeDispatch(service, [&] { ++starts; });
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {1375, 1375});
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_EQ(service.documentSize(), QSizeF(7.5, 3.5));
  EXPECT_EQ(service.image().size(), QSize(1375, 642));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !PreviewServiceTestAccess::resizePending(service) && !service.busy(); }));
  EXPECT_EQ(starts.load(), 1);
}
TEST(PreviewService, SvgDispatchUsesMimeOrSuffixAndValidatesContent) {
  QTemporaryDir dir(fixturePattern("svg-dispatch"));
  PreviewService service;
  const auto extensionless = writeFile(dir, "vector", smallSvg());
  setTargetFromFile(service, extensionless);
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.vectorImage());
  EXPECT_TRUE(service.hasImage());
  const auto invalid = writeFile(dir, "invalid.SVG", "<not-svg/>");
  setTargetFromFile(service, invalid);
  ASSERT_TRUE(settled(service));
  EXPECT_FALSE(service.hasImage());
  EXPECT_FALSE(service.hasText());
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::DecodeFailed);
}
TEST(PreviewService, SvgPolicyValidationPrecedesThumbnailLookup) {
  QTemporaryDir dir(fixturePattern("svg-policy"));
  const auto path = writeFile(dir, "external.svg", smallSvg("<image href='linked.png'/>"));
  QFile source(path);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  const std::atomic_bool running{false};
  ASSERT_EQ(
      ThumbnailService::renderSvg(source, path, {}, smallSvg(), {128, 128}, ThumbnailService::Tier::Normal, running)
          .outcome,
      HolonightImages::Outcome::Success);
  PreviewService service;
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {128, 128});
  std::atomic_int cacheInspections = 0;
  PreviewServiceTestAccess::thumbnailStage(service, [&](ThumbnailService::Stage stage) {
    if (stage == ThumbnailService::Stage::CacheInspect) {
      ++cacheInspections;
    }
  });
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_FALSE(service.hasImage());
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::Unsupported);
  EXPECT_TRUE(service.previewErrorMessage().contains("resource"));
  EXPECT_TRUE(service.quickLookEligible());
  EXPECT_EQ(cacheInspections.load(), 0);
}
TEST(PreviewService, SvgDiskAndMemoryCachesReuseValidatedPixels) {
  QTemporaryDir dir(fixturePattern("svg-reuse"));
  const auto path = writeFile(dir, "small.svg", smallSvg());
  std::atomic_int decodes = 0;
  {
    PreviewService service;
    PreviewServiceTestAccess::beforeFullDecode(service, [&] { ++decodes; });
    service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {200, 200});
    setTargetFromFile(service, path);
    ASSERT_TRUE(settled(service));
    ASSERT_TRUE(service.hasImage());
    service.clear();
    setTargetFromFile(service, path);
    ASSERT_TRUE(settled(service));
    EXPECT_EQ(decodes.load(), 1);
  }
  PreviewService second;
  PreviewServiceTestAccess::beforeFullDecode(second, [&] { ++decodes; });
  second.setRequestedSize(PreviewService::PreviewConsumer::Pane, {200, 200});
  setTargetFromFile(second, path);
  ASSERT_TRUE(settled(second));
  EXPECT_TRUE(second.hasImage());
  EXPECT_EQ(decodes.load(), 1);
}
TEST(PreviewService, SvgRetargetDuringRenderSuppressesStalePixelsAndCacheWrite) {
  QTemporaryDir dir(fixturePattern("svg-stale"));
  const auto path = writeFile(dir, "small.svg", smallSvg());
  const auto text = writeSmallText(dir);
  QSemaphore entered;
  QSemaphore release;
  std::atomic_int commits = 0;
  PreviewService service;
  const auto unblock = qScopeGuard([&] { release.release(); });
  PreviewServiceTestAccess::thumbnailStage(service, [&](ThumbnailService::Stage stage) {
    if (stage == ThumbnailService::Stage::BeforeCommit) {
      ++commits;
    }
    if (stage == ThumbnailService::Stage::OriginalDecoded) {
      entered.release();
      release.acquire();
    }
  });
  setTargetFromFile(service, path);
  ASSERT_TRUE(QTest::qWaitFor([&] { return entered.available() > 0; }));
  setTargetFromFile(service, text);
  release.release();
  ASSERT_TRUE(settled(service));
  EXPECT_TRUE(service.hasText());
  EXPECT_FALSE(service.hasImage());
  EXPECT_FALSE(service.vectorImage());
  EXPECT_EQ(commits.load(), 0);
}

TEST(PreviewService, SvgInputBudgetRejectsOversizedSource) {
  QTemporaryDir dir(fixturePattern("svg-budget"));
  const auto path = dir.filePath("oversized.svg");
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_TRUE(file.resize((10 * 1024 * 1024) + 1));
  file.close();
  PreviewService service;
  setTargetFromFile(service, path);
  ASSERT_TRUE(settled(service));
  EXPECT_FALSE(service.hasImage());
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::ResourceLimit);
}
