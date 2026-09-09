#pragma once

#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>

// Not a QObject, not thread-owning: a namespace of pure, synchronous functions called only from
// PreviewService's worker thread. Implements the freedesktop Thumbnail Managing Standard cache
// (SPEC.md REQ-F-017 through REQ-F-021) plus the separate full-resolution decode used for
// on-screen display (REQ-F-005).
namespace ThumbnailService {

// Looks up path's 128 px "normal" cache tier, validating the cached PNG's Thumb::MTime/Thumb::Size
// tEXt chunks against a fresh stat of path. On a cache miss (or invalid entry) decodes a fresh
// 128 px bounding-box thumbnail (aspect ratio preserved, REQ-F-019) and writes it to the cache
// atomically. Returns a null QImage (with errorOut set) if the source can't be decoded at all.
QImage lookupOrDecode(const QString& path, QString* errorOut);

// Decodes path at a resolution bounded by targetSize (aspect ratio preserved), for the
// full-resolution display tier (REQ-F-005). Not cached to disk. Returns a null QImage (with
// errorOut set) on decode failure.
QImage decodeScaled(const QString& path, QSize targetSize, QString* errorOut);

QImage decodeScaled(QFile& file, QSize targetSize, QString* errorOut);
QImage lookupOrDecode(QFile& file, const QString& path, const QString& revision, QString* errorOut);

}  // namespace ThumbnailService
