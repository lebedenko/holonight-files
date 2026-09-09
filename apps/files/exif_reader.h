#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QString>

#include <atomic>
#include <memory>

// Wraps libexif's C API behind one entry point. No exceptions anywhere in this file, matching
// libexif's own C error-signalling convention (NULL returns, not longjmp/exceptions) and
// SPEC.md REQ-C-001. A missing or corrupted individual tag leaves that one field empty; it never
// aborts the whole read (REQ-F-023).
namespace ExifReader {

struct ExifSummary {
  bool present = false;
  QString make;
  QString model;
  QString exposureTime;
  QString iso;
  QString focalLength;
  bool operator==(const ExifSummary&) const = default;
};

ExifSummary read(const QString& path, const QByteArray& mimeType);
ExifSummary read(QIODevice& source, const QByteArray& mimeType, const std::shared_ptr<std::atomic_bool>& cancel = {});

}  // namespace ExifReader
