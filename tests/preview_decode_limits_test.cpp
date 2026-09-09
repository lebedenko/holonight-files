#include "directory_fixtures.h"
#include "thumbnail_service.h"

#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using files_test::fixturePattern;

namespace {
// A PNG whose IHDR declares an enormous frame but carries no real pixel data — libpng/Qt can read
// the header (width/height) without ever decompressing a payload that doesn't exist. Exercises
// the same "reject before an expensive read()" path a real decompression bomb would hit, without
// needing to hand-roll a valid multi-megabyte deflate stream.
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
    appendU32(png, 0);  // CRC is not validated by the size()-only header read this test relies on
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
  QString error;
  const auto image = ThumbnailService::decodeScaled(path, QSize(1024, 1024), &error);
  EXPECT_TRUE(image.isNull());
  EXPECT_FALSE(error.isEmpty());
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
  QString error;
  const auto image = ThumbnailService::decodeScaled(path, QSize(1024, 1024), &error);
  EXPECT_FALSE(image.isNull()) << error.toStdString();
}
