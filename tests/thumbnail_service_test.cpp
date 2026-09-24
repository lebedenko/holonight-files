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

#include <array>
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
  const auto image = ThumbnailService::lookupOrDecode(path).image;
  EXPECT_FALSE(image.isNull());
}

TEST(ThumbnailService, CacheKeyMatchesFreedesktopMd5OfUri) {
  FakeCacheHome cacheHome;
  QTemporaryDir sourceDir(fixturePattern("thumb-key"));
  ASSERT_TRUE(sourceDir.isValid());
  const auto path = writeJpegWithExif(sourceDir);
  ThumbnailService::lookupOrDecode(path);
  const auto expectedCachePath = cachePathFor(cacheHome.dir.path(), path);
  EXPECT_TRUE(QFile::exists(expectedCachePath)) << expectedCachePath.toStdString();
}

TEST(ThumbnailService, ModifiedSourceInvalidatesCachedThumbnail) {
  FakeCacheHome cacheHome;
  QTemporaryDir sourceDir(fixturePattern("thumb-invalidate"));
  ASSERT_TRUE(sourceDir.isValid());
  const auto path = writeJpegWithExif(sourceDir);
  ThumbnailService::lookupOrDecode(path);
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

  ThumbnailService::lookupOrDecode(path);
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
  const auto thumb = ThumbnailService::lookupOrDecode(path).image;
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
  ThumbnailService::lookupOrDecode(path);
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
  const auto full = ThumbnailService::decodeScaled(path, QSize(512, 512)).image;
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
  const std::atomic_bool cancelled{false};
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  using namespace ThumbnailService;
  EXPECT_EQ(lookupOrDecode(file, path, "revision", Tier::XLarge, {400, 200}, cancelled).image.size(), QSize(512, 256));
  const auto root = home.dir.path() + "/thumbnails/";
  EXPECT_EQ(QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot), QStringList{"x-large"});
  EXPECT_EQ(lookup(file, path, "revision", Tier::Large, {200, 100}, cancelled).value().image.size(), QSize(512, 256));
  EXPECT_FALSE(lookup(file, path, "changed", Tier::Large, {200, 100}, cancelled));
  EXPECT_FALSE(lookup(file, path, "revision", Tier::XXLarge, {600, 300}, cancelled));
  // Disk lookup remains usable without a readable source descriptor: no original image decode.
  file.close();
  EXPECT_EQ(lookup(file, path, "revision", Tier::Large, {200, 100}, cancelled).value().image.size(), QSize(512, 256));
}

TEST(ThumbnailService, RejectsUndersizedCorruptAndIncorrectMetadata) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("tier-invalid"));
  const auto path = files_test::writeFile(dir, "image.jpg", renderJpegBytes({1000, 500}));
  const std::atomic_bool cancelled{false};
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  using namespace ThumbnailService;
  lookupOrDecode(file, path, "revision", Tier::Large, {200, 100}, cancelled);
  const auto cachePath = cachePathFor(home.dir.path(), path).replace("/normal/", "/large/");
  const QImage valid(cachePath);
  ASSERT_FALSE(valid.isNull());
  auto small = valid.scaled(100, 50);
  ASSERT_TRUE(small.save(cachePath));
  EXPECT_FALSE(lookup(file, path, "revision", Tier::Large, {200, 100}, cancelled));
  for (const auto& key : {"Thumb::URI", "Thumb::MTime", "Thumb::Size", "Files::Revision"}) {
    auto invalid = valid;
    invalid.setText(QString::fromLatin1(key), "invalid");
    ASSERT_TRUE(invalid.save(cachePath));
    EXPECT_FALSE(lookup(file, path, "revision", Tier::Large, {200, 100}, cancelled));
  }
  QFile corrupt(cachePath);
  ASSERT_TRUE(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate));
  corrupt.write("not a PNG");
  corrupt.close();
  EXPECT_FALSE(lookup(file, path, "revision", Tier::Large, {200, 100}, cancelled));
  EXPECT_EQ(lookupOrDecode(file, path, "revision", Tier::Large, {200, 100}, cancelled).image.size(), QSize(256, 128));
}

TEST(ThumbnailService, CacheWriteFailureDoesNotPreventDecode) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("tier-unwritable"));
  const auto path = files_test::writeFile(dir, "image.jpg", renderJpegBytes({1000, 500}));
  QFile blocker(home.dir.path() + "/thumbnails");
  ASSERT_TRUE(blocker.open(QIODevice::WriteOnly));
  blocker.close();
  const std::atomic_bool cancelled{false};
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(
      ThumbnailService::lookupOrDecode(file, path, "revision", ThumbnailService::Tier::Large, {200, 100}, cancelled)
          .image.size(),
      QSize(256, 128));
}

namespace {
class ThumbnailOrientation : public testing::TestWithParam<int> {};

void expectCorners(const QImage& image, int orientation) {
  ASSERT_FALSE(image.isNull());
  // Stored quadrant indices at displayed TL, TR, BL, BR, from EXIF semantics.
  constexpr std::array<std::array<int, 4>, 8> corners = {
      {{0, 1, 2, 3}, {1, 0, 3, 2}, {3, 2, 1, 0}, {2, 3, 0, 1}, {0, 2, 1, 3}, {2, 0, 3, 1}, {3, 1, 2, 0}, {1, 3, 0, 2}}};
  const std::array<QColor, 4> colors = {Qt::red, Qt::green, Qt::blue, Qt::yellow};
  for (int i = 0; i < 4; ++i) {
    const auto actual =
        image.pixelColor(image.width() * (i % 2 == 0 ? 1 : 3) / 4, image.height() * (i < 2 ? 1 : 3) / 4);
    const auto expected = colors.at(corners.at(orientation - 1).at(i));
    EXPECT_NEAR(actual.red(), expected.red(), 20);
    EXPECT_NEAR(actual.green(), expected.green(), 20);
    EXPECT_NEAR(actual.blue(), expected.blue(), 20);
  }
}
}  // namespace

TEST_P(ThumbnailOrientation, AppliesCornersRectangularBoundsAndNoUpscaling) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("orientation"));
  const auto path = files_test::writeFile(dir, "image.jpg", files_test::orientationJpeg({120, 60}, GetParam()));
  const std::atomic_bool cancelled{false};
  QFile source(path);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  const auto bounded = ThumbnailService::decodeScaled(source, {40, 60}, cancelled).image;
  EXPECT_EQ(bounded.size(), GetParam() >= 5 ? QSize(30, 60) : QSize(40, 20));
  expectCorners(bounded, GetParam());
  const auto small = ThumbnailService::decodeScaled(source, {500, 500}, cancelled).image;
  EXPECT_EQ(small.size(), GetParam() >= 5 ? QSize(60, 120) : QSize(120, 60));
  expectCorners(small, GetParam());
  EXPECT_TRUE(source.isOpen());
}

TEST_P(ThumbnailOrientation, MigratesLegacyCacheAndReusesOrientedPixelsAcrossTiers) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("orientation-cache"));
  const auto path = files_test::writeFile(dir, "image.jpg", files_test::orientationJpeg({2400, 1200}, GetParam()));
  const std::atomic_bool cancelled{false};
  QFile source(path);
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  using namespace ThumbnailService;
  const std::array names = {"normal", "large", "x-large", "xx-large"};
  int index = 0;
  for (const auto tier : {Tier::Normal, Tier::Large, Tier::XLarge, Tier::XXLarge}) {
    const int extent = static_cast<int>(tier);
    const QSize required = GetParam() >= 5 ? QSize(extent / 2, extent) : QSize(extent, extent / 2);
    const auto cachePath =
        cachePathFor(home.dir.path(), path).replace("/normal/", "/" + QString::fromLatin1(names.at(index++)) + "/");
    const auto cold = lookupOrDecode(source, path, "revision", tier, required, cancelled).image;
    EXPECT_EQ(cold.size(), required);
    expectCorners(cold, GetParam());
    const QImage marked(cachePath);
    ASSERT_EQ(marked.text("Files::OrientationPolicy"), "applied-v1");
    // Valid legacy identity/resolution cannot establish orientation, especially for mirrors.
    for (const auto& marker : {QString(), QString("different-v1")}) {
      QImageReader storedReader(path);
      storedReader.setAutoTransform(false);
      QImage legacy = storedReader.read().scaled(required);
      for (const auto& key : marked.textKeys()) {
        if (key != "Files::OrientationPolicy") {
          legacy.setText(key, marked.text(key));
        }
      }
      if (!marker.isEmpty()) {
        legacy.setText("Files::OrientationPolicy", marker);
      }
      ASSERT_TRUE(legacy.save(cachePath));
      EXPECT_FALSE(lookup(source, path, "revision", tier, required, cancelled));
      EXPECT_FALSE(lookup(source, path, {}, tier, required, cancelled));
      const auto regenerated = lookupOrDecode(source, path, "revision", tier, required, cancelled).image;
      expectCorners(regenerated, GetParam());
      const QImage persisted(cachePath);
      EXPECT_EQ(persisted.text("Files::OrientationPolicy"), "applied-v1");
      source.close();  // Reuse without source decode, with no second transform.
      const auto warm = lookup(source, path, "revision", tier, required, cancelled).value().image;
      EXPECT_EQ(warm, persisted);
      expectCorners(warm, GetParam());
      ASSERT_TRUE(source.open(QIODevice::ReadOnly));
    }
    EXPECT_EQ(QDir(home.dir.path() + "/thumbnails").entryList(QDir::Dirs | QDir::NoDotAndDotDot).size(), index);
  }
}

INSTANTIATE_TEST_SUITE_P(ExifValues, ThumbnailOrientation, testing::Range(1, 9));

TEST(ThumbnailService, MissingAndInvalidOrientationUseStoredDimensionsAndCorners) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("orientation-invalid"));
  for (const auto orientation : {std::optional<int>{}, std::optional<int>{0}, std::optional<int>{9}}) {
    const auto path = files_test::writeFile(dir, "image.jpg", files_test::orientationJpeg({120, 60}, orientation));
    const auto image = ThumbnailService::decodeScaled(path, {500, 500}).image;
    EXPECT_EQ(image.size(), QSize(120, 60));
    expectCorners(image, 1);
  }
}

TEST(ThumbnailService, PreCancelledDescriptorOperationsAreSilentAndDoNotTouchCache) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("thumbnail-precancel"));
  const auto path = writeJpegWithExif(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  ASSERT_TRUE(file.seek(7));
  const std::atomic_bool cancelled{true};
  int stages = 0;
  const auto callback = [&](ThumbnailService::Stage) { ++stages; };
  using namespace ThumbnailService;
  EXPECT_EQ(lookup(file, path, {}, Tier::Normal, {64, 48}, cancelled, callback)->outcome,
            HolonightImages::Outcome::Cancelled);
  EXPECT_EQ(lookupOrDecode(file, path, {}, Tier::Normal, {64, 48}, cancelled, callback).outcome,
            HolonightImages::Outcome::Cancelled);
  EXPECT_EQ(lookupOrDecode(file, path, {}, cancelled).outcome, HolonightImages::Outcome::Cancelled);
  EXPECT_EQ(decodeScaled(file, {128, 128}, cancelled, callback).outcome, HolonightImages::Outcome::Cancelled);
  EXPECT_EQ(stages, 0);
  EXPECT_EQ(file.pos(), 7);
  EXPECT_FALSE(QDir(home.dir.path() + "/thumbnails").exists());
}

namespace {
class ThumbnailCancellation : public testing::TestWithParam<ThumbnailService::Stage> {};
}  // namespace

TEST_P(ThumbnailCancellation, StopsFallbackAndPreservesExistingCacheBytes) {
  using namespace ThumbnailService;
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("thumbnail-cancel"));
  const auto path = files_test::writeFile(dir, "image.jpg", renderJpegBytes({1000, 500}));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  std::atomic_bool cancelled{false};
  ASSERT_FALSE(lookupOrDecode(file, path, "valid", Tier::Normal, {128, 64}, cancelled).image.isNull());
  // A second eligible cache tier makes accidental fallback observable.
  ASSERT_FALSE(lookupOrDecode(file, path, "valid", Tier::Large, {256, 128}, cancelled).image.isNull());
  QFile existing(cachePathFor(home.dir.path(), path));
  ASSERT_TRUE(existing.open(QIODevice::ReadOnly));
  const auto original = existing.readAll();
  existing.close();
  bool reached = false;
  int laterStages = 0;
  const auto callback = [&](Stage stage) {
    if (reached) {
      ++laterStages;
    } else if (stage == GetParam()) {
      reached = true;
      cancelled = true;
    }
  };
  // Cache stages cancel a valid hit; source/write stages force revision rejection and regeneration.
  const bool cachedStage =
      GetParam() == Stage::CacheInspect || GetParam() == Stage::CacheInspected || GetParam() == Stage::CacheDecode;
  const auto result =
      lookupOrDecode(file, path, cachedStage ? "valid" : "new", Tier::Normal, {128, 64}, cancelled, callback);
  EXPECT_EQ(result.outcome, HolonightImages::Outcome::Cancelled);
  EXPECT_TRUE(result.image.isNull());
  EXPECT_TRUE(reached);
  EXPECT_EQ(laterStages, 0);
  ASSERT_TRUE(existing.open(QIODevice::ReadOnly));
  EXPECT_EQ(existing.readAll(), original);
  EXPECT_EQ(QDir(QFileInfo(existing).absolutePath()).entryList(QDir::Files | QDir::Hidden).size(), 1);
}

INSTANTIATE_TEST_SUITE_P(Stages, ThumbnailCancellation,
                         testing::Values(ThumbnailService::Stage::CacheInspect, ThumbnailService::Stage::CacheInspected,
                                         ThumbnailService::Stage::CacheDecode, ThumbnailService::Stage::OriginalDecode,
                                         ThumbnailService::Stage::OriginalDecoded,
                                         ThumbnailService::Stage::BeforeCommit));

TEST(ThumbnailService, CancelAfterUncachedOriginalDecodeDiscardsImage) {
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("thumbnail-scaled-cancel"));
  const auto path = writeJpegWithExif(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  std::atomic_bool cancelled{false};
  const auto result = ThumbnailService::decodeScaled(file, {2048, 2048}, cancelled, [&](ThumbnailService::Stage stage) {
    if (stage == ThumbnailService::Stage::OriginalDecoded) {
      cancelled = true;
    }
  });
  EXPECT_TRUE(cancelled.load());
  EXPECT_TRUE(result.image.isNull());
  EXPECT_EQ(result.outcome, HolonightImages::Outcome::Cancelled);
  EXPECT_FALSE(QDir(home.dir.path() + "/thumbnails").exists());
}

TEST(ThumbnailService, CacheMissAndSourceFailuresPreserveOutcomes) {
  using namespace ThumbnailService;
  using HolonightImages::Outcome;
  FakeCacheHome home;
  QTemporaryDir dir(fixturePattern("source-outcomes"));
  const auto path = files_test::writeFile(dir, "image.jpg", renderJpegBytes({100, 50}));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const std::atomic_bool running{false};
  EXPECT_FALSE(lookup(file, path, {}, Tier::Normal, {100, 50}, running));
  EXPECT_EQ(lookupOrDecode(file, path, {}, running).outcome, Outcome::Success);
  const auto hit = lookup(file, path, {}, Tier::Normal, {100, 50}, running);
  ASSERT_TRUE(hit);
  EXPECT_EQ(hit->outcome, Outcome::Success);
  file.close();
  EXPECT_EQ(decodeScaled(file, {100, 50}, running).outcome, Outcome::IoFailure);
  EXPECT_EQ(decodeScaled(dir.filePath("missing.png"), {100, 50}).outcome, Outcome::IoFailure);
  const auto unsupported = files_test::writeFile(dir, "unsupported.png", "not an image");
  EXPECT_EQ(lookupOrDecode(unsupported).outcome, Outcome::Unsupported);
  const auto damaged = files_test::writeFile(dir, "damaged.png", files_test::renderPngBytes().first(45));
  EXPECT_EQ(decodeScaled(damaged, {100, 50}).outcome, Outcome::Damaged);
}

TEST(ThumbnailService, SvgPolicyMarkerRejectsLegacyThumbnailsAndRevisionMismatch) {
  FakeCacheHome cacheHome;
  QTemporaryDir dir(fixturePattern("svg-thumbnail"));
  const QByteArray bytes("<svg xmlns='http://www.w3.org/2000/svg' width='24' height='24'/>");
  const auto path = files_test::writeFile(dir, "small.svg", bytes);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const std::atomic_bool running{false};
  auto rendered =
      ThumbnailService::renderSvg(file, path, "revision", bytes, {100, 100}, ThumbnailService::Tier::Normal, running);
  ASSERT_EQ(rendered.outcome, HolonightImages::Outcome::Success);
  const auto lookup = [&](const QString& revision) {
    return ThumbnailService::lookup(file, path, revision, ThumbnailService::Tier::Normal, {100, 100}, running, {},
                                    ThumbnailService::ImageKind::Svg);
  };
  EXPECT_TRUE(lookup("revision").has_value());
  EXPECT_FALSE(lookup("changed").has_value());
  const auto cachePath = cachePathFor(cacheHome.dir.path(), path);
  QImage legacy(cachePath);
  ASSERT_FALSE(legacy.isNull());
  EXPECT_EQ(legacy.text("Files::SvgPolicy"), "self-contained-static-v1");
  legacy.setText("Files::SvgPolicy", {});
  ASSERT_TRUE(legacy.save(cachePath, "PNG"));
  EXPECT_FALSE(lookup("revision").has_value());
}
