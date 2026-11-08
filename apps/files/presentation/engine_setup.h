#pragma once
#include <HolonightFileBrowser/preview/runtime_options.h>
class QQmlEngine;
// Call before loading any application QML, for each engine independently.
void initializeFilesEngine(QQmlEngine& engine);

void initializeFilesEngine(QQmlEngine& engine, HolonightFileBrowser::ImageRuntimeOptions runtime);
