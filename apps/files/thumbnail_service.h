#pragma once

#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>

#include <optional>

// Synchronous helpers used on PreviewService's verified source descriptor.
namespace ThumbnailService {
enum class Tier { Normal = 128, Large = 256, XLarge = 512, XXLarge = 1024 };
QSize requiredSize(QSize source, QSize bound);
std::optional<Tier> tierForSize(QSize required);
QImage lookup(QFile& file, const QString& path, const QString& revision, Tier selected, QSize required);
QImage lookupOrDecode(QFile& file, const QString& path, const QString& revision, Tier tier, QSize required,
                      QString* errorOut);
QImage lookupOrDecode(const QString& path, QString* errorOut);
QImage lookupOrDecode(QFile& file, const QString& path, const QString& revision, QString* errorOut);
QImage decodeScaled(const QString& path, QSize targetSize, QString* errorOut);
QImage decodeScaled(QFile& file, QSize targetSize, QString* errorOut);
}  // namespace ThumbnailService
