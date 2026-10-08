#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>

#include <HolonightFileBrowser/directory_proxy_model.h>
#include <HolonightFileBrowser/directory_reader.h>
#ifdef QUICK_CONSUMER
#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>

#include <HolonightFileBrowser/quick.h>
#endif
#include <iostream>
#include <memory>

int main(int argc, char** argv) {
#ifdef QUICK_CONSUMER
  QGuiApplication app(argc, argv);
#else
  QCoreApplication app(argc, argv);
#endif
  QTemporaryDir dir;
  if (!dir.isValid()) {
    return 1;
  }
  QFile file(dir.filePath("selected.txt"));
  if (!file.open(QIODevice::WriteOnly)) {
    return 2;
  }
  file.close();
  HolonightFileBrowser::DirectoryReader reader;
  DirectoryProxyModel sorted;
  sorted.setSourceModel(&reader);
  QEventLoop loop;
  QObject::connect(&reader, &HolonightFileBrowser::DirectoryReader::changed, &loop, [&] {
    if (!reader.scanning()) {
      loop.quit();
    }
  });
  // A deterministic test deadline, never an application user-interaction timeout.
  QTimer::singleShot(5000, &loop, &QEventLoop::quit);
  reader.load(dir.path());
  loop.exec();
  if (reader.scanning() || !reader.directoryError().isEmpty() || sorted.rowCount() != 2) {
    return 3;
  }
#ifdef QUICK_CONSUMER
  QQmlEngine engine;
#ifndef PLUGIN_ONLY
  HolonightFileBrowser::initializeEngine(engine);
#endif
  QQmlComponent component(&engine);
  component.setData(R"(
import QtQuick
import Holonight.FileBrowser
DirectoryListing {
  width: 320; height: 200
  model: DirectorySortModel { sourceModel: DirectoryReader {} }
  iconProvider: ""
  selectedPaths: ["/explicit"]
  currentIndex: -1
}
)",
                    QUrl("file:///installed-consumer.qml"));
  std::unique_ptr<QObject> object(component.create());
  if (!object) {
    std::cerr << component.errorString().toStdString();
    return 4;
  }
  auto* view = qobject_cast<QQuickItem*>(object.get());
  if (!view) return 5;
  view->setProperty("model", QVariant::fromValue(&sorted));
  view->setProperty("currentIndex", 1);
  QQuickWindow window;
  window.resize(320, 200);
  view->setParentItem(window.contentItem());
  window.show();
  QCoreApplication::processEvents();
  if (view->property("selectedPaths").toStringList() != QStringList{"/explicit"}) return 6;
  if (view->property("count").toInt() != 2) return 7;
  view->setParentItem(nullptr);
#endif
  return 0;
}
