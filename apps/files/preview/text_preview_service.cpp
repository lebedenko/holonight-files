#include "text_preview_service.h"

#include "text_lines.h"

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
  result.total_size = file.size();
  if (!file.seek(0)) {
    result.error = file.errorString();
    return result;
  }
  const auto bytes = file.read(maxBytes);
  if (file.error() != QFileDevice::NoError) {
    result.error = file.errorString();
    return result;
  }
  result.was_truncated = result.total_size > bytes.size();
  const QByteArrayView loaded = result.was_truncated ? TextLines::trimIncompleteUtf8Tail(bytes) : QByteArrayView(bytes);
  result.lines = TextLines::splitLines(TextLines::decodeUtf8(loaded));
  return result;
}

}  // namespace TextPreviewService
