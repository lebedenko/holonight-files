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
#include <QtEndian>

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

TEST(ThumbnailService, TierBoundariesAndPhysicalAspectFit) {
  using namespace ThumbnailService;
  for (const auto tier : {Tier::Normal, Tier::Large, Tier::XLarge, Tier::XXLarge}) {
    const auto extent = static_cast<int>(tier);
    EXPECT_EQ(tierForSize({extent, extent}), tier);
    if (extent > 128) {
      EXPECT_EQ(tierForSize({(extent / 2) + 1, 1}), tier);
    }
  }
  EXPECT_FALSE(tierForSize({1025, 1}));
  EXPECT_FALSE(tierForSize({}));
  EXPECT_EQ(requiredSize({64, 48}, {512, 512}), QSize(64, 48));
  for (const double dpr : {1.0, 1.5, 2.0}) {
    const QSize physical(qRound(300 * dpr), qRound(200 * dpr));
    EXPECT_EQ(requiredSize({3000, 2000}, physical), physical);
    EXPECT_EQ(requiredSize({2000, 3000}, physical), QSize(physical.height() * 2 / 3, physical.height()));
  }
}

TEST(ThumbnailService, OnlySelectedTierIsWrittenAndLargerTierIsReused) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("tier-cache"));
  const auto path = files_test::writeFile(dir, "image.jpg", renderJpegBytes({2000, 1000}));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  using namespace ThumbnailService;
  EXPECT_EQ(lookupOrDecode(file, path, "revision", Tier::XLarge, {400, 200}, nullptr).size(), QSize(512, 256));
  const auto root = home.dir.path() + "/thumbnails/";
  EXPECT_EQ(QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList{"x-large"});
  EXPECT_EQ(lookup(file, path, "revision", Tier::Large, {200, 100}).size(), QSize(512, 256));
  EXPECT_TRUE(lookup(file, path, "changed", Tier::Large, {200, 100}).isNull());
  EXPECT_TRUE(lookup(file, path, "revision", Tier::XXLarge, {600, 300}).isNull());
  // Disk lookup remains usable without a readable source descriptor: no original image decode.
  file.close();
  EXPECT_EQ(lookup(file, path, "revision", Tier::Large, {200, 100}).size(), QSize(512, 256));
}

TEST(ThumbnailService, RejectsUndersizedCorruptAndIncorrectMetadata) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("tier-invalid"));
  const auto path = files_test::writeFile(dir, "image.jpg", renderJpegBytes({1000, 500}));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  using namespace ThumbnailService;
  lookupOrDecode(file, path, "revision", Tier::Large, {200, 100}, nullptr);
  const auto cachePath = cachePathFor(home.dir.path(), path).replace("/normal/", "/large/");
  const QImage valid(cachePath);
  ASSERT_FALSE(valid.isNull());
  auto small = valid.scaled(100, 50);
  ASSERT_TRUE(small.save(cachePath));
  EXPECT_TRUE(lookup(file, path, "revision", Tier::Large, {200, 100}).isNull());
  for (const auto& key : {"Thumb::URI", "Thumb::MTime", "Thumb::Size", "Files::Revision"}) {
    auto invalid = valid;
    invalid.setText(QString::fromLatin1(key), "invalid");
    ASSERT_TRUE(invalid.save(cachePath));
    EXPECT_TRUE(lookup(file, path, "revision", Tier::Large, {200, 100}).isNull());
  }
  QFile corrupt(cachePath);
  ASSERT_TRUE(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
  corrupt.write("not a PNG");
  corrupt.close();
  EXPECT_TRUE(lookup(file, path, "revision", Tier::Large, {200, 100}).isNull());
  EXPECT_EQ(lookupOrDecode(file, path, "revision", Tier::Large, {200, 100}, nullptr).size(), QSize(256, 128));
}

TEST(ThumbnailService, CacheWriteFailureDoesNotPreventDecode) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("tier-unwritable"));
  const auto path = files_test::writeFile(dir, "image.jpg", renderJpegBytes({1000, 500}));
  QFile blocker(home.dir.path() + "/thumbnails");
  ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
  blocker.close();
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(ThumbnailService::lookupOrDecode(file, path, "revision", ThumbnailService::Tier::Large, {200, 100}, nullptr)
                .size(),
            QSize(256, 128));
}

TEST(ThumbnailService, MigrationPreservesStoredPixelOrientation) {
  QTemporaryDir dir(fixturePattern("thumbnail-orientation"));
  auto jpeg = renderJpegBytes({120, 60});
  // EXIF orientation 6, a little-endian TIFF with a single SHORT in IFD0.
  const auto exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000600000000000000");
  QByteArray length(2, '\0');
  qToBigEndian(static_cast<quint16>(exif.size() + 2), length.data());
  jpeg.insert(2, QByteArray("\xff\xe1", 2) + length + exif);
  const auto path = files_test::writeFile(dir, "oriented.jpg", jpeg);
  QImageReader control(path);
  control.setAutoTransform(true);
  ASSERT_EQ(control.read().size(), QSize(60, 120));
  QFile source(path);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  EXPECT_EQ(ThumbnailService::decodeScaled(source, {40, 40}, nullptr).size(), QSize(40, 20));
  EXPECT_TRUE(source.isOpen());
}
