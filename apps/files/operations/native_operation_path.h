#pragma once

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cstdlib>
#include <memory>

// Private operation paths preserve undecodable Unix bytes with surrogate escapes.
// Only lexical QString/QDir/QFileInfo operations may consume this representation.
// All filesystem boundaries use operationBytes(); UI uses QFile::decodeName(bytes).
[[nodiscard]] inline qsizetype operationSequenceLength(unsigned char lead) {
  if (lead >= 0xc2 && lead <= 0xdf) {
    return 2;
  }
  if (lead >= 0xe0 && lead <= 0xef) {
    return 3;
  }
  if (lead >= 0xf0 && lead <= 0xf4) {
    return 4;
  }
  return 1;
}
[[nodiscard]] inline QString operationPath(const QByteArray& bytes) {
  QString result;
  for (qsizetype offset = 0; offset < bytes.size();) {
    const auto lead = static_cast<unsigned char>(bytes[offset]);
    const qsizetype length = operationSequenceLength(lead);
    const auto chunk = bytes.mid(offset, length);
    const auto text = QString::fromUtf8(chunk);
    if (text.toUtf8() == chunk) {
      result += text;
      offset += length;
    } else {
      result += QChar(static_cast<char16_t>(0xdc00 + lead));
      ++offset;
    }
  }
  return result;
}
[[nodiscard]] inline QByteArray operationBytes(const QString& path) {
  QByteArray result;
  qsizetype begin = 0;
  for (qsizetype index = 0; index < path.size(); ++index) {
    const auto unit = path[index].unicode();
    // A low surrogate in a valid supplementary character is not an escape.
    if (unit >= 0xdc80 && unit <= 0xdcff && (index == 0 || !path[index - 1].isHighSurrogate())) {
      result += path.mid(begin, index - begin).toUtf8();
      result += static_cast<char>(unit - 0xdc00);
      begin = index + 1;
    }
  }
  result += path.mid(begin).toUtf8();
  return result;
}
[[nodiscard]] inline QString canonicalOperationPath(const QString& path) {
  const std::unique_ptr<char, decltype(&std::free)> resolved(::realpath(operationBytes(path).constData(), nullptr),
                                                             &std::free);
  if (resolved == nullptr) {
    return {};
  }
  return operationPath(QByteArray(resolved.get()));
}
