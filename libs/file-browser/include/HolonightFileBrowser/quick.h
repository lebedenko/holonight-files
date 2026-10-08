#pragma once

class QQmlEngine;
namespace HolonightFileBrowser {
// Retain the module's linked resources and install its themed candidate-chain image provider.
// Call before loading QML; an existing image://icon provider is preserved.
void initializeEngine(QQmlEngine& engine);
}  // namespace HolonightFileBrowser
