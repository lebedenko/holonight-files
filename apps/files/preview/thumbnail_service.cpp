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
  if (cached.isNull()) {
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
QImage decodeBounded(QFile& file, QSize bound, QString* errorOut) {
  const std::atomic_bool cancelled{false};
  auto result = HolonightImages::decode(
      file, {.limits = kPreviewImageLimits, .bound = bound, .orientation = HolonightImages::OrientationPolicy::Ignore},
      cancelled);
  if (result.outcome != HolonightImages::Outcome::Success && errorOut != nullptr) {
    *errorOut = result.outcome == HolonightImages::Outcome::ResourceLimit
                    ? QObject::tr("Image exceeds the decode memory limit.")
                    : QObject::tr("The image is damaged or could not be decoded.");
  }
  return result.image;
}

void writeCacheEntry(const QString& cachePath, const QImage& image, const QString& uri, const QFileInfo& sourceInfo,
                     const QString& revision) {
  QImage tagged = image;
  tagged.setText(QStringLiteral("Files::Revision"), revision);
  tagged.setText(QStringLiteral("Thumb::URI"), uri);
  tagged.setText(QStringLiteral("Thumb::MTime"), QString::number(sourceInfo.lastModified().toSecsSinceEpoch()));
  tagged.setText(QStringLiteral("Thumb::Size"), QString::number(sourceInfo.size()));
  QSaveFile file(cachePath);
  if (!file.open(QIODevice::WriteOnly)) {
    return;
  }
  file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  if (!tagged.save(&file, "PNG")) {
    file.cancelWriting();
    return;
  }
  if (file.commit()) {
    QFile::setPermissions(cachePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
  }
}

}  // namespace

QImage lookupOrDecode(const QString& path, QString* errorOut) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (errorOut != nullptr) {
      *errorOut = file.errorString();
    }
    return {};
  }
  return lookupOrDecode(file, path, {}, errorOut);
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

QImage lookup(QFile& file, const QString& path, const QString& revision, Tier selected, QSize required) {
  const QFileInfo sourceInfo(file);
  const auto uri = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
  for (const auto tier : kTiers) {
    if (static_cast<int>(tier) < static_cast<int>(selected)) {
      continue;
    }
    QFile cachedFile(cacheDir(tier) + QLatin1Char('/') + cacheKeyFor(uri) + QStringLiteral(".png"));
    if (!cachedFile.open(QIODevice::ReadOnly)) {
      continue;
    }
    const std::atomic_bool cancelled{false};
    const auto inspection = HolonightImages::inspect(cachedFile, kPreviewImageLimits, cancelled,
                                                     HolonightImages::OrientationPolicy::Ignore, false);
    if (inspection.outcome != HolonightImages::Outcome::Success) {
      continue;
    }
    const auto size = inspection.sourceSize;
    const auto limit = static_cast<int>(tier);
    if (size.width() < required.width() || size.height() < required.height() || size.width() > limit ||
        size.height() > limit) {
      continue;
    }
    auto cached = decodeBounded(cachedFile, {limit, limit}, nullptr);
    if (cacheEntryValid(cached, uri, sourceInfo) && cached.width() >= required.width() &&
        cached.height() >= required.height() &&
        (revision.isEmpty() || cached.text(QStringLiteral("Files::Revision")) == revision)) {
      return cached;
    }
  }
  return {};
}

QImage lookupOrDecode(QFile& file, const QString& path, const QString& revision, Tier tier, QSize required,
                      QString* errorOut) {
  auto cached = lookup(file, path, revision, tier, required);
  if (!cached.isNull()) {
    return cached;
  }
  const auto extent = static_cast<int>(tier);
  auto decoded = decodeBounded(file, {extent, extent}, errorOut);
  if (decoded.isNull()) {
    return {};
  }
  const auto uri = QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
  const auto dir = cacheDir(tier);
  ensureCacheDir(dir);
  writeCacheEntry(dir + QLatin1Char('/') + cacheKeyFor(uri) + QStringLiteral(".png"), decoded, uri, QFileInfo(file),
                  revision);
  return decoded;
}

QImage lookupOrDecode(QFile& file, const QString& path, const QString& revision, QString* errorOut) {
  file.seek(0);
  QSize source;
  {
    const std::atomic_bool cancelled{false};
    source = HolonightImages::inspect(file, kPreviewImageLimits, cancelled, HolonightImages::OrientationPolicy::Ignore,
                                      false)
                 .sourceSize;
  }
  if (!source.isValid() || source.isEmpty()) {
    return decodeScaled(file, {128, 128}, errorOut);
  }
  return lookupOrDecode(file, path, revision, Tier::Normal, requiredSize(source, {128, 128}), errorOut);
}

QImage decodeScaled(const QString& path, QSize targetSize, QString* errorOut) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (errorOut != nullptr) {
      *errorOut = file.errorString();
    }
    return {};
  }
  return decodeScaled(file, targetSize, errorOut);
}

QImage decodeScaled(QFile& file, QSize targetSize, QString* errorOut) {
  const auto bound = (targetSize.isValid() && !targetSize.isEmpty()) ? targetSize : QSize(1024, 1024);
  return decodeBounded(file, bound, errorOut);
}

}  // namespace ThumbnailService
