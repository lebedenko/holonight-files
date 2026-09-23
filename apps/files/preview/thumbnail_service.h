#pragma once

#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>

#include <atomic>
#include <functional>
#include <optional>

// Synchronous helpers used on PreviewService's verified source descriptor.
namespace ThumbnailService {
// Internal per-request synchronization seam; never exposed to QML or stored globally.
enum class Stage { CacheInspect, CacheInspected, CacheDecode, OriginalDecode, OriginalDecoded, BeforeCommit };
using StageCallback = std::function<void(Stage)>;
enum class Tier { Normal = 128, Large = 256, XLarge = 512, XXLarge = 1024 };
QSize requiredSize(QSize source, QSize bound);
std::optional<Tier> tierForSize(QSize required);
QImage lookup(QFile& file, const QString& path, const QString& revision, Tier selected, QSize required,
              const std::atomic_bool& cancelled, const StageCallback& stage = {});
QImage lookupOrDecode(QFile& file, const QString& path, const QString& revision, Tier tier, QSize required,
                      const std::atomic_bool& cancelled, QString* errorOut, const StageCallback& stage = {});
QImage lookupOrDecode(const QString& path, QString* errorOut);
QImage lookupOrDecode(QFile& file, const QString& path, const QString& revision, const std::atomic_bool& cancelled,
                      QString* errorOut);
QImage decodeScaled(const QString& path, QSize targetSize, QString* errorOut);
QImage decodeScaled(QFile& file, QSize targetSize, const std::atomic_bool& cancelled, QString* errorOut,
                    const StageCallback& stage = {});
}  // namespace ThumbnailService
