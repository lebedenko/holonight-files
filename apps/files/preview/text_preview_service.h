#pragma once

#include <QByteArrayView>
#include <QFile>
#include <QString>
#include <QStringList>

// Pure, synchronous helpers called only from PreviewService's worker thread. Binary detection and
// the head-read share the same already-open QFile/already-read prefix the worker's MIME sniff
// produces, so text preview never opens the file twice (SPEC.md REQ-F-007, REQ-F-008, REQ-F-022).
namespace TextPreviewService {

struct TextPreviewResult {
  QStringList lines;
  QString error;
  qint64 totalSize = -1;
  bool wasTruncated = false;
};

// True if sample looks like binary data: a NUL byte anywhere, or more than 50% of the sampled
// bytes fall outside a printable/whitespace range.
bool looksBinary(QByteArrayView sample);

// Reads up to maxBytes from the start of path, decoded as UTF-8 and split into lines (see
// TextLines). totalSize reports the file's actual size regardless of how much was read;
// wasTruncated is true when totalSize > maxBytes. When truncated, a multi-byte character cut by
// the cap is dropped rather than decoded as U+FFFD.
TextPreviewResult readHead(const QString& path, qint64 maxBytes);
TextPreviewResult readHead(QFile& file, qint64 maxBytes);

}  // namespace TextPreviewService
