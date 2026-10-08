#include "engine_setup.h"

#include <QQmlEngine>

#include <HolonightFileBrowser/quick.h>
void initializeFilesEngine(QQmlEngine& engine) { HolonightFileBrowser::initializeEngine(engine); }
