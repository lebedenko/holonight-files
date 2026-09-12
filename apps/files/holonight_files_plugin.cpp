#include "icon_image_provider.h"

#include <QQmlEngine>
#include <QQmlEngineExtensionPlugin>
#include <QtCore/qtsymbolmacros.h>

// Name fixed by qmltyperegistrar for URI HolonightFiles.
QT_DECLARE_EXTERN_SYMBOL_VOID(qml_register_types_HolonightFiles)  // NOLINT(readability-identifier-naming)

// Hand-written replacement for the plugin qt_add_qml_module() would otherwise generate
// (NO_GENERATE_PLUGIN_SOURCE), so every engine importing HolonightFiles gets image://icon/
// registered automatically — the application and each test-constructed engine alike
// (docs/sdd/main-view-icons/DESIGN.md §5.4). The generated plugin's resource keep-alives are not
// needed: this module's QML and qmlcache resources are linked as object libraries, which the
// linker never drops.
class HolonightFilesPlugin : public QQmlEngineExtensionPlugin {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid)
 public:
  explicit HolonightFilesPlugin(QObject* parent = nullptr) : QQmlEngineExtensionPlugin(parent) {
    QT_KEEP_SYMBOL(qml_register_types_HolonightFiles)
  }
  void initializeEngine(QQmlEngine* engine, const char* uri) override {
    QQmlEngineExtensionPlugin::initializeEngine(engine, uri);
    engine->addImageProvider(QStringLiteral("icon"), new IconImageProvider);  // engine takes ownership
  }
};

#include "holonight_files_plugin.moc"
