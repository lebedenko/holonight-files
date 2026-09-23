#include "image_policy.h"
#include "preview_service.h"
#include "preview_service_test_access.h"
#include "thumbnail_service.h"

#include <QCoreApplication>
#include <QDirIterator>
#include <QFileInfo>
#include <QImageReader>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

namespace {
class UnknownDimensions : public testing::Test {
 private:
  QTemporaryDir directory_;
  QString source_ = directory_.filePath("synthetic.png");
  QString marker_ = directory_.filePath("pixel-read");

 protected:
  [[nodiscard]] QString source() const { return source_; }
  [[nodiscard]] QString marker() const { return marker_; }
  [[nodiscard]] QString filePath(const QString& name) const { return directory_.filePath(name); }

  void SetUp() override {
    ASSERT_TRUE(directory_.isValid());
    qputenv("XDG_CACHE_HOME", directory_.filePath("cache").toUtf8());
    qputenv("FILES_UNKNOWN_SIZE_READ_MARKER", marker_.toUtf8());
    QFile file(source_);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(QByteArray("\0HN-UNKNOWN-SIZE\xff\x01\x02", 18)), 18);
  }
  void expectNoDecodeOrCache() {
    EXPECT_FALSE(QFile::exists(marker_));
    QDirIterator files(directory_.filePath("cache"), QDir::Files, QDirIterator::Subdirectories);
    EXPECT_FALSE(files.hasNext());
  }
};

TEST_F(UnknownDimensions, HandlerIsReadableButHasNoSizeAndRecordsActualDecode) {
  QImageReader reader(source());
  reader.setDecideFormatFromContent(true);
  ASSERT_TRUE(reader.canRead());
  EXPECT_EQ(reader.format(), QByteArray("hnunknownsize"));
  EXPECT_FALSE(reader.size().isValid());
  EXPECT_FALSE(QFile::exists(marker()));
  // Positive control: the codec can produce pixels, and the read detector works.
  EXPECT_EQ(reader.read().size(), QSize(2, 3));
  EXPECT_TRUE(QFile::exists(marker()));
}

TEST_F(UnknownDimensions, InstalledProviderAndThumbnailRejectBeforePixelDecode) {
  QFile file(source());
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const std::atomic_bool running{false};
  const auto inspection = HolonightImages::inspect(file, kPreviewImageLimits, running);
  EXPECT_EQ(inspection.outcome, HolonightImages::Outcome::Damaged);
  EXPECT_FALSE(inspection.sourceSize.isValid());
  const auto decoded = HolonightImages::decode(file, {.limits = kPreviewImageLimits, .bound = {128, 128}}, running);
  EXPECT_EQ(decoded.outcome, HolonightImages::Outcome::Damaged);
  EXPECT_TRUE(decoded.image.isNull());
  const auto thumbnail = ThumbnailService::lookupOrDecode(file, source(), {}, running);
  EXPECT_EQ(thumbnail.outcome, HolonightImages::Outcome::Damaged);
  EXPECT_TRUE(thumbnail.image.isNull());
  const auto tier =
      ThumbnailService::lookupOrDecode(file, source(), {}, ThumbnailService::Tier::Normal, {128, 128}, running);
  EXPECT_EQ(tier.outcome, HolonightImages::Outcome::Damaged);
  EXPECT_TRUE(tier.image.isNull());
  expectNoDecodeOrCache();
}

TEST_F(UnknownDimensions, PreviewRejectsAndRecoversOnValidSelection) {
  PreviewService service;
  const auto select = [&](const QString& path) {
    const QFileInfo info(path);
    service.setTarget(path, false, info.size(), info.lastModified(), 0100644, false, {});
    return QTest::qWaitFor([&] { return !service.busy(); }, 5000);
  };
  ASSERT_TRUE(select(source()));
  EXPECT_TRUE(service.mimeType().startsWith("image/"));
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::DecodeFailed);
  EXPECT_FALSE(service.previewErrorMessage().isEmpty());
  EXPECT_TRUE(service.image().isNull());
  expectNoDecodeOrCache();
  const auto valid = filePath("valid.png");
  QImage image(32, 24, QImage::Format_RGB32);
  image.fill(Qt::green);
  ASSERT_TRUE(image.save(valid));
  ASSERT_TRUE(select(valid));
  EXPECT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::None);
  EXPECT_TRUE(service.previewErrorMessage().isEmpty());
  EXPECT_TRUE(service.hasImage());
  EXPECT_EQ(service.sourcePixelSize(), image.size());
  EXPECT_FALSE(QFile::exists(marker()));
}

TEST_F(UnknownDimensions, PreCancellationReturnsCancelledWithoutPixelsOrPresentationError) {
  QFile file(source());
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const std::atomic_bool cancelled{true};
  EXPECT_EQ(HolonightImages::inspect(file, kPreviewImageLimits, cancelled).outcome,
            HolonightImages::Outcome::Cancelled);
  const auto decoded = HolonightImages::decode(file, {.limits = kPreviewImageLimits, .bound = {128, 128}}, cancelled);
  EXPECT_EQ(decoded.outcome, HolonightImages::Outcome::Cancelled);
  EXPECT_TRUE(decoded.image.isNull());
  const auto thumbnail = ThumbnailService::lookupOrDecode(file, source(), {}, cancelled);
  EXPECT_EQ(thumbnail.outcome, HolonightImages::Outcome::Cancelled);
  EXPECT_TRUE(thumbnail.image.isNull());
  const auto scaled = ThumbnailService::decodeScaled(file, {128, 128}, cancelled);
  EXPECT_EQ(scaled.outcome, HolonightImages::Outcome::Cancelled);
  EXPECT_TRUE(scaled.image.isNull());
  const auto error = PreviewServiceTestAccess::rasterError(decoded.outcome);
  EXPECT_EQ(error.kind, PreviewService::PreviewErrorKind::None);
  EXPECT_TRUE(error.message.isEmpty());
  expectNoDecodeOrCache();
}
}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::addLibraryPath(QStringLiteral(FILES_UNKNOWN_PLUGIN_PATH));
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
