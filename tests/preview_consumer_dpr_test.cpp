#include "directory_controller.h"
#include "directory_fixtures.h"
#include "engine_setup.h"
#include "preview_fixtures.h"
#include "preview_service_test_access.h"
#include "quick_look_presentation_model.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>

TEST(PreviewConsumers, UseWindowDprWhenScreenDprDiffersAndTrackChanges) {
  if (QGuiApplication::platformName() != QStringLiteral("offscreen")) {
    GTEST_SKIP() << "Deterministic offscreen binding regression; not native acceptance";
  }
  DirectoryController controller;
  QQmlEngine engine;
  initializeFilesEngine(engine);
  QQmlComponent component(&engine);
  // A redirected window has a controlled effective DPR independent of Screen DPR.
  // No compositor interaction or private Qt API is needed.
  QQuickRenderControl renderControl;
  QQuickWindow window(&renderControl);
  window.resize(1000, 700);
  QImage buffer(2000, 1400, QImage::Format_ARGB32_Premultiplied);
  component.setData(R"(
    import QtQuick
    import HolonightFiles
    Item {
      id: testWindow
      required property DirectoryController controller
      width: 1000; height: 700
      PreviewPane { width: 320; height: 600; controller: testWindow.controller }
      QuickLookOverlay { controller: testWindow.controller }
    }
  )",
                    QUrl());
  std::unique_ptr<QObject> content(
      component.createWithInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}}));
  ASSERT_NE(content, nullptr) << component.errorString().toStdString();
  auto* item = qobject_cast<QQuickItem*>(content.get());
  ASSERT_NE(item, nullptr);
  item->setParent(window.contentItem());
  item->setParentItem(window.contentItem());
  QTemporaryDir directory(files_test::fixturePattern("consumer-dpr"));
  ASSERT_TRUE(directory.isValid());
  files_test::writeJpegWithExif(directory, "photo.jpg");
  controller.open(directory.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && !controller.preview()->busy(); }));
  ASSERT_TRUE(controller.handleKey(" "));
  renderControl.polishItems();
  auto* pane = content->findChild<QQuickItem*>(QStringLiteral("previewImageArea"));
  auto* quick = content->findChild<QuickLookPresentationModel*>(QStringLiteral("quickLookPresentation"));
  ASSERT_NE(pane, nullptr);
  ASSERT_NE(quick, nullptr);
  for (const qreal ratio : {1.25, 1.5, 2.0, 1.0}) {
    auto target = QQuickRenderTarget::fromPaintDevice(&buffer);
    target.setDevicePixelRatio(ratio);
    window.setRenderTarget(target);
    // Redirected targets do not receive a platform surface-DPR notification.
    QEvent changed(QEvent::DevicePixelRatioChange);
    QCoreApplication::sendEvent(&window, &changed);
    renderControl.polishItems();
    ASSERT_EQ(window.effectiveDevicePixelRatio(), ratio);
    ASSERT_TRUE(QTest::qWaitFor([&] {
      return PreviewServiceTestAccess::paneSize(*controller.preview()).width() == qRound(pane->width() * ratio) &&
             quick->devicePixelRatio() == ratio;
    })) << "ratio "
        << ratio << "; pane width " << pane->width() << "; request "
        << PreviewServiceTestAccess::paneSize(*controller.preview()).width() << "; quick DPR "
        << quick->devicePixelRatio();
  }
}
