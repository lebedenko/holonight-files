#include "directory_controller.h"
#include "directory_fixtures.h"

#include <QElapsedTimer>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <gtest/gtest.h>

namespace {
// Called during scenegraph synchronization, when the GUI thread is blocked. A populated
// delegate must intersect the listing viewport; a row count alone is not rendering evidence.
bool hasVisibleDelegate(QQuickItem* item, const QRectF& viewport) {
  if (!item->isVisible()) {
    return false;
  }
  if (item->objectName() == "directoryEntryDelegate" && !item->property("title").toString().isEmpty() &&
      item->width() > 0 && item->height() > 0 && item->mapRectToScene(item->boundingRect()).intersects(viewport)) {
    return true;
  }
  return std::ranges::any_of(item->childItems(), [&](auto* child) { return hasVisibleDelegate(child, viewport); });
}
void recordFrame(qint64 now, std::atomic<qint64>& lastFrame, std::atomic<qint64>& maxGap, std::atomic<int>& frames) {
  const auto previous = lastFrame.exchange(now);
  if (previous >= 0) {
    maxGap.store(qMax(maxGap.load(), now - previous));
  }
  ++frames;
}
}  // namespace

TEST(DirectoryPerformance, RenderedRowsAndInteractionWhileLoading) {
  if (!qEnvironmentVariableIsSet("FILES_BROWSE_BENCHMARK")) {
    GTEST_SKIP() << "Opt-in native Wayland acceptance: FILES_BROWSE_BENCHMARK=1";
  }
  if (QGuiApplication::platformName() != "wayland") {
    GTEST_SKIP() << "Offscreen/X11 is regression coverage, not native Wayland rendering acceptance";
  }
  bool sampled = false;
  for (const int count : {12000, 48000, 192000}) {
    QTemporaryDir dir(files_test::fixturePattern("performance"));
    ASSERT_TRUE(dir.isValid());
    files_test::populateEntries(dir, count);
    DirectoryController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    engine.loadFromModule("HolonightFiles", "Main");
    ASSERT_EQ(engine.rootObjects().size(), 1);
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    ASSERT_NE(window, nullptr);
    window->resize(1000, 700);
    window->requestActivate();
    ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
    ASSERT_TRUE(QTest::qWaitForWindowActive(window));
    auto* list = window->findChild<QQuickItem*>("directoryListView");
    ASSERT_NE(list, nullptr);
    QTest::qWait(100);  // Finish initial empty-window rendering before navigation.

    QElapsedTimer elapsed;
    std::atomic<bool> populated{false};
    std::atomic<bool> interacting{false};
    std::atomic<bool> scrolled{false};
    std::atomic<qint64> firstFrame{-1};
    std::atomic<qint64> lastFrame{-1};
    std::atomic<qint64> maxGap{0};
    std::atomic<int> frames{0};
    elapsed.start();
    const auto sync = QObject::connect(
        window, &QQuickWindow::beforeSynchronizing, window,
        [&] {
          populated.store(hasVisibleDelegate(list, list->mapRectToScene(list->boundingRect())));
          scrolled.store(list->property("contentY").toReal() > list->property("originY").toReal());
        },
        Qt::DirectConnection);
    const auto swap = QObject::connect(
        window, &QQuickWindow::frameSwapped, window,
        [&] {
          const auto now = elapsed.nsecsElapsed();
          if (populated.load()) {
            qint64 unset = -1;
            firstFrame.compare_exchange_strong(unset, now);
          }
          if (interacting.load() && scrolled.load()) {
            recordFrame(now, lastFrame, maxGap, frames);
          }
        },
        Qt::DirectConnection);
    // Explicit disconnect before captured locals die (also on an ASSERT early return).
    const auto cleanup = qScopeGuard([&] {
      QObject::disconnect(sync);
      QObject::disconnect(swap);
    });
    qint64 postedAt = -1;
    qint64 maxInput = 0;
    int keys = 0;
    int previousCursor = 0;
    qreal maxScrollOffset = 0;
    QObject::connect(&controller, &DirectoryController::changed, &controller, [&] {
      if (postedAt >= 0 && controller.cursorRow() != previousCursor) {
        maxInput = qMax(maxInput, elapsed.nsecsElapsed() - postedAt);
        postedAt = -1;
        ++keys;
      }
    });
    QTimer input;
    QObject::connect(&input, &QTimer::timeout, &controller, [&] {
      if (!controller.scanning() || controller.listing()->rowCount() < 100 || postedAt >= 0) {
        return;
      }
      maxScrollOffset = qMax(maxScrollOffset, list->property("contentY").toReal() - list->property("originY").toReal());
      interacting.store(true);
      previousCursor = controller.cursorRow();
      postedAt = elapsed.nsecsElapsed();
      // Posted events measure queueing delay as well as handling; repeated movement scrolls
      // the production ListView while batches are arriving.
      QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyPress, Qt::Key_5, Qt::NoModifier, "5"));
      QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyRelease, Qt::Key_5, Qt::NoModifier, "5"));
      QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyPress, Qt::Key_J, Qt::NoModifier, "j"));
      QCoreApplication::postEvent(window, new QKeyEvent(QEvent::KeyRelease, Qt::Key_J, Qt::NoModifier, "j"));
    });
    controller.open(dir.path());
    input.start(16);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }, 60000));
    const auto settledMs = elapsed.elapsed();
    input.stop();
    interacting.store(false);
    ASSERT_TRUE(QTest::qWaitFor([&] { return firstFrame.load() >= 0; }));
    const auto suffix = QString::number(count).toStdString();
    RecordProperty("first_rendered_ms_" + suffix, std::to_string(static_cast<double>(firstFrame.load()) / 1e6));
    RecordProperty("max_frame_gap_ms_" + suffix, std::to_string(static_cast<double>(maxGap.load()) / 1e6));
    RecordProperty("max_input_ms_" + suffix, std::to_string(static_cast<double>(maxInput) / 1e6));
    RecordProperty("frames_" + suffix, frames.load());
    RecordProperty("keys_" + suffix, keys);
    RecordProperty("max_scroll_offset_" + suffix, std::to_string(maxScrollOffset));
    RecordProperty("load_ms_" + suffix, static_cast<int>(settledMs));
    RecordProperty("locale", QLocale().name().toStdString());
    RecordProperty("graphics_api", static_cast<int>(window->rendererInterface()->graphicsApi()));
    RecordProperty("qt_version", qVersion());
    EXPECT_EQ(controller.listing()->rowCount(), count);
    EXPECT_LE(firstFrame.load(), 150000000);
    EXPECT_LE(maxGap.load(), 100000000);
    EXPECT_LE(maxInput, 100000000);
    QElapsedTimer refresh;
    refresh.start();
    ASSERT_FALSE(files_test::writeFile(dir, "watcher-new.txt").isEmpty());
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && controller.listing()->rowCount() == count + 1; },
                                60000));
    RecordProperty("watcher_refresh_ms_" + suffix, static_cast<int>(refresh.elapsed()));
    if (keys < 10 || frames.load() < 10 || settledMs < 200) {
      continue;  // Increase real fixture size instead of slowing the worker artificially.
    }
    sampled = true;
    EXPECT_EQ(postedAt, -1) << "Input still queued at completion";
    break;
  }
  if (!sampled) {
    GTEST_SKIP() << "Inconclusive: insufficient loading/interaction overlap even at 192000 entries";
  }
}
