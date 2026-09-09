#include "thumbnail_service.h"

#include "directory_fixtures.h"
#include "preview_fixtures.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::renderJpegBytes;
using files_test::writeJpegWithExif;

namespace {
// Redirects $XDG_CACHE_HOME to an isolated QTemporaryDir for the duration of one test, so cache
// reads/writes never touch the developer's real ~/.cache/thumbnails/.
struct FakeCacheHome {
  QTemporaryDir dir{fixturePattern("thumb-cache")};
  QByteArray previous = qgetenv("XDG_CACHE_HOME");
  FakeCacheHome() { qputenv("XDG_CACHE_HOME", dir.path().toLocal8Bit()); }
  FakeCacheHome(const FakeCacheHome&) = delete;
  FakeCacheHome& operator=(const FakeCacheHome&) = delete;
  FakeCacheHome(FakeCacheHome&&) = delete;
  FakeCacheHome& operator=(FakeCacheHome&&) = delete;
  ~FakeCacheHome() {
    if (previous.isNull()) {
      qunsetenv("XDG_CACHE_HOME");
    } else {
      qputenv("XDG_CACHE_HOME", previous);
    }
  }
};

QString cachePathFor(const QString& cacheHome, const QString& sourcePath) {
  const auto uri = QUrl::fromLocalFile(sourcePath).toString(QUrl::FullyEncoded);
  const auto key = QString::fromLatin1(QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex());
  return cacheHome + "/thumbnails/normal/" + key + ".png";
}
}  // namespace

TEST(ThumbnailService, FreshSystemWithNoPriorCacheStillDecodes) {
  FakeCacheHome cacheHome;
  QTemporaryDir sourceDir(fixturePattern("thumb-fresh"));
  ASSERT_TRUE(sourceDir.isValid());
  const auto path = writeJpegWithExif(sourceDir);
  QString error;
  const auto image = ThumbnailService::lookupOrDecode(path, &error);
  EXPECT_FALSE(image.isNull()) << error.toStdString();
}

TEST(ThumbnailService, CacheKeyMatchesFreedesktopMd5OfUri) {
  FakeCacheHome cacheHome;
  QTemporaryDir sourceDir(fixturePattern("thumb-key"));
  ASSERT_TRUE(sourceDir.isValid());
  const auto path = writeJpegWithExif(sourceDir);
  ThumbnailService::lookupOrDecode(path, nullptr);
  const auto expectedCachePath = cachePathFor(cacheHome.dir.path(), path);
  EXPECT_TRUE(QFile::exists(expectedCachePath)) << expectedCachePath.toStdString();
}

TEST(ThumbnailService, ModifiedSourceInvalidatesCachedThumbnail) {
  FakeCacheHome cacheHome;
  QTemporaryDir sourceDir(fixturePattern("thumb-invalidate"));
  ASSERT_TRUE(sourceDir.isValid());
  const auto path = writeJpegWithExif(sourceDir);
  ThumbnailService::lookupOrDecode(path, nullptr);
  const auto cachePath = cachePathFor(cacheHome.dir.path(), path);
  // QImageReader::text()/canRead() only header-peeks and does not reliably surface PNG tEXt
  // chunks (see thumbnail_service.cpp's cacheEntryValid()); a full QImage load does.
  const QImage firstCached(cachePath);
  ASSERT_FALSE(firstCached.isNull());
  const auto firstMtime = firstCached.text(QStringLiteral("Thumb::MTime"));
  ASSERT_FALSE(firstMtime.isEmpty());

  // Rewrite the source with different bytes; QFile::write always refreshes mtime, but sleep a
  // beat to guarantee whole-second mtime resolution actually differs.
  QTest::qWait(1100);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  file.write(renderJpegBytes(QSize(200, 40)));
  file.close();

  ThumbnailService::lookupOrDecode(path, nullptr);
  const QImage secondCached(cachePath);
  ASSERT_FALSE(secondCached.isNull());
  const auto secondMtime = secondCached.text(QStringLiteral("Thumb::MTime"));
  EXPECT_NE(firstMtime, secondMtime);
}

TEST(ThumbnailService, NormalTierIsBoundedTo128PixelsPreservingAspectRatio) {
  FakeCacheHome cacheHome;
  QTemporaryDir sourceDir(fixturePattern("thumb-aspect"));
  ASSERT_TRUE(sourceDir.isValid());
  QImage wide(QSize(400, 100), QImage::Format_RGB32);
  wide.fill(Qt::blue);
  const auto path = sourceDir.filePath("wide.png");
  ASSERT_TRUE(wide.save(path, "PNG"));
  const auto thumb = ThumbnailService::lookupOrDecode(path, nullptr);
  ASSERT_FALSE(thumb.isNull());
  EXPECT_LE(thumb.width(), 128);
  EXPECT_LE(thumb.height(), 128);
  EXPECT_EQ(thumb.width(), 128);  // the wider dimension hits the 128px bound...
  EXPECT_EQ(thumb.height(), 32);  // ...and the narrower one scales down preserving 400:100 = 4:1
}

TEST(ThumbnailService, CacheDirectoryIsCreatedWithOwnerOnlyPermissions) {
  FakeCacheHome cacheHome;
  QTemporaryDir sourceDir(fixturePattern("thumb-perms"));
  ASSERT_TRUE(sourceDir.isValid());
  const auto path = writeJpegWithExif(sourceDir);
  ThumbnailService::lookupOrDecode(path, nullptr);
  const auto normalDir = cacheHome.dir.path() + "/thumbnails/normal";
  ASSERT_TRUE(QDir(normalDir).exists());
  const auto perms = QFile(normalDir).permissions();
  const auto forbidden = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                         QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
  EXPECT_EQ(perms & forbidden, QFileDevice::Permissions{});
}

TEST(ThumbnailService, DecodeScaledProducesFullResolutionTierBoundedToRequestedSize) {
  FakeCacheHome cacheHome;
  QTemporaryDir sourceDir(fixturePattern("thumb-scaled"));
  ASSERT_TRUE(sourceDir.isValid());
  const auto path = writeJpegWithExif(sourceDir, "photo.jpg");  // 64x48 source
  const auto full = ThumbnailService::decodeScaled(path, QSize(512, 512), nullptr);
  ASSERT_FALSE(full.isNull());
  EXPECT_LE(full.width(), 512);
  EXPECT_LE(full.height(), 512);
}
