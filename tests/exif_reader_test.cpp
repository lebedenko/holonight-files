#include "exif_reader.h"

#include "directory_fixtures.h"
#include "preview_fixtures.h"

#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <future>
#include <gtest/gtest.h>
#include <thread>

using files_test::fixturePattern;
using files_test::writeCorruptJpeg;
using files_test::writeJpegWithExif;
using files_test::writeJpegWithoutExif;
using files_test::writePngWithExif;

namespace {

QString writeMalformedPngChunk(const QTemporaryDir& dir) {
  QByteArray content;
  content.append("\x89PNG\r\n\x1a\n", 8);
  auto writeU32 = [](QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
  };

  QByteArray ihdrData;
  writeU32(ihdrData, 16);
  writeU32(ihdrData, 16);
  ihdrData.append('\x08');
  ihdrData.append('\x02');
  ihdrData.append('\x00');
  ihdrData.append('\x00');
  ihdrData.append('\x00');

  writeU32(content, 13);
  content.append("IHDR", 4);
  content.append(ihdrData);
  writeU32(content, 0);

  writeU32(content, 16);
  content.append("eXIf", 4);
  content.append("Exif\0\0EXIF\x01\x00\x00");  // declares 16 bytes but provides fewer.
  writeU32(content, 0);

  writeU32(content, 0);
  content.append("IEND", 4);
  writeU32(content, 0);

  const auto path = dir.filePath("malformed.png");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(content);
  return path;
}

QString writePngWithOversizedExif(const QTemporaryDir& dir) {
  QByteArray content;
  content.append("\x89PNG\r\n\x1a\n", 8);
  auto writeU32 = [](QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
  };

  QByteArray ihdrData;
  writeU32(ihdrData, 16);
  writeU32(ihdrData, 16);
  ihdrData.append('\x08');
  ihdrData.append('\x02');
  ihdrData.append('\x00');
  ihdrData.append('\x00');
  ihdrData.append('\x00');

  writeU32(content, 13);
  content.append("IHDR", 4);
  content.append(ihdrData);
  writeU32(content, 0);

  const auto payloadSize = 2 * 1024 * 1024;
  writeU32(content, payloadSize);
  content.append("eXIf", 4);
  content.append(QByteArray(payloadSize, 'a'));
  writeU32(content, 0);

  writeU32(content, 0);
  content.append("IEND", 4);
  writeU32(content, 0);

  const auto path = dir.filePath("oversized-exif.png");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(content);
  return path;
}

QString writePngWithManyChunks(const QTemporaryDir& dir) {
  QByteArray content;
  content.append("\x89PNG\r\n\x1a\n", 8);
  auto writeU32 = [](QByteArray& out, quint32 value) {
    out.append(static_cast<char>((value >> 24) & 0xFF));
    out.append(static_cast<char>((value >> 16) & 0xFF));
    out.append(static_cast<char>((value >> 8) & 0xFF));
    out.append(static_cast<char>(value & 0xFF));
  };

  QByteArray ihdrData;
  writeU32(ihdrData, 8);
  writeU32(ihdrData, 8);
  ihdrData.append('\x08');
  ihdrData.append('\x02');
  ihdrData.append('\x00');
  ihdrData.append('\x00');
  ihdrData.append('\x00');

  writeU32(content, 13);
  content.append("IHDR", 4);
  content.append(ihdrData);
  writeU32(content, 0);

  for (int i = 0; i < 2000; ++i) {
    writeU32(content, 4);
    content.append("tEXt", 4);
    content.append("data");
    writeU32(content, 0);
  }

  writeU32(content, 0);
  content.append("IEND", 4);
  writeU32(content, 0);

  const auto path = dir.filePath("many-chunks.png");
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(content);
  return path;
}

class CountingReadDevice : public QFile {
 public:
  using QFile::QFile;

  [[nodiscard]] qint64 bytesRead() const { return bytesRead_.load(); }

 protected:
  qint64 readData(char* data, qint64 maxlen) override {
    const auto result = QFile::readData(data, maxlen);
    if (result > 0) {
      bytesRead_.fetch_add(result, std::memory_order_relaxed);
    }
    return result;
  }

 private:
  std::atomic<qint64> bytesRead_{0};
};

class SlowCountingDevice final : public CountingReadDevice {
 public:
  using CountingReadDevice::CountingReadDevice;

 protected:
  qint64 readData(char* data, qint64 maxlen) override {
    QThread::msleep(1);
    return CountingReadDevice::readData(data, maxlen);
  }
};

}  // namespace

TEST(ExifReader, ExtractsAllFieldsFromAJpegWithExif) {
  QTemporaryDir dir(fixturePattern("exif-jpeg"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeJpegWithExif(dir);
  const auto summary = ExifReader::read(path, "image/jpeg");
  EXPECT_TRUE(summary.present);
  EXPECT_EQ(summary.make, "Holonight");
  EXPECT_EQ(summary.model, "TestCam 1000");
  EXPECT_EQ(summary.exposureTime, "1/250s");
  EXPECT_EQ(summary.focalLength, "50mm");
  EXPECT_EQ(summary.iso, "100");
}

TEST(ExifReader, JpegWithoutExifReportsNotPresent) {
  QTemporaryDir dir(fixturePattern("exif-none"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeJpegWithoutExif(dir);
  const auto summary = ExifReader::read(path, "image/jpeg");
  EXPECT_FALSE(summary.present);
  EXPECT_TRUE(summary.make.isEmpty());
}

TEST(ExifReader, CorruptedExifDoesNotCrashAndLeavesFieldsEmptyOrPartial) {
  QTemporaryDir dir(fixturePattern("exif-corrupt"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeCorruptJpeg(dir);
  const auto summary = ExifReader::read(path, "image/jpeg");
  // The file is truncated mid-stream; libexif must not crash, and whatever it can't parse must
  // simply come back empty rather than throwing or aborting (REQ-F-023).
  EXPECT_NO_THROW(ExifReader::read(path, "image/jpeg"));
  EXPECT_TRUE(summary.exposureTime.isEmpty() || summary.present);
}

TEST(ExifReader, MalformedPngLengthFieldsAreHandledWithoutCrash) {
  QTemporaryDir dir(fixturePattern("exif-malformed"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeMalformedPngChunk(dir);
  const auto summary = ExifReader::read(path, "image/png");
  EXPECT_FALSE(summary.present);
}

TEST(ExifReader, OversizedPngExifPayloadIsIgnoredWithoutUnboundedRead) {
  QTemporaryDir dir(fixturePattern("exif-oversized"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writePngWithOversizedExif(dir);
  CountingReadDevice device(path);
  ASSERT_TRUE(device.open(QIODevice::ReadOnly));
  const auto before = device.bytesRead();
  const auto summary = ExifReader::read(device, "image/png");
  EXPECT_FALSE(summary.present);
  EXPECT_GE(device.bytesRead(), before);
  EXPECT_LT(device.bytesRead(), qint64{1'200'000});
}

TEST(ExifReader, CancellationStopsLongRunningExifScan) {
  QTemporaryDir dir(fixturePattern("exif-cancel"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writePngWithManyChunks(dir);
  const auto cancel = std::make_shared<std::atomic_bool>(false);
  SlowCountingDevice device(path);
  ASSERT_TRUE(device.open(QIODevice::ReadOnly));

  auto future = std::async(std::launch::async, [&] { return ExifReader::read(device, "image/png", cancel); });
  QThread::msleep(10);
  cancel->store(true, std::memory_order_release);
  const auto summary = future.get();
  EXPECT_FALSE(summary.present);
  EXPECT_LT(device.bytesRead(), QFileInfo(path).size());
}

TEST(ExifReader, PngExifChunkIsExtractedViaTheChunkScanner) {
  QTemporaryDir dir(fixturePattern("exif-png"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writePngWithExif(dir);
  const auto summary = ExifReader::read(path, "image/png");
  EXPECT_TRUE(summary.present);
  EXPECT_EQ(summary.make, "Holonight");
  EXPECT_EQ(summary.model, "TestCam 1000");
}

TEST(ExifReader, PlainPngWithoutEXifChunkReportsNotPresent) {
  QTemporaryDir dir(fixturePattern("exif-png-none"));
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath("plain.png");
  QImage image(QSize(16, 16), QImage::Format_RGB32);
  image.fill(Qt::black);
  ASSERT_TRUE(image.save(path, "PNG"));
  const auto summary = ExifReader::read(path, "image/png");
  EXPECT_FALSE(summary.present);
}

TEST(ExifReader, LargeImagePayloadIsSkippedAndRecordCountIsBounded) {
  QTemporaryDir dir(fixturePattern("exif-sparse"));
  ASSERT_TRUE(dir.isValid());
  const auto path = dir.filePath("sparse.png");
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(QByteArray("\x89PNG\r\n\x1a\n", 8)), 8);
  ASSERT_EQ(file.write(QByteArray("\x10\0\0\0IDAT", 8)), 8);
  ASSERT_TRUE(file.seek(file.pos() + (256LL * 1024 * 1024)));
  file.write(QByteArray(4, '\0'));
  // Real small metadata after a large sparse image payload.
  const auto small = writePngWithExif(dir, "small.png");
  QFile metadata(small);
  ASSERT_TRUE(metadata.open(QIODevice::ReadOnly));
  const auto bytes = metadata.readAll();
  const auto position = bytes.indexOf("eXIf");
  ASSERT_GT(position, 4);
  file.write(bytes.sliced(position - 4));
  file.close();
  CountingReadDevice device(path);
  ASSERT_TRUE(device.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
  EXPECT_TRUE(ExifReader::read(device, "image/png").present);
  EXPECT_LT(device.bytesRead(), 65536);

  QFile many(path);
  ASSERT_TRUE(many.open(QIODevice::WriteOnly | QIODevice::Truncate));
  many.write(QByteArray("\x89PNG\r\n\x1a\n", 8));
  for (int i = 0; i < 5000; ++i) {
    many.write(QByteArray("\0\0\0\0tEXt\0\0\0\0", 12));
  }
  many.close();
  CountingReadDevice bounded(path);
  ASSERT_TRUE(bounded.open(QIODevice::ReadOnly | QIODevice::Unbuffered));
  EXPECT_FALSE(ExifReader::read(bounded, "image/png").present);
  EXPECT_LE(bounded.bytesRead(), 8 + (4096 * 8));
}
