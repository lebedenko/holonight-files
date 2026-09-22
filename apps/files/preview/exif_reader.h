#pragma once

#include <QByteArray>
#include <QIODevice>
#include <QString>

#include <atomic>
#include <memory>

// Formats shared structured EXIF facts for Files presentation.
namespace ExifReader {

struct ExifSummary {
  bool present = false;
  QString make;
  QString model;
  QString exposureTime;
  QString iso;
  QString focalLength;
  QString lensModel;
  QString aperture;  // "f/X.X"
  bool operator==(const ExifSummary&) const = default;
};

ExifSummary read(const QString& path, const QByteArray& mimeType);
ExifSummary read(QIODevice& source, const QByteArray& mimeType, const std::shared_ptr<std::atomic_bool>& cancel = {});

}  // namespace ExifReader
