#include "text_preview_service.h"

#include <QFile>
#include <QFileInfo>

namespace TextPreviewService {

bool looksBinary(QByteArrayView sample) {
  if (sample.isEmpty()) {
    return false;
  }
  qsizetype nonPrintable = 0;
  for (const char byte : sample) {
    const auto value = static_cast<unsigned char>(byte);
    if (value == 0) {
      return true;
    }
    // Printable ASCII, plus the common whitespace control codes (tab, LF, CR, form feed,
    // vertical tab) and anything >= 0x80, which is very likely part of a valid UTF-8 sequence
    // rather than binary noise.
    const bool printable = (value >= 0x20 && value < 0x7f) || value == '\t' || value == '\n' || value == '\r' ||
                           value == '\f' || value == '\v' || value >= 0x80;
    if (!printable) {
      ++nonPrintable;
    }
  }
  return nonPrintable * 2 > sample.size();
}

TextPreviewResult readHead(const QString& path, qint64 maxBytes) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    TextPreviewResult result;
    result.error = file.errorString();
    return result;
  }
  return readHead(file, maxBytes);
}

TextPreviewResult readHead(QFile& file, qint64 maxBytes) {
  TextPreviewResult result;
  result.totalSize = file.size();
  if (!file.seek(0)) {
    result.error = file.errorString();
    return result;
  }
  const auto bytes = file.read(maxBytes);
  if (file.error() != QFileDevice::NoError) {
    result.error = file.errorString();
    return result;
  }
  result.content = QString::fromUtf8(bytes);
  result.wasTruncated = result.totalSize > bytes.size();
  return result;
}

}  // namespace TextPreviewService
