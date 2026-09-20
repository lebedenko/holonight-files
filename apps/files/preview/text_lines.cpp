#include "text_lines.h"

namespace TextLines {

namespace {

constexpr qsizetype kMaxUtf8TailBytes = 3;

qsizetype sequenceLength(unsigned char lead) {
  if (lead >= 0xF0 && lead <= 0xF4) {
    return 4;
  }
  if (lead >= 0xE0 && lead < 0xF0) {
    return 3;
  }
  if (lead >= 0xC2 && lead < 0xE0) {
    return 2;
  }
  return 1;
}

}  // namespace

QByteArrayView trimIncompleteUtf8Tail(QByteArrayView bytes) {
  const qsizetype size = bytes.size();
  const qsizetype limit = qMin(kMaxUtf8TailBytes, size);
  for (qsizetype back = 1; back <= limit; ++back) {
    const auto byte = static_cast<unsigned char>(bytes[size - back]);
    if (byte >= 0x80 && byte < 0xC0) {
      continue;  // continuation byte; keep looking for its lead
    }
    return sequenceLength(byte) > back ? bytes.first(size - back) : bytes;
  }
  return bytes;
}

QString decodeUtf8(QByteArrayView bytes) {
  QString text = QString::fromUtf8(bytes);
  if (text.startsWith(QChar(0xFEFF))) {
    text.remove(0, 1);
  }
  text.replace(QChar(0), QChar(0xFFFD));
  return text;
}

QStringList splitLines(const QString& text) {
  QStringList lines;
  qsizetype start = 0;
  const qsizetype size = text.size();
  for (qsizetype i = 0; i < size; ++i) {
    const QChar current = text.at(i);
    if (current != QLatin1Char('\n') && current != QLatin1Char('\r')) {
      continue;
    }
    lines.append(text.mid(start, i - start));
    if (current == QLatin1Char('\r') && i + 1 < size && text.at(i + 1) == QLatin1Char('\n')) {
      ++i;
    }
    start = i + 1;
  }
  if (start < size) {
    lines.append(text.mid(start));
  }
  if (lines.isEmpty()) {
    lines.append(QString());
  }
  return lines;
}

}  // namespace TextLines
