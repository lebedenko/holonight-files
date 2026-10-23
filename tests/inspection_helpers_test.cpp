#include "directory_fixtures.h"
#include "engine_setup.h"
#include "grid_thumbnails.h"
#include "icon_fallbacks.h"
#include "inspection_keys.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>
#include <memory>
#include <sys/stat.h>

TEST(IconFallbacks, ExactChainsAreSharedWithinEngineAndIsolatedBetweenEngines) {
  QQmlEngine first;
  initializeFilesEngine(first);
  QQmlEngine second;
  initializeFilesEngine(second);
  auto* cache = first.singletonInstance<IconFallbacks*>("HolonightFiles", "IconFallbacks");
  ASSERT_NE(cache, nullptr);
  cache->markUnresolved("a/b");
  cache->markUnresolved("a/b");
  EXPECT_TRUE(cache->isUnresolved("a/b"));
  for (const auto* chain : {"a", "b/a", "A/b", "a/b/", ""}) {
    EXPECT_FALSE(cache->isUnresolved(chain));
  }
  QQmlComponent component(&first);
  component.setData(
      "import QtQml\nimport HolonightFiles\nQtObject { property bool failed: IconFallbacks.isUnresolved('a/b') }",
      QUrl());
  std::unique_ptr<QObject> object(component.create());
  ASSERT_NE(object, nullptr) << component.errorString().toStdString();
  EXPECT_TRUE(object->property("failed").toBool());
  QQmlComponent observer(&first);
  observer.setData(
      "import QtQml\nimport HolonightFiles\nQtObject { property bool failed: IconFallbacks.isUnresolved('later') }",
      QUrl());
  std::unique_ptr<QObject> observed(observer.create());
  ASSERT_NE(observed, nullptr);
  EXPECT_FALSE(observed->property("failed").toBool());
  cache->markUnresolved("later");
  QCoreApplication::processEvents();
  EXPECT_FALSE(observed->property("failed").toBool());
  EXPECT_TRUE(cache->isUnresolved("later"));
  auto* other = second.singletonInstance<IconFallbacks*>("HolonightFiles", "IconFallbacks");
  ASSERT_NE(other, nullptr);
  EXPECT_NE(cache, other);
  EXPECT_FALSE(other->isUnresolved("a/b"));
}

TEST(InspectionKeys, AcceptanceAndNullController) {
  const InspectionKeys keys;
  for (bool popup : {false, true}) {
    EXPECT_TRUE(keys.press(Qt::Key_Space, "wrong", 0, true, nullptr, popup));
    EXPECT_FALSE(keys.press(Qt::Key_Space, "", 0, false, nullptr, popup));
    EXPECT_FALSE(keys.press(Qt::Key_J, "j", 0, false, nullptr, popup));
    for (bool visual : {false, true}) {
      EXPECT_TRUE(keys.overrideShortcut(Qt::Key_Space, popup, visual));
      EXPECT_EQ(keys.overrideShortcut(Qt::Key_Escape, popup, visual), popup || visual);
      EXPECT_FALSE(keys.overrideShortcut(Qt::Key_J, popup, visual));
    }
  }
  for (const auto key : {Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L}) {
    for (const bool popup : {false, true}) {
      EXPECT_TRUE(keys.press(key, "j", Qt::ControlModifier, false, nullptr, popup));
    }
  }
  EXPECT_TRUE(keys.press(Qt::Key_O, "", Qt::ControlModifier, false, nullptr, true));
  EXPECT_TRUE(keys.release(Qt::Key_Space));
  EXPECT_FALSE(keys.release(Qt::Key_Escape));
  EXPECT_FALSE(keys.release(Qt::Key_Return));
}

TEST(InspectionKeys, NormalizationPopupAllowlistRepeatsAndHistory) {
  QTemporaryDir dir(files_test::fixturePattern("inspection-keys"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("child"));
  files_test::writeFile(dir, "child/a.txt");
  files_test::writeFile(dir, "child/b.txt");
  files_test::writeFile(dir, "root.txt");  // Quick Look needs a previewable entry (directories are not)
  DirectoryController controller;
  const InspectionKeys keys;
  auto settle = [&] { return QTest::qWaitFor([&] { return !controller.scanning(); }); };
  controller.open(dir.path());
  ASSERT_TRUE(settle());
  ASSERT_TRUE(controller.handleKey("j"));  // from .. to child/
  for (int enter : {Qt::Key_Return, Qt::Key_Enter}) {
    EXPECT_TRUE(keys.press(enter, "wrong", 0, false, &controller, false));
    ASSERT_TRUE(settle());
    EXPECT_EQ(controller.currentPath(), dir.filePath("child"));
    controller.navigateParent();
    ASSERT_TRUE(settle());
  }
  EXPECT_FALSE(keys.press(Qt::Key_J, "", 0, false, &controller, false));
  EXPECT_FALSE(keys.press(Qt::Key_F1, "?", 0, false, &controller, false));
  ASSERT_TRUE(controller.handleKey("j"));  // from child/ to root.txt
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->quickLookEligible(); }, 5000));
  EXPECT_TRUE(keys.press(Qt::Key_Space, "wrong", 0, false, &controller, false));
  EXPECT_TRUE(controller.quickLookOpen());
  EXPECT_TRUE(keys.press(Qt::Key_Space, "", 0, true, &controller, true));
  EXPECT_TRUE(controller.quickLookOpen());
  for (const auto* text : {"h", "l", "Return", "Enter", "v", "J", ""}) {
    EXPECT_FALSE(keys.press(Qt::Key_F1, text, 0, false, &controller, true));
  }
  EXPECT_TRUE(keys.press(Qt::Key_Escape, "wrong", 0, false, &controller, true));
  EXPECT_FALSE(controller.quickLookOpen());
  controller.navigateInto(1);
  ASSERT_TRUE(settle());
  EXPECT_TRUE(keys.press(Qt::Key_J, "j", Qt::AltModifier | Qt::ShiftModifier, true, &controller, true));
  EXPECT_EQ(controller.cursorRow(), 1);
  EXPECT_TRUE(keys.press(Qt::Key_K, "k", 0, false, &controller, true));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_FALSE(keys.press(Qt::Key_O, "o", Qt::AltModifier, false, &controller, true));
  EXPECT_FALSE(keys.press(Qt::Key_O, "", Qt::ControlModifier, false, &controller, false));
  EXPECT_TRUE(keys.press(Qt::Key_O, "v", Qt::ControlModifier | Qt::ShiftModifier, true, &controller, true));
  ASSERT_TRUE(settle());
  EXPECT_EQ(controller.currentPath(), dir.path());
  EXPECT_TRUE(keys.press(Qt::Key_I, "", Qt::ControlModifier | Qt::AltModifier, false, &controller, true));
  ASSERT_TRUE(settle());
  EXPECT_EQ(controller.currentPath(), dir.filePath("child"));
  EXPECT_TRUE(controller.handleKey("v"));
  EXPECT_TRUE(keys.press(Qt::Key_Escape, "", 0, false, &controller, false));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
}

TEST(IconFallbacks, EngineInitializationIsIdempotentAndProvidersAreIndependent) {
  QQmlEngine first;
  QQmlEngine second;
  initializeFilesEngine(first);
  initializeFilesEngine(second);
  auto* provider = first.imageProvider(QStringLiteral("icon"));
  ASSERT_NE(provider, nullptr);
  EXPECT_NE(provider, second.imageProvider(QStringLiteral("icon")));
  initializeFilesEngine(first);
  EXPECT_EQ(provider, first.imageProvider(QStringLiteral("icon")));
}

TEST(InspectionKeys, ArrowKeysReachTheQuickLookLineMoverOnlyInPopups) {
  QTemporaryDir dir(files_test::fixturePattern("inspection-arrows"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeFile(dir, "a.txt", "one\ntwo\nthree\n");
  DirectoryController controller;
  const InspectionKeys keys;
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  ASSERT_TRUE(controller.handleKey("j"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->quickLookEligible(); }, 5000));
  EXPECT_FALSE(keys.press(Qt::Key_Down, "", 0, false, &controller, false));  // listing: not a bound key
  ASSERT_TRUE(keys.press(Qt::Key_Space, " ", 0, false, &controller, false));
  ASSERT_TRUE(controller.quickLookOpen());
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->currentLineIndex() == 0; }, 5000));
  EXPECT_TRUE(keys.press(Qt::Key_Down, "", 0, false, &controller, true));
  EXPECT_EQ(controller.preview()->currentLineIndex(), 1);
  EXPECT_TRUE(keys.press(Qt::Key_Down, "", 0, true, &controller, true));  // held-key repeat is allowed
  EXPECT_EQ(controller.preview()->currentLineIndex(), 2);
  EXPECT_TRUE(keys.press(Qt::Key_Up, "", 0, false, &controller, true));
  EXPECT_EQ(controller.preview()->currentLineIndex(), 1);
  EXPECT_EQ(controller.cursorRow(), 1);
}

TEST(GridThumbnails, EligibilityNativeEncodingAndPhysicalSizing) {
  GridThumbnails thumbnails;
  for (const auto& suffix : HolonightImages::supportedSuffixes() + QStringList{"svg"}) {
    EXPECT_EQ(thumbnails.eligible("photo." + suffix.toUpper(), false, false, S_IFREG), suffix != "pdf");
  }
  for (const auto* name : {"file", "file.pdf", "file.mp4", "file.txt"}) {
    EXPECT_FALSE(thumbnails.eligible(name, false, false, S_IFREG));
  }
  EXPECT_FALSE(thumbnails.eligible("a.png", true, false, S_IFDIR));
  EXPECT_FALSE(thumbnails.eligible("a.png", false, true, S_IFREG));
  EXPECT_FALSE(thumbnails.eligible("a.png", false, false, S_IFIFO));
  const QByteArray native = QByteArray("/tmp/a #?%") + static_cast<char>(0xff) + ".png";
  for (qreal ratio : {1.0, 1.25, 1.5, 1.5625, 2.0}) {
    const auto url = thumbnails.source(native, ratio, 7);
    EXPECT_EQ(url.host(), "thumbnail");
    const auto parts = url.path().mid(1).split('/');
    ASSERT_EQ(parts.size(), 3);
    EXPECT_EQ(parts[0].toInt(), static_cast<int>(std::ceil(64 * ratio)));
    EXPECT_EQ(parts[1], "7");
    EXPECT_EQ(QByteArray::fromBase64(parts[2].toLatin1(), QByteArray::Base64UrlEncoding), native);
  }
  QQmlEngine engine;
  initializeFilesEngine(engine);
  auto* provider = engine.imageProvider("thumbnail");
  ASSERT_NE(provider, nullptr);
  initializeFilesEngine(engine);
  EXPECT_EQ(provider, engine.imageProvider("thumbnail"));
  QQmlEngine other;
  initializeFilesEngine(other);
  EXPECT_NE(provider, other.imageProvider("thumbnail"));
}
