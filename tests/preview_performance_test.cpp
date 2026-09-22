#include "preview_fixtures.h"
#include "preview_service.h"
#include "preview_service_test_access.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

#include <array>
#include <atomic>
#include <functional>
#include <gtest/gtest.h>

namespace {
// Run a normal event loop: QTest::qWaitFor sleeps between processEvents calls,
// imposing a 10 ms floor on otherwise sub-millisecond cache-hit notifications.
bool awaitCompletion(const std::function<bool()>& complete, int timeout) {
  if (complete()) {
    return true;
  }
  QEventLoop loop;
  QTimer poll;
  QTimer deadline;
  deadline.setSingleShot(true);
  QObject::connect(&poll, &QTimer::timeout, &loop, [&] {
    if (complete()) {
      loop.quit();
    }
  });
  QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
  poll.start(1);
  deadline.start(timeout);
  loop.exec();
  return complete();
}
QString fixture(int index) {
  return QDir(qEnvironmentVariable("FILES_PERFORMANCE_FIXTURES"))
      .filePath(QString::number(index) + (index < 3 ? ".png" : ".jpg"));
}
QColor regionColor(int index, int region) { return {35 + (index * 27), 40 + (region * 48), 210 - (region * 35)}; }
void fillFixture(QImage& image, int index) {
  for (int row = 0; row < image.height(); ++row) {
    for (int column = 0; column < image.width(); ++column) {
      const int region = (row >= 2000 ? 2 : 0) + (column >= 3000 ? 1 : 0);
      const auto color = regionColor(index, region);
      // Solid interiors support exact PNG / tolerant JPEG identity checks;
      // deterministic gradients, detail and transparency surround the patches.
      const bool patch = std::abs((column % 3000) - 1500) < 500 && std::abs((row % 2000) - 1000) < 350;
      const auto pixel = patch ? color.rgba()
                               : qRgba(((column / 24) + (index * 29)) % 256, ((row / 16) + (region * 17)) % 256,
                                       ((column * 13) + (row * 7)) % 256, index < 3 ? (column + row) % 256 : 255);
      image.setPixel(column, row, pixel);
    }
  }
}
QSize orientedSize(int index) { return index >= 4 ? QSize(4000, 6000) : QSize(6000, 4000); }
void target(PreviewService& service, int index) {
  const QFileInfo info(fixture(index));
  service.setTarget(info.absoluteFilePath(), false, info.size(), info.lastModified(), 0100644, false, {});
}
void metric(const QString& name, qint64 value) {
  testing::Test::RecordProperty(name.toStdString(), QString::number(value).toStdString());
}
qint64 rss() {
  QFile status("/proc/self/status");
  if (!status.open(QIODevice::ReadOnly)) {
    return -1;
  }
  for (const auto& line : status.readAll().split('\n')) {
    if (line.startsWith("VmRSS:")) {
      return line.simplified().split(' ').value(1).toLongLong();
    }
  }
  return -1;
}
bool correctPixels(const PreviewService& service, int index, QSize bounds) {
  const auto image = service.image();
  const auto needed = orientedSize(index).scaled(bounds, Qt::KeepAspectRatio);
  if (service.name() != QFileInfo(fixture(index)).fileName() || service.sourcePixelSize() != orientedSize(index) ||
      image.width() < needed.width() || image.height() < needed.height()) {
    return false;
  }
  // Expected displayed quadrant order, independently specified for EXIF 2, 6, 7.
  constexpr std::array<std::array<int, 4>, 4> orders{{{{0, 1, 2, 3}}, {{1, 0, 3, 2}}, {{2, 0, 3, 1}}, {{3, 1, 2, 0}}}};
  const auto& order = orders.at(index < 3 ? 0 : index - 2);
  for (int quadrant = 0; quadrant < 4; ++quadrant) {
    const auto actual =
        image.pixelColor(image.width() * (quadrant % 2 == 0 ? 1 : 3) / 4, image.height() * (quadrant < 2 ? 1 : 3) / 4);
    const auto expected = regionColor(index, order.at(quadrant));
    const int tolerance = index < 3 ? 0 : 5;
    if (std::abs(actual.red() - expected.red()) > tolerance ||
        std::abs(actual.green() - expected.green()) > tolerance ||
        std::abs(actual.blue() - expected.blue()) > tolerance || actual.alpha() != 255) {
      return false;
    }
  }
  return true;
}

class PreviewPerformance : public testing::Test {
 protected:
  void SetUp() override {
    if (!qEnvironmentVariableIsSet("FILES_PREVIEW_PERFORMANCE")) {
      GTEST_SKIP() << "Opt-in only: scripts/measure-preview.py";
    }
    ASSERT_FALSE(qEnvironmentVariable("FILES_PERFORMANCE_FIXTURES").isEmpty());
  }
  static void sample(PreviewService& service, std::atomic_int& attempts, const QString& label, int index, QSize bounds,
                     const std::function<void()>& request, bool retain = false) {
    const auto previous = service.image().size();
    const int before = attempts.load();
    QElapsedTimer clock;
    clock.start();
    qint64 pixels = -1;
    qint64 metadata = -1;
    const auto connection = QObject::connect(&service, &PreviewService::changed, &service, [&] {
      const auto notified = clock.nsecsElapsed();
      if (retain) {
        EXPECT_GE(service.image().width(), previous.width());
        EXPECT_GE(service.image().height(), previous.height());
      }
      if (correctPixels(service, index, bounds)) {
        if (pixels < 0) {
          pixels = notified;
        }
        if (!service.busy() && metadata < 0) {
          metadata = notified;
        }
      }
    });
    request();
    // Reused pixels can already satisfy a shrinking request without emitting a signal.
    if (correctPixels(service, index, bounds) && !service.busy()) {
      if (pixels < 0) {
        pixels = clock.nsecsElapsed();
      }
      if (metadata < 0) {
        metadata = clock.nsecsElapsed();
      }
    }
    const bool completed = awaitCompletion([&] { return metadata >= 0; }, 10000);
    QObject::disconnect(connection);
    metric(label + "_pixels_ns", pixels);
    metric(label + "_metadata_ns", metadata);
    metric(label + "_attempts", attempts.load() - before);
    ASSERT_TRUE(completed) << label.toStdString() << ": " << service.previewErrorMessage().toStdString()
                           << "; observed " << service.image().width() << "x" << service.image().height()
                           << "; attempts " << attempts.load() - before;
    ASSERT_EQ(service.previewErrorKind(), PreviewService::PreviewErrorKind::None);
    ASSERT_TRUE(correctPixels(service, index, bounds));
  }
};
}  // namespace

TEST_F(PreviewPerformance, GenerateFixtures) {
  ASSERT_TRUE(QDir().mkpath(qEnvironmentVariable("FILES_PERFORMANCE_FIXTURES")));
  for (int index = 0; index < 6; ++index) {
    QImage image(6000, 4000, QImage::Format_ARGB32);
    fillFixture(image, index);
    QBuffer buffer;
    ASSERT_TRUE(buffer.open(QIODevice::WriteOnly));
    ASSERT_TRUE(image.save(&buffer, index < 3 ? "PNG" : "JPEG", index < 3 ? -1 : 93));
    auto bytes = buffer.data();
    if (index >= 3) {
      auto exif = QByteArray::fromHex("45786966000049492a0008000000010012010300010000000100000000000000");
      constexpr std::array<int, 3> orientations{2, 6, 7};
      exif[24] = static_cast<char>(orientations.at(index - 3));
      bytes = files_test::spliceJpegExif(bytes, exif);
    }
    QFile file(fixture(index));
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    ASSERT_EQ(file.write(bytes), bytes.size());
  }
  metric("fixtures", 6);
}

TEST_F(PreviewPerformance, CacheReuse) {
  for (int index = 0; index < 6; ++index) {
    std::atomic_int attempts = 0;
    const QSize bounds(512, 512);
    const auto label = QString::number(index);
    {
      PreviewService cold;
      PreviewServiceTestAccess::beforeFullDecode(cold, [&] { ++attempts; });
      cold.setRequestedSize(PreviewService::PreviewConsumer::Pane, bounds);
      ASSERT_NO_FATAL_FAILURE(sample(cold, attempts, "cold_" + label, index, bounds, [&] { target(cold, index); }));
      ASSERT_EQ(attempts.load(), 1);
    }
    PreviewService warm;
    PreviewServiceTestAccess::beforeFullDecode(warm, [&] { ++attempts; });
    warm.setRequestedSize(PreviewService::PreviewConsumer::Pane, bounds);
    ASSERT_NO_FATAL_FAILURE(sample(warm, attempts, "disk_" + label, index, bounds, [&] { target(warm, index); }));
    ASSERT_EQ(attempts.load(), 1);
    const auto memoryKey = warm.image().cacheKey();
    warm.clear();
    ASSERT_NO_FATAL_FAILURE(sample(warm, attempts, "memory_" + label, index, bounds, [&] { target(warm, index); }));
    ASSERT_EQ(attempts.load(), 1);
    ASSERT_EQ(warm.image().cacheKey(), memoryKey);
  }
}

TEST_F(PreviewPerformance, ResizeQuickLook) {
  for (int index = 0; index < 6; ++index) {
    std::atomic_int attempts = 0;
    PreviewService service;
    PreviewServiceTestAccess::beforeFullDecode(service, [&] { ++attempts; });
    for (const int size : {128, 256, 512, 1024, 1800}) {
      const auto request = [&] {
        if (size == 1800) {
          service.setRequestedSize(PreviewService::PreviewConsumer::QuickLook, {size, size});
          service.setQuickLookActive(true);
        } else {
          service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {size, size});
        }
        if (size == 128) {
          target(service, index);
        }
      };
      ASSERT_NO_FATAL_FAILURE(sample(service, attempts, QString("resize_%1_%2").arg(index).arg(size), index,
                                     {size, size}, request, size != 128));
    }
    ASSERT_NO_FATAL_FAILURE(sample(
        service, attempts, QString("rapid_%1").arg(index), index, {2300, 2300},
        [&] {
          for (int size = 1900; size <= 2300; size += 10) {
            service.setRequestedSize(PreviewService::PreviewConsumer::QuickLook, {size, size});
          }
        },
        true));
    const auto retained = service.image().size();
    service.setQuickLookActive(false);
    service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {128, 128});
    // A satisfied shrink keeps pixels immediately; then await actual debounce completion.
    ASSERT_NO_FATAL_FAILURE(sample(
        service, attempts, QString("return_%1").arg(index), index, {128, 128}, [&] { target(service, index); }, true));
    ASSERT_TRUE(
        awaitCompletion([&] { return !PreviewServiceTestAccess::resizePending(service) && !service.busy(); }, 5000));
    ASSERT_EQ(service.image().size(), retained);
  }
}

TEST_F(PreviewPerformance, SelectionPressure) {
  std::atomic_int attempts = 0;
  PreviewService service;
  PreviewServiceTestAccess::beforeFullDecode(service, [&] { ++attempts; });
  service.setRequestedSize(PreviewService::PreviewConsumer::Pane, {4096, 4096});
  QElapsedTimer clock;
  clock.start();
  qint64 last = 0;
  qint64 maxGap = 0;
  int ticks = 0;
  QTimer timer;
  QObject::connect(&timer, &QTimer::timeout, [&] {
    const auto now = clock.nsecsElapsed();
    maxGap = std::max(maxGap, now - last);
    last = now;
    ++ticks;
  });
  timer.start(5);
  metric("rss_start_kib", rss());
  for (int cycle = 0; cycle < 3; ++cycle) {
    for (int step = 0; step < 12; ++step) {
      const int index = step < 6 ? step : 11 - step;
      ASSERT_NO_FATAL_FAILURE(sample(service, attempts, QString("selection_%1_%2").arg(cycle).arg(step), index,
                                     {4096, 4096}, [&] { target(service, index); }));
    }
    metric(QString("rss_cycle_%1_kib").arg(cycle), rss());
  }
  ASSERT_NO_FATAL_FAILURE(sample(service, attempts, "replacement", 5, {4096, 4096}, [&] {
    for (int step = 0; step < 60; ++step) {
      target(service, step % 6);
    }
  }));
  // Observe real dispatch, without blocking the worker or injecting decode delays.
  std::atomic_bool dispatched = false;
  PreviewServiceTestAccess::beforeDispatch(service, [&] { dispatched = true; });
  target(service, 0);
  ASSERT_TRUE(awaitCompletion([&] { return dispatched.load(); }, 5000));
  target(service, 1);
  ASSERT_TRUE(service.busy());
  QSignalSpy stopped(&service, &PreviewService::shutdownFinished);
  QElapsedTimer shutdown;
  shutdown.start();
  service.shutdown();
  ASSERT_TRUE(awaitCompletion([&] { return !stopped.empty(); }, 5000));
  metric("shutdown_ns", shutdown.nsecsElapsed());
  metric("rss_shutdown_kib", rss());
  metric("attempts_total", attempts.load());
  metric("max_gui_timer_gap_ns", maxGap);
  metric("timer_ticks", ticks);
  ASSERT_GT(ticks, 0);
}
