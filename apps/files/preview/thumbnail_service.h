#pragma once

#include <QFile>
#include <QImage>
#include <QSize>
#include <QString>

#include <atomic>
#include <functional>
#include <holonight_images/image.h>
#include <optional>
#include <utility>

// Synchronous helpers used on PreviewService's verified source descriptor.
namespace ThumbnailService {
// Internal per-request synchronization seam; never exposed to QML or stored globally.
enum class Stage { CacheInspect, CacheInspected, CacheDecode, OriginalDecode, OriginalDecoded, BeforeCommit };
using StageCallback = std::function<void(Stage)>;
struct Result {
  QImage image;
  HolonightImages::Outcome outcome;
  Result(QImage pixels, HolonightImages::Outcome status) : image(std::move(pixels)), outcome(status) {}
};

enum class ImageKind { Raster, Svg };

enum class Tier { Normal = 128, Large = 256, XLarge = 512, XXLarge = 1024 };
QSize requiredSize(QSizeF source, QSize bound, ImageKind kind = ImageKind::Raster);
std::optional<Tier> tierForSize(QSize required);
std::optional<Result> lookup(QFile& file, const QString& path, const QString& revision, Tier selected, QSize required,
                             const std::atomic_bool& cancelled, const StageCallback& stage = {},
                             ImageKind kind = ImageKind::Raster);
Result renderSvg(QFile& file, const QString& path, const QString& revision, const QByteArray& bytes, QSize bound,
                 std::optional<Tier> tier, const std::atomic_bool& cancelled, const StageCallback& stage = {});
Result lookupOrDecode(QFile& file, const QString& path, const QString& revision, Tier tier, QSize required,
                      const std::atomic_bool& cancelled, const StageCallback& stage = {});
Result lookupOrDecode(const QString& path);
Result lookupOrDecode(QFile& file, const QString& path, const QString& revision, const std::atomic_bool& cancelled);
Result decodeScaled(const QString& path, QSize targetSize);
Result decodeScaled(QFile& file, QSize targetSize, const std::atomic_bool& cancelled, const StageCallback& stage = {});
}  // namespace ThumbnailService
