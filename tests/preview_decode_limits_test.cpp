#include "directory_fixtures.h"
#include "thumbnail_service.h"

#include <QDataStream>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using files_test::fixturePattern;

namespace {
// An enormous declared PNG frame with invalid CRCs and no pixel payload must fail fast.
// The provider classifies this malformed header as Damaged; the valid BMP below covers ResourceLimit.
QString writePathologicalPng(const QTemporaryDir& dir) {
  QByteArray png;
  png.append("\x89PNG\r\n\x1a\n", 8);
  QByteArray ihdrData;
  const auto appendU32 = [&](QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
  };
  appendU32(ihdrData, 60000);             // width
  appendU32(ihdrData, 60000);             // height
  ihdrData.append(static_cast<char>(8));  // bit depth
  ihdrData.append(static_cast<char>(2));  // color type: truecolor
  ihdrData.append(static_cast<char>(0));  // compression
  ihdrData.append(static_cast<char>(0));  // filter
  ihdrData.append(static_cast<char>(0));  // interlace
  const auto appendChunk = [&](const QByteArray& type, const QByteArray& data) {
    appendU32(png, static_cast<quint32>(data.size()));
    png.append(type);
    png.append(data);
    appendU32(png, 0);  // Deliberately invalid CRC.
  };
  appendChunk("IHDR", ihdrData);
  appendChunk("IEND", {});
  const auto path = dir.filePath("pathological.png");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(png);
  return path;
}
}  // namespace

TEST(PreviewDecodeLimits, PathologicallyLargeDeclaredDimensionsFailFastRatherThanHang) {
  QTemporaryDir dir(fixturePattern("decode-limit-png"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writePathologicalPng(dir);
  QElapsedTimer elapsed;
  elapsed.start();
  const auto result = ThumbnailService::decodeScaled(path, QSize(1024, 1024));
  EXPECT_TRUE(result.image.isNull());
  EXPECT_EQ(result.outcome, HolonightImages::Outcome::Damaged);
  // "Fails fast" per REQ-NF-001/REQ-NF-003 — well under the 3-second decode timeout budget.
  EXPECT_LT(elapsed.elapsed(), 3000);
}

TEST(PreviewDecodeLimits, ALegitimateLargeImageStillDecodesWithinTheAllocationBudget) {
  QTemporaryDir dir(fixturePattern("decode-limit-legit"));
  ASSERT_TRUE(dir.isValid());
  QImage large(QSize(2000, 1500), QImage::Format_RGB32);
  large.fill(Qt::gray);
  const auto path = dir.filePath("legit-large.png");
  ASSERT_TRUE(large.save(path, "PNG"));
  const auto image = ThumbnailService::decodeScaled(path, QSize(1024, 1024)).image;
  EXPECT_FALSE(image.isNull());
}

TEST(PreviewDecodeLimits, ValidOversizedHeaderPreservesResourceLimit) {
  QTemporaryDir dir(fixturePattern("decode-limit-bmp"));
  QByteArray bitmap;
  QDataStream header(&bitmap, QIODevice::WriteOnly);
  header.setByteOrder(QDataStream::LittleEndian);
  header << quint16{0x4d42} << quint32{54} << quint32{0} << quint32{54} << quint32{40} << qint32{9000} << qint32{9000}
         << quint16{1} << quint16{24} << quint32{0} << quint32{0} << qint32{0} << qint32{0} << quint32{0} << quint32{0};
  const auto path = dir.filePath("limited.bmp");
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bitmap), bitmap.size());
  file.close();
  EXPECT_EQ(ThumbnailService::decodeScaled(path, {1024, 1024}).outcome, HolonightImages::Outcome::ResourceLimit);
}
