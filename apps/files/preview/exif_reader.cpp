#include "exif_reader.h"

#include "image_policy.h"

#include <QFile>

#include <cmath>
namespace ExifReader {
ExifSummary read(const QString& path, const QByteArray& mimeType) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return read(file, mimeType);
}
ExifSummary read(QIODevice& source, const QByteArray& mimeType, const std::shared_ptr<std::atomic_bool>& cancel) {
  if (mimeType != "image/jpeg" && mimeType != "image/png") {
    return {};
  }
  const std::atomic_bool running{false};
  const auto result = HolonightImages::readMetadata(source, kPreviewImageLimits, cancel ? *cancel : running);
  const auto& facts = result.facts;
  ExifSummary summary;
  summary.outcome = result.outcome;
  summary.make = facts.make;
  summary.model = facts.model;
  summary.lens_model = facts.lens;
  if (facts.exposureSeconds && *facts.exposureSeconds > 0) {
    const auto time = *facts.exposureSeconds;
    summary.exposure_time = time < 1 ? QStringLiteral("1/%1s").arg(std::round(1 / time), 0, 'f', 0)
                                     : QStringLiteral("%1s").arg(time, 0, 'g', 4);
  }
  if (facts.iso) {
    summary.iso = QString::number(*facts.iso);
  }
  if (facts.focalLengthMm) {
    auto number = QString::number(*facts.focalLengthMm, 'f', 1);
    if (number.endsWith(".0")) {
      number.chop(2);
    }
    summary.focal_length = number + "mm";
  }
  if (facts.aperture) {
    summary.aperture = QStringLiteral("f/%1").arg(*facts.aperture, 0, 'f', 1);
  }
  summary.present = !summary.make.isEmpty() || !summary.model.isEmpty() || !summary.lens_model.isEmpty() ||
                    !summary.exposure_time.isEmpty() || !summary.iso.isEmpty() || !summary.focal_length.isEmpty() ||
                    !summary.aperture.isEmpty();
  return summary;
}
}  // namespace ExifReader
