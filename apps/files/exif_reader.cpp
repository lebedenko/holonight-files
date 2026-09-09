#include "exif_reader.h"

#include <QFile>

#include <array>
#include <atomic>
#include <libexif/exif-data.h>
#include <libexif/exif-entry.h>
#include <libexif/exif-ifd.h>
#include <libexif/exif-log.h>
#include <libexif/exif-tag.h>
#include <memory>

namespace ExifReader {
namespace {

using ExifDataPtr = std::unique_ptr<ExifData, decltype(&exif_data_unref)>;
using ExifLogPtr = std::unique_ptr<ExifLog, decltype(&exif_log_unref)>;

// bytes -> const unsigned char* without a reinterpret_cast: char and unsigned char have the same
// representation, and a two-step static_cast through void* is well-defined for that conversion.
constexpr qsizetype kMaxExifPayloadBytes = 1024 * 1024;
constexpr int kMaxExifChunks = 4096;

void silentLog(ExifLog* log, ExifLogCode code, const char* domain, const char* format, va_list args, void* data);
QString entryValue(ExifContent* content, ExifTag tag);
QString normalizeExposureTime(QString value);
QString normalizeFocalLength(QString value);

const unsigned char* asUnsignedChar(const char* bytes) {
  return static_cast<const unsigned char*>(static_cast<const void*>(bytes));
}

bool isCancelled(const std::shared_ptr<std::atomic_bool>& cancel) {
  return cancel != nullptr && cancel->load(std::memory_order_acquire);
}

bool readFully(QIODevice& source, char* data, qsizetype expected, const std::shared_ptr<std::atomic_bool>& cancel) {
  if (expected <= 0) {
    return true;
  }
  if (isCancelled(cancel)) {
    return false;
  }
  const auto got = source.read(data, expected);
  return got == expected;
}

template <typename T>
bool readBigEndian(QIODevice& source, T& value, const std::shared_ptr<std::atomic_bool>& cancel) {
  if (isCancelled(cancel)) {
    return false;
  }
  std::array<char, sizeof(T)> bytes{};
  if (!readFully(source, bytes.data(), static_cast<qsizetype>(bytes.size()), cancel)) {
    return false;
  }
  if constexpr (sizeof(T) == 2) {
    value = static_cast<T>((static_cast<T>(static_cast<unsigned char>(bytes[0])) << 8) |
                           static_cast<T>(static_cast<unsigned char>(bytes[1])));
  } else if constexpr (sizeof(T) == 4) {
    value = static_cast<T>((static_cast<T>(static_cast<unsigned char>(bytes[0])) << 24) |
                           (static_cast<T>(static_cast<unsigned char>(bytes[1])) << 16) |
                           (static_cast<T>(static_cast<unsigned char>(bytes[2])) << 8) |
                           static_cast<T>(static_cast<unsigned char>(bytes[3])));
  }
  return true;
}

bool skipBytes(QIODevice& source, qint64 byteCount, const std::shared_ptr<std::atomic_bool>& cancel) {
  if (isCancelled(cancel)) {
    return false;
  }
  if (byteCount <= 0) {
    return true;
  }
  return !source.isSequential() && byteCount <= source.size() - source.pos() && source.seek(source.pos() + byteCount);
}

QByteArray readLimitedPayload(QIODevice& source, qsizetype payloadSize, qsizetype maxSize,
                              const std::shared_ptr<std::atomic_bool>& cancel) {
  if (payloadSize <= 0) {
    return {};
  }
  const auto accepted = qMin(payloadSize, maxSize);
  QByteArray data = source.read(accepted);
  if (data.size() != accepted || isCancelled(cancel)) {
    return {};
  }
  if (payloadSize > maxSize && !skipBytes(source, payloadSize - maxSize, cancel)) {
    return {};
  }
  return data;
}

ExifSummary parseExifBlob(const QByteArray& payload) {
  ExifSummary summary;
  ExifDataPtr data(exif_data_new(), exif_data_unref);
  if (!data) {
    return summary;
  }
  ExifLogPtr log(exif_log_new(), exif_log_unref);
  if (log) {
    exif_log_set_func(log.get(), &silentLog, nullptr);
    exif_data_log(data.get(), log.get());
  }
  exif_data_load_data(data.get(), asUnsignedChar(payload.constData()), static_cast<unsigned int>(payload.size()));
  const auto make = entryValue(data->ifd[EXIF_IFD_0], EXIF_TAG_MAKE);
  const auto model = entryValue(data->ifd[EXIF_IFD_0], EXIF_TAG_MODEL);
  const auto exposureTime = entryValue(data->ifd[EXIF_IFD_EXIF], EXIF_TAG_EXPOSURE_TIME);
  const auto focalLength = entryValue(data->ifd[EXIF_IFD_EXIF], EXIF_TAG_FOCAL_LENGTH);
  const auto iso = entryValue(data->ifd[EXIF_IFD_EXIF], EXIF_TAG_ISO_SPEED_RATINGS);
  if (make.isEmpty() && model.isEmpty() && exposureTime.isEmpty() && focalLength.isEmpty() && iso.isEmpty()) {
    return summary;
  }
  summary.present = true;
  summary.make = make;
  summary.model = model;
  summary.exposureTime = normalizeExposureTime(exposureTime);
  summary.iso = iso;
  summary.focalLength = normalizeFocalLength(focalLength);
  return summary;
}

bool readJpegMarker(QIODevice& source, quint16& marker, int& records, const std::shared_ptr<std::atomic_bool>& cancel) {
  if (!readBigEndian(source, marker, cancel) || (marker >> 8) != 0xff) {
    return false;
  }
  while (marker == 0xffff && ++records < kMaxExifChunks) {
    char byte = 0;
    if (!readFully(source, &byte, 1, cancel)) {
      return false;
    }
    marker = static_cast<quint16>(0xff00 | static_cast<unsigned char>(byte));
  }
  return records < kMaxExifChunks;
}

QByteArray app1Payload(QIODevice& source, qint64 size, const std::shared_ptr<std::atomic_bool>& cancel) {
  const auto signature = source.read(6);
  if (signature != QByteArrayLiteral("Exif\0\0")) {
    skipBytes(source, size - 6, cancel);
    return {};
  }
  const auto payload = readLimitedPayload(source, size - 6, kMaxExifPayloadBytes - 6, cancel);
  return payload.isEmpty() ? QByteArray{} : signature + payload;
}

QByteArray jpegPayload(QIODevice& source, const std::shared_ptr<std::atomic_bool>& cancel) {
  quint16 signature = 0;
  if (!readBigEndian(source, signature, cancel) || signature != 0xffd8) {
    return {};
  }
  for (int records = 0; records < kMaxExifChunks && !isCancelled(cancel); ++records) {
    quint16 marker = 0;
    if (!readJpegMarker(source, marker, records, cancel)) {
      return {};
    }
    if (records >= kMaxExifChunks || marker == 0xffd9 || marker == 0xffda) {
      return {};
    }
    if (marker >= 0xffd0 && marker <= 0xffd7) {
      continue;
    }
    quint16 length = 0;
    if (!readBigEndian(source, length, cancel) || length < 2 || length - 2 > source.size() - source.pos()) {
      return {};
    }
    const qint64 payloadSize = length - 2;
    if (marker == 0xffe1 && payloadSize >= 6) {
      const auto payload = app1Payload(source, payloadSize, cancel);
      if (payload.startsWith(QByteArrayLiteral("Exif\0\0"))) {
        return payload;
      }
    } else if (!skipBytes(source, payloadSize, cancel)) {
      return {};
    }
  }
  return {};
}

QByteArray pngPayload(QIODevice& source, const std::shared_ptr<std::atomic_bool>& cancel) {
  std::array<char, 8> signature{};
  static const auto kSignature = QByteArrayLiteral("\x89PNG\r\n\x1a\n");
  if (!readFully(source, signature.data(), signature.size(), cancel) ||
      !std::equal(signature.begin(), signature.end(), kSignature.constBegin())) {
    return {};
  }
  for (int chunks = 0; chunks < kMaxExifChunks && !isCancelled(cancel); ++chunks) {
    quint32 length = 0;
    QByteArray type(4, '\0');
    if (!readBigEndian(source, length, cancel) || !readFully(source, type.data(), 4, cancel)) {
      return {};
    }
    if (source.size() - source.pos() < 4 || qint64{length} > source.size() - source.pos() - 4) {
      return {};
    }
    if (type == "IEND") {
      return {};
    }
    if (type == "eXIf") {
      if (length > kMaxExifPayloadBytes - 6) {
        return {};
      }
      const auto payload = readLimitedPayload(source, length, kMaxExifPayloadBytes - 6, cancel);
      return payload.isEmpty() ? QByteArray{} : QByteArray("Exif\0\0", 6) + payload;
    }
    if (!skipBytes(source, qint64{length} + 4, cancel)) {
      return {};
    }
  }
  return {};
}

void silentLog(ExifLog* log, ExifLogCode code, const char* domain, const char* format, va_list args, void* data) {
  Q_UNUSED(log);
  Q_UNUSED(code);
  Q_UNUSED(domain);
  Q_UNUSED(format);
  Q_UNUSED(args);
  Q_UNUSED(data);
  // Intentionally discards libexif's default stderr warnings for malformed EXIF blocks, keeping
  // REQ-F-023 silent-and-graceful rather than console-noisy.
}

QString entryValue(ExifContent* content, ExifTag tag) {
  if (content == nullptr) {
    return {};
  }
  auto* entry = exif_content_get_entry(content, tag);
  if (entry == nullptr) {
    return {};
  }
  std::array<char, 1024> buffer{};
  const char* value = exif_entry_get_value(entry, buffer.data(), static_cast<unsigned int>(buffer.size()));
  return value == nullptr ? QString{} : QString::fromLocal8Bit(value).trimmed();
}

// libexif formats exposure time as e.g. "1/250 sec." or "0.5 sec."; the spec's example shape is
// the terser "1/250s".
QString normalizeExposureTime(QString value) {
  value.replace(QStringLiteral(" sec."), QStringLiteral("s"));
  value.replace(QStringLiteral(" sec"), QStringLiteral("s"));
  return value;
}

// libexif formats focal length as e.g. "50.0 mm"; the spec's example shape is "50mm", and a
// trailing ".0" reads as noise rather than precision.
QString normalizeFocalLength(QString value) {
  value.replace(QStringLiteral(" mm"), QStringLiteral("mm"));
  if (value.endsWith(QStringLiteral(".0mm"))) {
    value.remove(value.size() - 4, 2);
  }
  return value;
}

}  // namespace

ExifSummary read(const QString& path, const QByteArray& mimeType) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return read(file, mimeType);
}

ExifSummary read(QIODevice& source, const QByteArray& mimeType, const std::shared_ptr<std::atomic_bool>& cancel) {
  ExifSummary summary;
  if (source.isSequential() || !source.seek(0)) {
    return summary;
  }
  if (mimeType == "image/png") {
    const auto payload = pngPayload(source, cancel);
    return isCancelled(cancel) || payload.isEmpty() ? summary : parseExifBlob(payload);
  }
  if (mimeType == "image/jpeg") {
    const auto payload = jpegPayload(source, cancel);
    return isCancelled(cancel) || payload.isEmpty() ? summary : parseExifBlob(payload);
  }
  return summary;
}
}  // namespace ExifReader
