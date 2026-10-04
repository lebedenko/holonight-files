#include "thumbnail_service.h"

#include "image_policy.h"

#include <QDateTime>
#include <QFile>
#include <QUrl>

#include <holonight_images/svg.h>
#include <holonight_thumbnails/cache.h>
#include <sys/stat.h>

namespace ThumbnailService {
namespace {

using HolonightImages::Outcome;

HolonightThumbnails::Request cacheRequest(QFile& file, const QString& path, const QString& revision, QSize required,
                                          ImageKind kind, Tier tier) {
  struct stat source{};
  const bool metadataValid = file.handle() >= 0 && ::fstat(file.handle(), &source) == 0;
  const qint64 milliseconds =
      metadataValid ? (static_cast<qint64>(source.st_mtim.tv_sec) * 1000) + (source.st_mtim.tv_nsec / 1000000) : 0;
  return {
      .uri = QUrl::fromLocalFile(path),
      .modified = metadataValid ? QDateTime::fromMSecsSinceEpoch(milliseconds) : QDateTime{},
      .size = metadataValid ? source.st_size : -1,
      .required = required,
      .kind = kind == ImageKind::Svg ? HolonightThumbnails::Kind::Svg : HolonightThumbnails::Kind::Raster,
      .revision = revision,
      .tier = static_cast<int>(tier),
  };
}

HolonightThumbnails::StageCallback cacheStage(const StageCallback& stage) {
  if (!stage) {
    return {};
  }
  return [stage](HolonightThumbnails::Stage current) {
    switch (current) {
      case HolonightThumbnails::Stage::CacheInspect:
        stage(Stage::CacheInspect);
        break;
      case HolonightThumbnails::Stage::CacheInspected:
        stage(Stage::CacheInspected);
        break;
      case HolonightThumbnails::Stage::CacheDecode:
        stage(Stage::CacheDecode);
        break;
      case HolonightThumbnails::Stage::BeforeCommit:
        stage(Stage::BeforeCommit);
        break;
    }
  };
}

// Shared by both the cache tier and the full-resolution tier: setScaledSize() before read() lets
// format plugins with scaled-decode support (JPEG, PNG) avoid allocating a full-resolution bitmap
// just to downscale it (REQ-NF-003).
Result decodeBounded(QFile& file, QSize bound, HolonightImages::OrientationPolicy orientation,
                     const std::atomic_bool& cancelled) {
  auto result = HolonightImages::decode(
      file, {.limits = kPreviewImageLimits, .bound = bound, .orientation = orientation}, cancelled);
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  return {std::move(result.image), result.outcome};
}

}  // namespace

Result lookupOrDecode(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {{}, Outcome::IoFailure};
  }
  const std::atomic_bool cancelled{false};
  return lookupOrDecode(file, path, {}, cancelled);
}

QSize requiredSize(QSizeF source, QSize bound, ImageKind kind) {
  if (!bound.isValid() || bound.isEmpty()) {
    bound = QSize(1024, 1024);
  }
  if (!source.isValid() || source.isEmpty()) {
    return bound;
  }
  if (kind == ImageKind::Svg) {
    return HolonightImages::svgPixelSize(source, bound);
  }
  return source.toSize().scaled(bound.boundedTo(source.toSize()), Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

std::optional<Tier> tierForSize(QSize required) {
  const auto tier = HolonightThumbnails::tierForSize(required);
  return tier ? std::optional{static_cast<Tier>(*tier)} : std::nullopt;
}

std::optional<Result> lookup(QFile& file, const QString& path, const QString& revision, Tier selected, QSize required,
                             const std::atomic_bool& cancelled, const StageCallback& stage, ImageKind kind) {
  if (cancelled.load()) {
    return Result{{}, Outcome::Cancelled};
  }
  auto cached = HolonightThumbnails::lookup(cacheRequest(file, path, revision, required, kind, selected), cancelled,
                                            cacheStage(stage));
  if (cancelled.load()) {
    return Result{{}, Outcome::Cancelled};
  }
  return cached ? std::optional<Result>{Result{std::move(*cached), Outcome::Success}} : std::nullopt;
}

Result renderSvg(QFile& file, const QString& path, const QString& revision, const QByteArray& bytes, QSize bound,
                 std::optional<Tier> tier, const std::atomic_bool& cancelled, const StageCallback& stage) {
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  if (tier) {
    bound = QSize(static_cast<int>(*tier), static_cast<int>(*tier));
  }
  if (stage) {
    stage(Stage::OriginalDecode);
  }
  auto decoded = HolonightImages::rasterizeSvg(bytes, {.bound = bound, .outputBytes = kPreviewImageLimits.decodedBytes},
                                               cancelled);
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  if (stage) {
    stage(Stage::OriginalDecoded);
  }
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  if (decoded.inspection.outcome == Outcome::Success && tier) {
    HolonightThumbnails::store(cacheRequest(file, path, revision, decoded.image.size(), ImageKind::Svg, *tier),
                               decoded.image, cancelled, cacheStage(stage));
  }
  return cancelled.load() ? Result{{}, Outcome::Cancelled}
                          : Result{std::move(decoded.image), decoded.inspection.outcome};
}

Result lookupOrDecode(QFile& file, const QString& path, const QString& revision, Tier tier, QSize required,
                      const std::atomic_bool& cancelled, const StageCallback& stage) {
  auto cached = lookup(file, path, revision, tier, required, cancelled, stage);
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  if (cached) {
    return *cached;
  }
  const auto extent = static_cast<int>(tier);
  if (stage) {
    stage(Stage::OriginalDecode);
  }
  auto decoded = decodeBounded(file, {extent, extent}, HolonightImages::OrientationPolicy::Apply, cancelled);
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  if (stage) {
    stage(Stage::OriginalDecoded);
  }
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  if (decoded.outcome != Outcome::Success) {
    return decoded;
  }
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  HolonightThumbnails::store(cacheRequest(file, path, revision, required, ImageKind::Raster, tier), decoded.image,
                             cancelled, cacheStage(stage));
  return cancelled.load() ? Result{{}, Outcome::Cancelled} : decoded;
}

Result lookupOrDecode(QFile& file, const QString& path, const QString& revision, const std::atomic_bool& cancelled) {
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  const auto inspection =
      HolonightImages::inspect(file, kPreviewImageLimits, cancelled, HolonightImages::OrientationPolicy::Apply, false);
  if (inspection.outcome == HolonightImages::Outcome::Cancelled || cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  const auto source = inspection.orientedSize;
  if (!source.isValid() || source.isEmpty()) {
    return decodeScaled(file, {128, 128}, cancelled);
  }
  return lookupOrDecode(file, path, revision, Tier::Normal, requiredSize(source, {128, 128}), cancelled);
}

Result decodeScaled(const QString& path, QSize targetSize) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {{}, Outcome::IoFailure};
  }
  const std::atomic_bool cancelled{false};
  return decodeScaled(file, targetSize, cancelled);
}

Result decodeScaled(QFile& file, QSize targetSize, const std::atomic_bool& cancelled, const StageCallback& stage) {
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  const auto bound = (targetSize.isValid() && !targetSize.isEmpty()) ? targetSize : QSize(1024, 1024);
  if (stage) {
    stage(Stage::OriginalDecode);
  }
  auto image = decodeBounded(file, bound, HolonightImages::OrientationPolicy::Apply, cancelled);
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  if (stage) {
    stage(Stage::OriginalDecoded);
  }
  return cancelled.load() ? Result{{}, Outcome::Cancelled} : image;
}

}  // namespace ThumbnailService
