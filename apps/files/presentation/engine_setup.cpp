#include "engine_setup.h"

#include <QQmlEngine>

#include <HolonightFileBrowser/preview_quick/thumbnail_provider.h>
#include <HolonightFileBrowser/quick.h>
void initializeFilesEngine(QQmlEngine& engine) {
  initializeFilesEngine(engine, HolonightFileBrowser::defaultImageRuntimeOptions());
}
void initializeFilesEngine(QQmlEngine& engine, HolonightFileBrowser::ImageRuntimeOptions runtime) {
  HolonightFileBrowser::initializeEngine(engine);
  if (engine.imageProvider(QStringLiteral("thumbnail")) == nullptr) {
    engine.addImageProvider(QStringLiteral("thumbnail"), new ThumbnailProvider(std::move(runtime)));
  }
}
