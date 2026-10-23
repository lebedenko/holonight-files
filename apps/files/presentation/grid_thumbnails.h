#pragma once

#include <QFileInfo>
#include <QObject>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <HolonightFileBrowser/preview_quick/thumbnail_provider.h>
#include <cmath>
#include <holonight_images/image.h>
#include <sys/stat.h>

class GridThumbnails : public QObject {
  Q_OBJECT
  QML_ELEMENT
  QML_SINGLETON
 public:
  explicit GridThumbnails(QObject* parent = nullptr) : QObject(parent) {}

  // QML invokes these methods through the engine-owned singleton instance.
  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  Q_INVOKABLE [[nodiscard]] bool eligible(const QString& name, bool isDir, bool statFailed, quint32 mode) const {
    if (isDir || statFailed || !S_ISREG(mode)) {
      return false;
    }
    const auto suffix = QFileInfo(name).suffix().toLower();
    static const auto suffixes = HolonightImages::supportedSuffixes();
    return !suffix.isEmpty() && suffix != QStringLiteral("pdf") &&
           (suffix == QStringLiteral("svg") || suffixes.contains(suffix));
  }

  // NOLINTNEXTLINE(readability-convert-member-functions-to-static)
  Q_INVOKABLE [[nodiscard]] QUrl source(const QByteArray& nativePath, qreal devicePixelRatio, int generation) const {
    const int pixels = static_cast<int>(std::ceil(64 * devicePixelRatio));
    return {QStringLiteral("image://thumbnail/") + ThumbnailProvider::idForNative(nativePath, pixels, generation)};
  }
};
