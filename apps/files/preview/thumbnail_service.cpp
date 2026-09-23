#include "thumbnail_service.h"

#include "image_policy.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <array>

namespace ThumbnailService {
namespace {

using HolonightImages::Outcome;

constexpr std::array kTiers = {Tier::Normal, Tier::Large, Tier::XLarge, Tier::XXLarge};
QString cacheKeyFor(const QString& uri) {
  return QString::fromLatin1(QCryptographicHash::hash(uri.toUtf8(), QCryptographicHash::Md5).toHex());
}

// Falls back to ~/.cache when $XDG_CACHE_HOME is unset, per REQ-F-017 (QStandardPaths already
// implements the freedesktop base-directory fallback, no need to read the environment ourselves).
QString cacheDir(Tier tier) {
  QString name;
  switch (tier) {
    case Tier::Normal:
      name = QStringLiteral("normal");
      break;
    case Tier::Large:
      name = QStringLiteral("large");
      break;
    case Tier::XLarge:
      name = QStringLiteral("x-large");
      break;
    case Tier::XXLarge:
      name = QStringLiteral("xx-large");
      break;
  }
  return QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/thumbnails/") + name;
}

void ensureCacheDir(const QString& dir) {
  QDir().mkpath(dir);
  const auto privateOwnerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
  QFile::setPermissions(dir, privateOwnerOnly);
  QFile::setPermissions(QFileInfo(dir).absolutePath(), privateOwnerOnly);
}

// QImageReader::text()/textKeys() only reliably sees a PNG's tEXt chunks after a full read() —
// canRead()'s header-only peek does not populate them (keys containing "::" come back empty, or
// silently truncated at the first colon). QImage's own constructor performs a full decode, so it
// sees the chunks correctly; validation therefore loads the candidate image fully rather than
// peeking its header, at the cost of one bounded cache-image decode per candidate.
bool cacheEntryValid(const QImage& cached, const QString& uri, const QFileInfo& sourceInfo) {
  if (cached.isNull() || cached.text(QStringLiteral("Files::OrientationPolicy")) != QStringLiteral("applied-v1")) {
    return false;
  }
  if (cached.text(QStringLiteral("Thumb::URI")) != uri) {
    return false;
  }
  bool mtimeOk = false;
  const qint64 mtime = cached.text(QStringLiteral("Thumb::MTime")).toLongLong(&mtimeOk);
  if (!mtimeOk || mtime != sourceInfo.lastModified().toSecsSinceEpoch()) {
    return false;
  }
  bool sizeOk = false;
  const qint64 size = cached.text(QStringLiteral("Thumb::Size")).toLongLong(&sizeOk);
  return sizeOk && size == sourceInfo.size();
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

std::optional<Result> readCacheEntry(QFile& cachedFile, Tier tier, QSize required, const std::atomic_bool& cancelled,
                                     const StageCallback& stage) {
  if (stage) {
    stage(Stage::CacheInspect);
  }
  const auto inspection = HolonightImages::inspect(cachedFile, kPreviewImageLimits, cancelled,
                                                   HolonightImages::OrientationPolicy::Ignore, false);
  if (inspection.outcome == Outcome::Cancelled || cancelled.load()) {
    return Result{{}, Outcome::Cancelled};
  }
  if (stage) {
    stage(Stage::CacheInspected);
  }
  if (cancelled.load()) {
    return Result{{}, Outcome::Cancelled};
  }
  if (inspection.outcome != Outcome::Success) {
    return std::nullopt;
  }
  const auto size = inspection.sourceSize;
  const auto limit = static_cast<int>(tier);
  if (size.width() < required.width() || size.height() < required.height() || size.width() > limit ||
      size.height() > limit) {
    return std::nullopt;
  }
  if (stage) {
    stage(Stage::CacheDecode);
  }
  auto result = decodeBounded(cachedFile, {limit, limit}, HolonightImages::OrientationPolicy::Ignore, cancelled);
  if (result.outcome == Outcome::Success || result.outcome == Outcome::Cancelled) {
    return result;
  }
  return std::nullopt;
}

void writeCacheEntry(const QString& cachePath, const QImage& image, const QString& uri, const QFileInfo& sourceInfo,
                     const QString& revision, const std::atomic_bool& cancelled, const StageCallback& stage) {
  if (cancelled.load()) {
    return;
  }
  QImage tagged = image;
  tagged.setText(QStringLiteral("Files::Revision"), revision);
  tagged.setText(QStringLiteral("Files::OrientationPolicy"), QStringLiteral("applied-v1"));
  tagged.setText(QStringLiteral("Thumb::URI"), uri);
  tagged.setText(QStringLiteral("Thumb::MTime"), QString::number(sourceInfo.lastModified().toSecsSinceEpoch()));
  tagged.setText(QStringLiteral("Thumb::Size"), QString::number(sourceInfo.size()));
  QSaveFile file(cachePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  if (cancelled.load() || !tagged.save(&file, "PNG")) {
    file.cancelWriting();
    return;
  }
  if (stage) {
    stage(Stage::BeforeCommit);
  }
  if (cancelled.load()) {
    file.cancelWriting();
    return;
  }
  if (file.commit()) {
    QFile::setPermissions(cachePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  }
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

QSize requiredSize(QSize source, QSize bound) {
  if (!bound.isValid() || bound.isEmpty()) {
    bound = QSize(1024, 1024);
  }
  if (!source.isValid() || source.isEmpty()) {
    return bound;
  }
  return source.scaled(bound.boundedTo(source), Qt::KeepAspectRatio).expandedTo(QSize(1, 1));
}

std::optional<Tier> tierForSize(QSize required) {
  if (!required.isValid() || required.isEmpty()) {
    return std::nullopt;
  }
  for (const auto tier : kTiers) {
    if (required.width() <= static_cast<int>(tier) && required.height() <= static_cast<int>(tier)) {
      return tier;
    }
  }
  return std::nullopt;
}

std::optional<Result> lookup(QFile& file, const QString& path, const QString& revision, Tier selected, QSize required,
                             const std::atomic_bool& cancelled, const StageCallback& stage) {
  if (cancelled.load()) {
    return Result{{}, Outcome::Cancelled};
  }
  const QFileInfo sourceInfo(file);
  const auto uri = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
  for (const auto tier : kTiers) {
    if (cancelled.load()) {
      return Result{{}, Outcome::Cancelled};
    }
    if (static_cast<int>(tier) < static_cast<int>(selected)) {
      continue;
    }
    QFile cachedFile(cacheDir(tier) + QLatin1Char('/') + cacheKeyFor(uri) + QStringLiteral(".png"));
    if (!cachedFile.open(QIODevice::ReadOnly)) {
      continue;
    }
    auto cached = readCacheEntry(cachedFile, tier, required, cancelled, stage);
    if (cancelled.load() || (cached && cached->outcome == Outcome::Cancelled)) {
      return Result{{}, Outcome::Cancelled};
    }
    if (cached && cacheEntryValid(cached->image, uri, sourceInfo) && cached->image.width() >= required.width() &&
        cached->image.height() >= required.height() &&
        (revision.isEmpty() || cached->image.text(QStringLiteral("Files::Revision")) == revision)) {
      return cancelled.load() ? Result{{}, Outcome::Cancelled} : *cached;
    }
  }
  return std::nullopt;
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
  const auto uri = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
  const auto dir = cacheDir(tier);
  if (cancelled.load()) {
    return {{}, Outcome::Cancelled};
  }
  ensureCacheDir(dir);
  writeCacheEntry(dir + QLatin1Char('/') + cacheKeyFor(uri) + QStringLiteral(".png"), decoded.image, uri,
                  QFileInfo(file), revision, cancelled, stage);
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
