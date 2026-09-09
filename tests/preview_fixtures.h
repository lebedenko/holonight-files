#pragma once

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QImage>
#include <QString>
#include <QTemporaryDir>

#include <cstdlib>
#include <cstring>
#include <libexif/exif-data.h>
#include <libexif/exif-entry.h>
#include <libexif/exif-ifd.h>
#include <libexif/exif-tag.h>
#include <libexif/exif-utils.h>

// Fixture builders for PreviewService/ThumbnailService/ExifReader/TextPreviewService tests,
// mirroring directory_fixtures.h's philosophy: real files on a real QTemporaryDir filesystem,
// synthesized at test time rather than checked into the repo as static binaries (SPEC.md
// REQ-C-002/REQ-C-003 apply the same "no mocked filesystem" spirit here).
namespace files_test {

inline QByteArray renderJpegBytes(QSize size = QSize(64, 48)) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(Qt::darkCyan);
  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "JPEG", 90);
  return buffer.data();
}

inline QByteArray renderPngBytes(QSize size = QSize(64, 48)) {
  QImage image(size, QImage::Format_RGB32);
  image.fill(Qt::darkMagenta);
  QBuffer buffer;
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  return buffer.data();
}

namespace detail {

inline ExifEntry* newEntry(ExifIfd ifd, ExifData* data, ExifTag tag, ExifFormat format, unsigned long components,
                           unsigned int size) {
  auto* entry = exif_entry_new();
  entry->tag = tag;
  entry->format = format;
  entry->components = components;
  entry->size = size;
  entry->data = static_cast<unsigned char*>(malloc(size));
  exif_content_add_entry(data->ifd[ifd], entry);
  exif_entry_unref(entry);  // exif_content_add_entry took its own reference.
  return entry;
}

inline void setAsciiEntry(ExifData* data, ExifIfd ifd, ExifTag tag, const QByteArray& value) {
  auto* entry = newEntry(ifd, data, tag, EXIF_FORMAT_ASCII, static_cast<unsigned long>(value.size() + 1),
                         static_cast<unsigned int>(value.size() + 1));
  std::memcpy(entry->data, value.constData(), static_cast<size_t>(value.size()));
  entry->data[value.size()] = '\0';
}

inline void setRationalEntry(ExifData* data, ExifIfd ifd, ExifTag tag, ExifRational value, ExifByteOrder order) {
  auto* entry = newEntry(ifd, data, tag, EXIF_FORMAT_RATIONAL, 1, 8);
  exif_set_rational(entry->data, order, value);
}

inline void setShortEntry(ExifData* data, ExifIfd ifd, ExifTag tag, ExifShort value, ExifByteOrder order) {
  auto* entry = newEntry(ifd, data, tag, EXIF_FORMAT_SHORT, 1, 2);
  exif_set_short(entry->data, order, value);
}

// PNG's CRC-32 (ISO 3309 / zlib's polynomial), needed to hand-splice a well-formed eXIf chunk.
inline quint32 pngCrc32(const QByteArray& bytes) {
  static quint32 table[256];
  static bool initialized = false;
  if (!initialized) {
    for (quint32 n = 0; n < 256; ++n) {
      quint32 c = n;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      table[n] = c;
    }
    initialized = true;
  }
  quint32 crc = 0xFFFFFFFFu;
  for (const unsigned char byte : bytes) {
    crc = table[(crc ^ byte) & 0xFFu] ^ (crc >> 8);
  }
  return crc ^ 0xFFFFFFFFu;
}

inline quint32 readBigEndianU32(const QByteArray& bytes, qsizetype at) {
  const auto* p = reinterpret_cast<const unsigned char*>(bytes.constData() + at);
  return (quint32(p[0]) << 24) | (quint32(p[1]) << 16) | (quint32(p[2]) << 8) | quint32(p[3]);
}

}  // namespace detail

// Builds a small, realistic EXIF/TIFF byte blob (Make/Model in IFD0, ExposureTime/FocalLength/ISO
// in the Exif sub-IFD) using libexif's own writer — the same tags ExifReader::read() extracts.
// Note: exif_data_save_data() already prepends the 6-byte "Exif\0\0" marker (i.e. it returns
// JPEG-APP1-payload-ready bytes, not a bare TIFF blob) — callers that need the bare TIFF blob
// (e.g. a PNG eXIf chunk) must strip the first 6 bytes themselves.
inline QByteArray buildSampleExifBlob() {
  ExifData* data = exif_data_new();
  exif_data_set_option(data, EXIF_DATA_OPTION_FOLLOW_SPECIFICATION);
  const auto order = EXIF_BYTE_ORDER_INTEL;
  exif_data_set_byte_order(data, order);
  detail::setAsciiEntry(data, EXIF_IFD_0, EXIF_TAG_MAKE, "Holonight");
  detail::setAsciiEntry(data, EXIF_IFD_0, EXIF_TAG_MODEL, "TestCam 1000");
  detail::setRationalEntry(data, EXIF_IFD_EXIF, EXIF_TAG_EXPOSURE_TIME, ExifRational{1, 250}, order);
  detail::setRationalEntry(data, EXIF_IFD_EXIF, EXIF_TAG_FOCAL_LENGTH, ExifRational{50, 1}, order);
  detail::setShortEntry(data, EXIF_IFD_EXIF, EXIF_TAG_ISO_SPEED_RATINGS, 100, order);
  unsigned char* rawData = nullptr;
  unsigned int rawSize = 0;
  exif_data_save_data(data, &rawData, &rawSize);
  const QByteArray blob(reinterpret_cast<char*>(rawData), static_cast<qsizetype>(rawSize));
  free(rawData);
  exif_data_unref(data);
  return blob;
}

// Inserts a JPEG APP1 segment (exifBlob is already "Exif\0\0"-prefixed by buildSampleExifBlob())
// immediately after the SOI marker, exactly the shape ExifReader::read()'s libexif call expects
// for non-PNG files.
inline QByteArray spliceJpegExif(const QByteArray& jpeg, const QByteArray& exifBlob) {
  const QByteArray& payload = exifBlob;
  const auto length = static_cast<quint16>(payload.size() + 2);
  QByteArray segment;
  segment.append(char(0xFF));
  segment.append(char(0xE1));
  segment.append(char((length >> 8) & 0xFF));
  segment.append(char(length & 0xFF));
  segment.append(payload);
  return jpeg.left(2) + segment + jpeg.mid(2);
}

// Inserts a PNG eXIf chunk (raw EXIF/TIFF blob, no "Exif\0\0" prefix — that's JPEG-only) right
// after IHDR, the position ExifReader::read()'s PNG chunk scanner expects to find it in.
inline QByteArray splicePngExifChunk(const QByteArray& png, const QByteArray& exifBlobWithJpegPrefix) {
  constexpr qsizetype kSignatureSize = 8;
  const quint32 ihdrLength = detail::readBigEndianU32(png, kSignatureSize);
  const qsizetype ihdrEnd = kSignatureSize + 8 + ihdrLength + 4;  // length + type + data + crc
  const auto exifBlob = exifBlobWithJpegPrefix.sliced(6);         // drop the JPEG-only "Exif\0\0" marker
  const QByteArray typeAndData = QByteArray("eXIf", 4) + exifBlob;
  const auto length = static_cast<quint32>(exifBlob.size());
  QByteArray chunk;
  chunk.append(char((length >> 24) & 0xFF));
  chunk.append(char((length >> 16) & 0xFF));
  chunk.append(char((length >> 8) & 0xFF));
  chunk.append(char(length & 0xFF));
  chunk.append(typeAndData);
  const quint32 crc = detail::pngCrc32(typeAndData);
  chunk.append(char((crc >> 24) & 0xFF));
  chunk.append(char((crc >> 16) & 0xFF));
  chunk.append(char((crc >> 8) & 0xFF));
  chunk.append(char(crc & 0xFF));
  return png.left(ihdrEnd) + chunk + png.mid(ihdrEnd);
}

inline QString writeJpegWithExif(const QTemporaryDir& dir, const QString& name = QStringLiteral("jpeg_with_exif.jpg")) {
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(spliceJpegExif(renderJpegBytes(), buildSampleExifBlob()));
  return path;
}

inline QString writeJpegWithoutExif(const QTemporaryDir& dir,
                                    const QString& name = QStringLiteral("jpeg_no_exif.jpg")) {
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(renderJpegBytes());
  return path;
}

inline QString writePngWithExif(const QTemporaryDir& dir, const QString& name = QStringLiteral("png_with_exif.png")) {
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(splicePngExifChunk(renderPngBytes(), buildSampleExifBlob()));
  return path;
}

inline QString writeCorruptJpeg(const QTemporaryDir& dir, const QString& name = QStringLiteral("corrupt_jpeg.jpg")) {
  auto bytes = renderJpegBytes();
  bytes.truncate(bytes.size() / 3);
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(bytes);
  return path;
}

inline QString writeSmallText(const QTemporaryDir& dir, const QString& name = QStringLiteral("small.txt")) {
  QByteArray content;
  const QByteArray line = "The quick brown fox jumps over the lazy dog.\n";
  while (content.size() < 10 * 1024) {
    content += line;
  }
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(content);
  return path;
}

// The first 64 KiB (readHead's cutoff) is all 'A'; everything past it is 'B', so a test can assert
// exactly what a truncated head-read should — and should not — contain.
inline QString writeLargeText(const QTemporaryDir& dir, const QString& name = QStringLiteral("large.txt")) {
  QByteArray content(64 * 1024, 'A');
  content += QByteArray(140 * 1024, 'B');
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(content);
  return path;
}

inline QString writeUtf8Multilang(const QTemporaryDir& dir,
                                  const QString& name = QStringLiteral("utf8_multilang.txt")) {
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(QStringLiteral("Café résumé — naïve. 日本語のテキスト。 😀🎉📁 Здравствуй, мир!\n").toUtf8());
  return path;
}

inline QString writeRandomBinary(const QTemporaryDir& dir, const QString& name = QStringLiteral("random.bin")) {
  QByteArray bytes(4096, '\0');
  for (qsizetype i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<char>(i % 256);
  }
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(bytes);
  return path;
}

inline QString writeBrokenSymlink(const QTemporaryDir& dir, const QString& name = QStringLiteral("broken_link")) {
  const auto path = dir.filePath(name);
  QFile::link(dir.filePath(QStringLiteral("missing-target-for-%1").arg(name)), path);
  return path;
}

// The smallest valid GIF (1x1, transparent) — Qt's GIF plugin is read-only, so this well-known
// literal stands in for QImage::save(), which cannot produce one.
inline QString writeGifSample(const QTemporaryDir& dir, const QString& name = QStringLiteral("sample.gif")) {
  static const auto kGif = QByteArray::fromBase64("R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBTAA7");
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(kGif);
  return path;
}

inline QString writeSvgSample(const QTemporaryDir& dir, const QString& name = QStringLiteral("sample.svg")) {
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return path;
  }
  file.write(QByteArrayLiteral(
      "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\"><rect width=\"64\" height=\"64\" "
      "fill=\"#3366ff\"/></svg>"));
  return path;
}

inline QString writeBmpSample(const QTemporaryDir& dir, const QString& name = QStringLiteral("sample.bmp")) {
  QImage image(QSize(64, 48), QImage::Format_RGB32);
  image.fill(Qt::yellow);
  const auto path = dir.filePath(name);
  image.save(path, "BMP");
  return path;
}

inline QString writeTiffSample(const QTemporaryDir& dir, const QString& name = QStringLiteral("sample.tiff")) {
  QImage image(QSize(64, 48), QImage::Format_RGB32);
  image.fill(Qt::darkGreen);
  const auto path = dir.filePath(name);
  image.save(path, "TIFF");
  return path;
}

inline QString writeWebpSample(const QTemporaryDir& dir, const QString& name = QStringLiteral("sample.webp")) {
  QImage image(QSize(64, 48), QImage::Format_RGB32);
  image.fill(Qt::red);
  const auto path = dir.filePath(name);
  image.save(path, "WEBP");
  return path;
}

}  // namespace files_test
