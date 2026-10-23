#include "directory_controller.h"
#include "directory_fixtures.h"
#include "engine_setup.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTest>

#include <HolonightFileBrowser/preview_quick/thumbnail_provider.h>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>

namespace {
QQuickItem* namedThumbnail(QQuickItem* root, const QString& name) {
  for (auto* child : root->childItems()) {
    if (child->objectName() == "directoryIconDelegate" && child->property("name").toString() == name) {
      return child->findChild<QQuickItem*>("gridThumbnail");
    }
    if (auto* found = namedThumbnail(child, name)) {
      return found;
    }
  }
  return nullptr;
}
}  // namespace

TEST(GridThumbnails, GridDecodesImagesKeepsFallbackAndRefreshesMetadata) {
  QTemporaryDir dir(files_test::fixturePattern("grid-thumbnails"));
  ASSERT_TRUE(dir.isValid());
  QImage image(200, 100, QImage::Format_ARGB32);
  image.fill(Qt::red);
  ASSERT_TRUE(image.save(dir.filePath("wide.PNG")));
  QImage small(16, 8, QImage::Format_ARGB32);
  small.fill(Qt::transparent);
  ASSERT_TRUE(small.save(dir.filePath("small.png")));
  files_test::writeFile(dir, "vector.svg",
                        "<svg xmlns='http://www.w3.org/2000/svg' width='120' height='60'><rect width='120' height='60' "
                        "fill='blue'/></svg>");
  files_test::writeFile(dir, "corrupt.png", "broken");
  files_test::writeFile(dir, "notes.txt", "text");
  ASSERT_TRUE(QFile::link(dir.filePath("small.png"), dir.filePath("link.png")));
  DirectoryController controller;
  QQmlEngine engine;
  initializeFilesEngine(engine);
  QQuickWindow window;
  window.resize(1000, 700);
  QQmlComponent component(&engine);
  component.setData("import QtQuick\nimport HolonightFiles\nIconPresentation { width: 1000; height: 700 }", QUrl());
  std::unique_ptr<QObject> content(
      component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}}));
  ASSERT_NE(content, nullptr) << component.errorString().toStdString();
  auto* item = qobject_cast<QQuickItem*>(content.get());
  ASSERT_NE(item, nullptr);
  item->setParentItem(window.contentItem());
  window.show();
  controller.open(dir.path());
  auto thumbnail = [&](const QString& name) { return namedThumbnail(item, name); };
  ASSERT_TRUE(QTest::qWaitFor([&] { return thumbnail("link.png") && thumbnail("notes.txt"); }));
  for (const auto* name : {"wide.PNG", "small.png", "vector.svg", "link.png"}) {
    ASSERT_TRUE(QTest::qWaitFor([&] { return thumbnail(name)->property("status").toInt() == 1; })) << name;
    EXPECT_TRUE(thumbnail(name)->isVisible());
    auto* area = thumbnail(name)->parentItem();
    EXPECT_FALSE(area->findChild<QQuickItem*>("themeFileIcon")->isVisible());
    EXPECT_FALSE(area->findChild<QQuickItem*>("fallbackFileIcon")->isVisible());
  }
  EXPECT_EQ(thumbnail("wide.PNG")->property("paintedWidth").toReal(), 64);
  EXPECT_EQ(thumbnail("wide.PNG")->property("paintedHeight").toReal(), 32);
  EXPECT_EQ(thumbnail("small.png")->width(), 16);
  EXPECT_EQ(thumbnail("small.png")->height(), 8);
  ASSERT_TRUE(QTest::qWaitFor([&] { return thumbnail("corrupt.png")->property("status").toInt() == 3; }));
  for (const auto* name : {"notes.txt", "corrupt.png"}) {
    auto* thumb = thumbnail(name);
    EXPECT_FALSE(thumb->isVisible());
    auto* area = thumb->parentItem();
    EXPECT_TRUE(area->findChild<QQuickItem*>("themeFileIcon")->isVisible() ||
                area->findChild<QQuickItem*>("fallbackFileIcon")->isVisible());
  }
  EXPECT_TRUE(thumbnail("notes.txt")->property("source").toUrl().isEmpty());
  const auto oldSource = thumbnail("small.png")->property("source").toUrl();
  QImage replacement(20, 30, QImage::Format_ARGB32);
  replacement.fill(Qt::green);
  ASSERT_TRUE(replacement.save(dir.filePath("small.png")));
  files_test::writeFile(dir, "refresh-trigger.txt");
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return thumbnail("small.png") && thumbnail("small.png")->property("source").toUrl() != oldSource; }));
  ASSERT_TRUE(QTest::qWaitFor([&] { return thumbnail("small.png")->property("status").toInt() == 1; }));
  EXPECT_EQ(thumbnail("small.png")->width(), 20);
  EXPECT_EQ(thumbnail("small.png")->height(), 30);
}

TEST(GridThumbnails, LoadingFallbackAndDestructionCancelPendingRequests) {
  QTemporaryDir dir(files_test::fixturePattern("grid-pending"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeFile(dir, "pending.png");
  std::atomic_bool started = false;
  std::atomic_bool cancelled = false;
  std::atomic_bool release = false;
  DirectoryController controller;
  controller.setIconView(true);
  QQmlEngine engine;
  engine.addImageProvider("thumbnail",
                          new ThumbnailProvider([&](const ThumbnailRequest&, const std::atomic_bool& stop) {
                            started = true;
                            while (!stop.load() && !release.load()) {
                              QThread::msleep(1);
                            }
                            cancelled = stop.load();
                            return ThumbnailResult{};
                          }));
  const QScopeGuard cleanup([&] { release = true; });
  initializeFilesEngine(engine);
  QQuickWindow window;
  window.resize(800, 600);
  QQmlComponent component(&engine);
  component.setData("import QtQuick\nimport HolonightFiles\nDirectoryListing { width: 800; height: 600 }", QUrl());
  std::unique_ptr<QObject> content(
      component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}}));
  ASSERT_NE(content, nullptr) << component.errorString().toStdString();
  auto* item = qobject_cast<QQuickItem*>(content.get());
  item->setParentItem(window.contentItem());
  window.show();
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return started.load(); }));
  auto* thumb = namedThumbnail(item, "pending.png");
  ASSERT_NE(thumb, nullptr);
  EXPECT_EQ(thumb->property("status").toInt(), 2);
  EXPECT_FALSE(thumb->isVisible());
  auto* area = thumb->parentItem();
  EXPECT_TRUE(area->findChild<QQuickItem*>("themeFileIcon")->isVisible() ||
              area->findChild<QQuickItem*>("fallbackFileIcon")->isVisible());
  controller.setIconView(false);
  ASSERT_TRUE(QTest::qWaitFor([&] { return cancelled.load(); }));
  release = true;
  controller.setIconView(true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return namedThumbnail(item, "pending.png") != nullptr; }));
}

TEST(GridThumbnails, RequestsTrackEffectiveWindowDevicePixelRatio) {
  QTemporaryDir dir(files_test::fixturePattern("grid-dpr"));
  ASSERT_TRUE(dir.isValid());
  QImage image(400, 200, QImage::Format_ARGB32);
  image.fill(Qt::red);
  ASSERT_TRUE(image.save(dir.filePath("image.png")));
  DirectoryController controller;
  QQmlEngine engine;
  initializeFilesEngine(engine);
  QQuickRenderControl renderControl;
  QQuickWindow window(&renderControl);
  window.resize(800, 600);
  QImage buffer(1600, 1200, QImage::Format_ARGB32_Premultiplied);
  QQmlComponent component(&engine);
  component.setData("import QtQuick\nimport HolonightFiles\nIconPresentation { width: 800; height: 600 }", QUrl());
  std::unique_ptr<QObject> content(
      component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}}));
  ASSERT_NE(content, nullptr) << component.errorString().toStdString();
  auto* item = qobject_cast<QQuickItem*>(content.get());
  item->setParentItem(window.contentItem());
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] {
    renderControl.polishItems();
    return namedThumbnail(item, "image.png") != nullptr;
  }));
  for (qreal ratio : {1.25, 1.5, 1.5625, 2.0, 1.0}) {
    auto target = QQuickRenderTarget::fromPaintDevice(&buffer);
    target.setDevicePixelRatio(ratio);
    window.setRenderTarget(target);
    QEvent changed(QEvent::DevicePixelRatioChange);
    QCoreApplication::sendEvent(&window, &changed);
    renderControl.polishItems();
    auto* thumbnail = namedThumbnail(item, "image.png");
    ASSERT_NE(thumbnail, nullptr);
    EXPECT_EQ(thumbnail->property("source").toUrl().path().split('/')[1].toInt(),
              static_cast<int>(std::ceil(64 * ratio)));
  }
}
