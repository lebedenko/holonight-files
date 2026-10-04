#include "directory_controller.h"
#include "directory_fixtures.h"
#include "engine_setup.h"
#include "icon_theme_fixtures.h"
#include "preview_fixtures.h"
#include "preview_service_test_access.h"
#include "quick_look_presentation_model.h"

#include <QGuiApplication>
#include <QMetaProperty>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
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

TEST(PreviewConsumers, SidebarDelaysEveryFallbackAndResetsForEachSelection) {
  DirectoryController controller;
  auto& preview = *controller.preview();
  QQmlEngine engine;
  initializeFilesEngine(engine);
  QQmlComponent component(&engine);
  component.setData(R"(
    import QtQuick
    import HolonightFiles
    PreviewPane { width: 320; height: 600 }
  )",
                    QUrl());
  std::unique_ptr<QObject> pane(
      component.createWithInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}}));
  ASSERT_NE(pane, nullptr) << component.errorString().toStdString();
  auto* area = pane->findChild<QQuickItem*>("previewImageArea");
  ASSERT_NE(area, nullptr);
  const auto hidden = [&] {
    for (const auto* name : {"previewThemeIcon", "previewFallbackIcon", "previewIconFailurePlaceholder"}) {
      auto* icon = pane->findChild<QQuickItem*>(name);
      EXPECT_NE(icon, nullptr);
      if (icon != nullptr) {
        EXPECT_FALSE(icon->isVisible()) << name;
      }
    }
  };
  const auto fallback = [&] { return pane->property("showIconFallback").toBool(); };
  // A folder gives a real selection and no worker. Synthetic worker states isolate timer behavior.
  preview.setTarget("/folder", true, -1, {}, 0040755, false, {}, "folder");
  EXPECT_TRUE(fallback());
  int revision = 0;
  const auto begin = [&] {
    PreviewServiceTestAccess::presentationSelection(preview, "new-" + QString::number(++revision));
    EXPECT_FALSE(fallback());
    hidden();
  };
  begin();
  QTest::qWait(60);
  EXPECT_FALSE(fallback());
  hidden();
  // Navigation before the old deadline restarts the full interval, even for the same path.
  begin();
  QTest::qWait(100);
  EXPECT_FALSE(fallback());
  ASSERT_TRUE(QTest::qWaitFor(fallback, 500));
  EXPECT_TRUE(pane->property("fallbackDelayElapsed").toBool());
  // A thumbnail wins while metadata remains busy, including after the fallback was visible.
  QImage image(32, 32, QImage::Format_RGB32);
  image.fill(Qt::red);
  PreviewServiceTestAccess::presentationState(preview, true, image);
  EXPECT_TRUE(preview.busy());
  EXPECT_FALSE(fallback());
  hidden();
  // Size upgrades keep the pixels; they do not reset selection presentation.
  auto* item = qobject_cast<QQuickItem*>(pane.get());
  ASSERT_NE(item, nullptr);
  item->setWidth(400);
  EXPECT_TRUE(preview.hasImage());
  EXPECT_FALSE(fallback());
  EXPECT_TRUE(pane->property("fallbackDelayElapsed").toBool());
  begin();
  PreviewServiceTestAccess::presentationState(preview, true, image);  // Fast cache-like delivery.
  hidden();
  EXPECT_FALSE(pane->property("fallbackDelayElapsed").toBool());
  QTest::qWait(200);
  hidden();
  begin();
  PreviewServiceTestAccess::presentationState(preview, false);  // Completion without pixels.
  EXPECT_TRUE(fallback());
  EXPECT_FALSE(pane->property("fallbackDelayElapsed").toBool());
  begin();
  PreviewServiceTestAccess::presentationState(preview, false, {}, PreviewService::PreviewErrorKind::DecodeFailed);
  EXPECT_TRUE(fallback());
  EXPECT_FALSE(pane->property("fallbackDelayElapsed").toBool());
  begin();
  PreviewServiceTestAccess::presentationState(preview, false, {}, PreviewService::PreviewErrorKind::DecodeTimeout);
  EXPECT_TRUE(fallback());
  preview.clear();
  EXPECT_FALSE(fallback());
  hidden();
}

TEST(PreviewConsumers, SidebarWorkerThumbnailIsVisibleBeforeMetadataAndOnCacheRevisit) {
  QTemporaryDir dir(files_test::fixturePattern("sidebar-worker"));
  ASSERT_TRUE(dir.isValid());
  const auto path = files_test::writeJpegWithExif(dir);
  const QFileInfo info(path);
  DirectoryController controller;
  auto& preview = *controller.preview();
  QQmlEngine engine;
  initializeFilesEngine(engine);
  QQmlComponent component(&engine);
  component.setData(R"(
    import QtQuick
    import HolonightFiles
    PreviewPane { width: 320; height: 600 }
  )",
                    QUrl());
  std::unique_ptr<QObject> pane(
      component.createWithInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}}));
  ASSERT_NE(pane, nullptr) << component.errorString().toStdString();
  auto* thumbnail = pane->findChild<QQuickItem*>("previewThumbnail");
  ASSERT_NE(thumbnail, nullptr);
  bool sawPartial = false;
  std::atomic_int decodes = 0;
  QSemaphore release;
  PreviewServiceTestAccess::beforeDispatch(preview, [&] { release.acquire(); });
  const auto unblock = qScopeGuard([&] {
    release.release();
    EXPECT_TRUE(QTest::qWaitFor([&] { return !PreviewServiceTestAccess::activeJob(preview); }));
    PreviewServiceTestAccess::beforeFullDecode(preview, {});
  });
  const auto select = [&] {
    preview.setTarget(path, false, info.size(), info.lastModified(), 0100644, false, {}, "image-jpeg");
  };
  QObject::connect(&preview, &PreviewService::changed, pane.get(), [&] {
    if (preview.hasImage() && preview.busy()) {
      sawPartial = true;
      EXPECT_TRUE(thumbnail->isVisible());
      EXPECT_FALSE(pane->property("showIconFallback").toBool());
    }
  });
  select();
  EXPECT_FALSE(pane->property("showIconFallback").toBool());
  EXPECT_FALSE(thumbnail->isVisible());
  ASSERT_TRUE(QTest::qWaitFor([&] { return pane->property("showIconFallback").toBool(); }, 500));
  PreviewServiceTestAccess::beforeDispatch(preview, {});
  release.release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !preview.busy(); }));
  EXPECT_TRUE(sawPartial);
  EXPECT_TRUE(thumbnail->isVisible());
  preview.setTarget(dir.path(), true, -1, {}, 0040755, false, {}, "folder");
  EXPECT_TRUE(pane->property("showIconFallback").toBool());
  EXPECT_FALSE(thumbnail->isVisible());
  PreviewServiceTestAccess::beforeFullDecode(preview, [&] { ++decodes; });
  select();
  EXPECT_FALSE(pane->property("showIconFallback").toBool());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !preview.busy(); }));
  EXPECT_EQ(decodes.load(), 0);
  EXPECT_TRUE(thumbnail->isVisible());
  EXPECT_FALSE(pane->property("showIconFallback").toBool());
  QSignalSpy selections(&preview, &PreviewService::selectionChanged);
  qobject_cast<QQuickItem*>(pane.get())->setWidth(640);
  QTest::qWait(200);
  EXPECT_TRUE(thumbnail->isVisible());
  EXPECT_TRUE(selections.empty());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !preview.busy(); }));
}

TEST(PreviewConsumers, SidebarFadesOutgoingThumbnailWithoutRestoringStalePixels) {
  DirectoryController controller;
  auto& preview = *controller.preview();
  QQmlEngine engine;
  initializeFilesEngine(engine);
  QQmlComponent component(&engine);
  component.setData(R"(
    import QtQuick
    import HolonightFiles
    PreviewPane { width: 320; height: 600 }
  )",
                    QUrl());
  QQuickItem host;
  std::unique_ptr<QObject> pane(
      component.createWithInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}}));
  ASSERT_NE(pane, nullptr) << component.errorString().toStdString();
  auto* item = qobject_cast<QQuickItem*>(pane.get());
  auto* thumbnail = pane->findChild<QQuickItem*>("previewThumbnail");
  ASSERT_NE(item, nullptr);
  ASSERT_NE(thumbnail, nullptr);
  item->setParentItem(&host);
  QImage red(32, 32, QImage::Format_RGB32);
  red.fill(Qt::red);
  QImage blue(64, 64, QImage::Format_RGB32);
  blue.fill(Qt::blue);
  const auto pixels = [&] { return thumbnail->property("image").value<QImage>(); };
  const auto retained = [&] { return pane->property("displayedImage").isValid(); };
  const auto select = [&] {
    PreviewServiceTestAccess::presentationState(preview, true, {}, PreviewService::PreviewErrorKind::None, true);
  };
  int delivery = 0;
  const auto show = [&](const QImage& image) {
    SCOPED_TRACE(++delivery);
    PreviewServiceTestAccess::presentationState(preview, true, image);
    EXPECT_EQ(pixels(), image);
    EXPECT_EQ(thumbnail->opacity(), 1);
    EXPECT_TRUE(thumbnail->isVisible()) << " root=" << item->isVisible()
                                        << " parent=" << thumbnail->parentItem()->isVisible()
                                        << " hasEntry=" << preview.hasEntry();
  };
  const auto discarded = [&] {
    EXPECT_FALSE(retained());
    EXPECT_TRUE(pixels().isNull());
    EXPECT_FALSE(thumbnail->isVisible());
  };
  preview.setTarget("/folder", true, -1, {}, 0040755, false, {}, "folder");
  show(red);
  select();
  EXPECT_EQ(pixels(), red);
  ASSERT_TRUE(QTest::qWaitFor([&] { return thumbnail->opacity() < 1; }, 100));
  EXPECT_GT(thumbnail->opacity(), 0);
  auto* fade = pane->findChild<QObject*>("previewThumbnailFade");
  ASSERT_NE(fade, nullptr);
  EXPECT_EQ(fade->property("duration").toInt(), 120);
  const auto runningProperty = fade->metaObject()->property(fade->metaObject()->indexOfProperty("running"));
  QSignalSpy runningChanges(fade, runningProperty.notifySignal());
  ASSERT_TRUE(runningChanges.isValid());
  const auto opacity = thumbnail->opacity();
  select();  // Rapid navigation must not restore opacity or restart the fade.
  EXPECT_EQ(thumbnail->opacity(), opacity);
  EXPECT_TRUE(runningChanges.empty());
  EXPECT_EQ(pixels(), red);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !retained(); }, 200));
  discarded();
  select();  // An exhausted transition cannot resurrect pixels on later navigation.
  discarded();

  show(red);
  select();
  QTest::qWait(40);
  show(blue);  // Early delivery cancels the outgoing fade, even while metadata is busy.
  QTest::qWait(160);
  EXPECT_EQ(pixels(), blue);
  EXPECT_EQ(thumbnail->opacity(), 1);
  item->setWidth(480);
  show(red);  // Same-selection resolution replacement has no fade.
  QTest::qWait(160);
  EXPECT_EQ(pixels(), red);
  EXPECT_EQ(thumbnail->opacity(), 1);

  select();
  preview.clear();
  discarded();
  preview.setTarget("/folder", true, -1, {}, 0040755, false, {}, "folder");
  discarded();
  for (const auto error :
       {PreviewService::PreviewErrorKind::DecodeFailed, PreviewService::PreviewErrorKind::DecodeTimeout}) {
    show(red);
    select();
    PreviewServiceTestAccess::presentationState(preview, true, {}, error);
    discarded();
    EXPECT_TRUE(pane->property("showIconFallback").toBool());
  }
  show(red);
  select();
  PreviewServiceTestAccess::presentationState(preview, false);  // Text/non-image completion.
  discarded();
  show(red);
  preview.setTarget("/another-folder", true, -1, {}, 0040755, false, {}, "folder");
  discarded();
  EXPECT_EQ(pane->findChild<QObject*>("previewFileName")->property("rawText").toString(), "another-folder");
  EXPECT_EQ(pane->property("sizeText").toString(), "Dir");

  show(red);
  select();
  item->setVisible(false);
  discarded();
  item->setVisible(true);
  discarded();
  show(blue);
  QTest::qWait(160);
  EXPECT_EQ(pixels(), blue);
  EXPECT_EQ(thumbnail->opacity(), 1);
}

class SidebarFallbackContinuity : public testing::TestWithParam<int> {};

TEST_P(SidebarFallbackContinuity, MatchingChainsSurviveRapidNavigationAcrossEveryTier) {
  QTemporaryDir icons(files_test::fixturePattern("sidebar-tiny-theme"));
  ASSERT_TRUE(icons.isValid());
  ASSERT_TRUE(files_test::writeTinyTheme(icons));
  const auto restoreIcons = files_test::scopedIconTheme({icons.path()}, QStringLiteral("tiny-test-theme"));
  DirectoryController controller;
  auto& preview = *controller.preview();
  QQuickWindow window;
  window.resize(400, 700);
  window.show();
  QQmlEngine engine;
  initializeFilesEngine(engine);
  QQmlComponent component(&engine);
  component.setData("import QtQuick\nimport HolonightFiles\nPreviewPane { width: 320; height: 600 }", QUrl());
  std::unique_ptr<QObject> pane(
      component.createWithInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}}));
  ASSERT_NE(pane, nullptr);
  qobject_cast<QQuickItem*>(pane.get())->setParentItem(window.contentItem());
  preview.setTarget("/folder", true, -1, {}, 0040755, false, {}, "folder");
  auto* theme = pane->findChild<QQuickItem*>("previewThemeIcon");
  auto* fallback = pane->findChild<QQuickItem*>("previewFallbackIcon");
  auto* placeholder = pane->findChild<QQuickItem*>("previewIconFailurePlaceholder");
  ASSERT_NE(theme, nullptr);
  ASSERT_NE(fallback, nullptr);
  ASSERT_NE(placeholder, nullptr);
  if (GetParam() > 0) {
    QQmlProperty::write(theme, "source", QUrl("qrc:/missing-theme.svg"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return fallback->isVisible(); }));
  }
  if (GetParam() > 1) {
    QQmlProperty::write(fallback, "source", QUrl("qrc:/missing-fallback.svg"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return placeholder->isVisible(); }));
  }
  const QList<QQuickItem*> tiers{theme, fallback, placeholder};
  auto* tier = tiers.at(GetParam());
  ASSERT_TRUE(tier->isVisible());
  QSignalSpy visibility(tier, &QQuickItem::visibleChanged);
  for (int i = 0; i < 5; ++i) {
    PreviewServiceTestAccess::presentationSelection(preview, "folder");
    EXPECT_TRUE(tier->isVisible());
    EXPECT_FALSE(pane->property("fallbackDelayElapsed").toBool());
    EXPECT_TRUE(preview.mimeTypeDescription().isEmpty());
  }
  EXPECT_TRUE(visibility.empty());
  auto* item = qobject_cast<QQuickItem*>(pane.get());
  item->setVisible(false);
  EXPECT_FALSE(pane->property("fallbackDisplayed").toBool());
  item->setVisible(true);
  EXPECT_FALSE(pane->property("showIconFallback").toBool());
  ASSERT_TRUE(QTest::qWaitFor([&] { return pane->property("showIconFallback").toBool(); }))
      << "visible=" << item->isVisible() << " elapsed=" << pane->property("fallbackDelayElapsed").toBool()
      << " running=" << pane->findChild<QObject*>("previewFallbackDelay")->property("running").toBool()
      << " busy=" << preview.busy();
  PreviewServiceTestAccess::presentationSelection(preview, "folder/different-chain");
  EXPECT_FALSE(pane->property("showIconFallback").toBool());
  preview.clear();
  EXPECT_FALSE(pane->property("fallbackDisplayed").toBool());
}

INSTANTIATE_TEST_SUITE_P(AllTiers, SidebarFallbackContinuity, testing::Values(0, 1, 2));
