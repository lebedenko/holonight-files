#include "engine_setup.h"

#include "icon_image_provider.h"

#include <QQmlEngine>
void initializeFilesEngine(QQmlEngine& engine) {
  if (engine.imageProvider(QStringLiteral("icon")) == nullptr) {
    engine.addImageProvider(QStringLiteral("icon"), new IconImageProvider);
  }
}
