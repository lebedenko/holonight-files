#include "icon_image_provider.h"

#include "icon_name_resolver.h"

#include <QIcon>
namespace {

// Cost is in pixels; 4096 KiB of ARGB covers thousands of 20 px row icons at 1.5x scale plus the
// preview pane's 128 px variants without growing with directory size.
constexpr qsizetype kCacheCostLimit = qsizetype{4096} * 1024 / 4;
constexpr int kDefaultExtent = 20;

QString normalizedChain(const QString& chain) {
  return chain.split(IconNameResolver::kChainSeparator, Qt::SkipEmptyParts).join(IconNameResolver::kChainSeparator);
}
}  // namespace

IconImageProvider::IconImageProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap), cache_(kCacheCostLimit) {}

QPixmap IconImageProvider::requestPixmap(const QString& chain, QSize* size, const QSize& requestedSize) {
  const auto candidates = normalizedChain(chain);
  // Qt Quick already multiplies HnIcon's logical sourceSize by the window's effective device pixel
  // ratio, and re-requests when that ratio changes (REQ-F-019), so requestedSize is physical pixels.
  const QSize extent =
      requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(kDefaultExtent, kDefaultExtent);
  const auto sizeSuffix = QStringLiteral("@%1x%2").arg(extent.width()).arg(extent.height());
  for (const auto& name : candidates.split(IconNameResolver::kChainSeparator, Qt::SkipEmptyParts)) {
    const auto key = name + sizeSuffix;
    QPixmap pixmap;
    if (const auto* cached = cache_.object(key)) {
      pixmap = *cached;
    } else {
      ++theme_lookup_count_for_test_;
      const auto icon = QIcon::fromTheme(name);
      if (!icon.isNull()) {
        // Explicit ratio 1: extent is already physical, and QIcon::pixmap(QSize) would otherwise
        // multiply it by the application-wide ratio a second time.
        pixmap = icon.pixmap(extent, 1.0);
        if (!pixmap.isNull() && pixmap.size() != extent) {
          // Theme lacks a scalable or exact-size variant; never hand QML an undersized pixmap.
          pixmap = pixmap.scaled(extent, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
      }
      // A null pixmap still costs 1, so misses are remembered as well as hits.
      cache_.insert(key, new QPixmap(pixmap), qMax<qsizetype>(1, qsizetype{extent.width()} * extent.height()));
    }
    if (!pixmap.isNull()) {
      if (size != nullptr) {
        *size = pixmap.size();
      }
      return pixmap;
    }
  }
  return {};
}
