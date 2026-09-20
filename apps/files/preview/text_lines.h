#pragma once

#include <QByteArrayView>
#include <QString>
#include <QStringList>

// Pure text-to-lines helpers for the Quick Look text viewer. No I/O and no QObject identity, so they
// run on PreviewService's worker thread and are unit-testable on their own.
namespace TextLines {

// Drops an incomplete multi-byte UTF-8 sequence (at most 3 bytes) from the end of bytes. Used only
// when a byte cap may have cut a character in half, so no spurious U+FFFD appears at the cut.
QByteArrayView trimIncompleteUtf8Tail(QByteArrayView bytes);

// Decodes bytes as UTF-8. Invalid sequences and NUL bytes become U+FFFD; a leading BOM is stripped.
// Decoding never fails.
QString decodeUtf8(QByteArrayView bytes);

// Splits text on LF, CRLF and lone CR. A terminator ends a line; a trailing terminator adds no extra
// empty line. Empty text yields a single empty line so the viewer always has a row to highlight.
QStringList splitLines(const QString& text);

}  // namespace TextLines
