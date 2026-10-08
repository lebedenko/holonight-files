#include <QQmlEngine>

#include <HolonightFileBrowser/icon_image_provider.h>
#include <HolonightFileBrowser/quick.h>

void HolonightFileBrowser::initializeEngine(QQmlEngine& engine) {
  if (engine.imageProvider(QStringLiteral("icon")) == nullptr) {
    engine.addImageProvider(QStringLiteral("icon"), new IconImageProvider);
  }
}
