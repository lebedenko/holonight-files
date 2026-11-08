#include "isolated_preview_acceptance.h"

#include "directory_controller.h"
#include "engine_setup.h"
#include "preview_service_test_access.h"

#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <holonight_thumbnails/cache.h>
#include <memory>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
QQuickItem* thumbnail(QQuickItem* parent, const QString& name) {
  for (auto* child : parent->childItems()) {
    if (child->objectName() == "directoryIconDelegate" && child->property("name").toString() == name) {
      return child->findChild<QQuickItem*>("gridThumbnail");
    }
    if (auto* found = thumbnail(child, name)) {
      return found;
    }
  }
  return nullptr;
}
void pixels(QQuickWindow& window, QQuickItem* area, const QColor& expected) {
  require(area != nullptr, "Missing application image area");
  require(QTest::qWaitFor([&] {
            if (!area->isVisible() || area->width() <= 0 || area->height() <= 0) {
              return false;
            }
            const auto image = window.grabWindow();
            const auto point = area->mapToScene({area->width() / 2, area->height() / 2}).toPoint();
            return image.rect().contains(point) && image.pixelColor(point) == expected;
          }),
          "Application rendered pixels did not converge");
}
struct View {
  explicit View(HolonightFileBrowser::ImageRuntimeOptions runtime) : controller(runtime) {
    initializeFilesEngine(engine, std::move(runtime));
    engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
    engine.loadFromModule("HolonightFiles", "Main");
    require(!engine.rootObjects().isEmpty(), "Files Main QML failed");
    window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    require(window != nullptr, "Missing Files window");
    window->resize(1100, 800);
  }
  View(const View&) = delete;
  View& operator=(const View&) = delete;
  View(View&&) = delete;
  View& operator=(View&&) = delete;
  ~View() {
    if (shutdownDone) {
      return;
    }
    QSignalSpy retired(&controller, &DirectoryController::shutdownFinished);
    controller.shutdown();
    if (retired.isEmpty() && !QTest::qWaitFor([&] { return !retired.isEmpty(); })) {
      qFatal("Files acceptance shutdown did not finish");
    }
  }
  void open(const QString& directory) {
    controller.open(directory);
    require(QTest::qWaitFor([&] { return !controller.scanning(); }), "Directory scan did not finish");
  }
  void select(int row) {
    controller.selectEntry(row);
    require(QTest::qWaitFor([&] { return !controller.preview()->busy(); }), "Preview did not finish");
    require(controller.preview()->hasImage(), qPrintable(controller.preview()->previewErrorMessage()));
  }
  void pane(const QColor& color) const { pixels(*window, window->findChild<QQuickItem*>("previewImageArea"), color); }
  DirectoryController controller;
  QQmlApplicationEngine engine;
  QQuickWindow* window = nullptr;
  bool shutdownDone = false;
};
void presentation(const HolonightFileBrowser::ImageRuntimeOptions& runtime, const QString& fixtures, bool disk) {
  View view(runtime);
  auto decodes = std::make_shared<std::atomic_int>(0);
  auto cacheDecodes = std::make_shared<std::atomic_int>(0);
  PreviewServiceTestAccess::thumbnailStage(*view.controller.preview(), [cacheDecodes](ThumbnailService::Stage stage) {
    if (stage == ThumbnailService::Stage::CacheDecode) {
      ++*cacheDecodes;
    }
  });
  PreviewServiceTestAccess::beforeFullDecode(*view.controller.preview(), [decodes] { ++*decodes; });
  view.open(fixtures);
  view.select(0);
  view.pane(Qt::red);
  require(disk ? decodes->load() == 0 : decodes->load() > 0, "Wrong cold/disk path");
  view.select(1);
  view.pane(Qt::blue);
  const int beforeMemory = decodes->load();
  const int beforeMemoryCache = cacheDecodes->load();
  view.select(0);
  view.pane(Qt::red);
  require(decodes->load() == beforeMemory, "Memory revisit decoded source");
  require(cacheDecodes->load() == beforeMemoryCache, "Memory revisit decoded disk cache");
  require(view.controller.preview()->sourcePixelSize() == QSize(160, 80), "Warm source dimensions lost");
  require(view.controller.preview()->mimeType() == "image/png", "Warm MIME metadata lost");
  require(view.controller.preview()->exifMake().isEmpty(), "Warm preview retained unrelated EXIF");
  require(view.controller.handleKey(" "), "Quick Look failed to open");
  pixels(*view.window, view.window->findChild<QQuickItem*>("quickLookImageArea"), Qt::red);
  view.window->resize(1250, 900);
  pixels(*view.window, view.window->findChild<QQuickItem*>("quickLookImageArea"), Qt::red);
  require(view.controller.handleKey("Escape"), "Quick Look failed to close");
  const auto beforeSvg = cacheDecodes->load();
  view.select(2);
  view.pane(Qt::green);
  require(!disk || cacheDecodes->load() > beforeSvg, "SVG-derived PNG did not use isolated cache decoding");
  view.controller.setIconView(true);
  for (const auto& [name, color] : {
           std::pair{"01-red.png", QColor(Qt::red)},
           std::pair{"02-blue.png", QColor(Qt::blue)},
           std::pair{"03-vector.svg", QColor(Qt::green)},
       }) {
    require(QTest::qWaitFor([&] { return thumbnail(view.window->contentItem(), name) != nullptr; }),
            "Grid thumbnail missing");
    pixels(*view.window, thumbnail(view.window->contentItem(), name), color);
  }
  qInfo() << (disk ? "disk/memory/SVG-cache" : "cold/memory/SVG-source") << "pane, Quick Look and grid pixels passed";
}
struct Barrier {
  QSemaphore reached;
  QSemaphore resume;
  std::atomic_bool armed{true};
  void wait() {
    if (armed.exchange(false)) {
      reached.release();
      resume.tryAcquire(1, 8000);
    }
  }
};
void cancellation(const HolonightFileBrowser::ImageRuntimeOptions& runtime, const QString& fixtures,
                  ThumbnailService::Stage stage, bool cache, bool shutdown) {
  QTemporaryDir directory;
  require(directory.isValid(), "Cancellation fixture directory missing");
  for (const auto* name : {"01-red.png", "02-blue.png"}) {
    require(QFile::copy(QDir(fixtures).filePath(name), directory.filePath(name)), "Cannot copy cancellation fixture");
  }
  const auto oldPath = directory.filePath("01-red.png");
  if (cache) {
    View warm(runtime);
    warm.open(directory.path());
    warm.select(0);
    warm.pane(Qt::red);
  }
  const auto cacheBytes = [&] {
    QByteArray bytes;
    for (int tier : {128, 256, 512, 1024}) {
      QFile file(HolonightThumbnails::cachePath(QUrl::fromLocalFile(oldPath), tier));
      if (file.open(QIODevice::ReadOnly)) {
        bytes += file.readAll();
      }
    }
    return bytes;
  };
  const auto before = cacheBytes();
  require(cache ? !before.isEmpty() : before.isEmpty(), "Wrong cancellation cache prerequisite");
  View view(runtime);
  auto barrier = std::make_shared<Barrier>();
  const auto release = qScopeGuard([&] { barrier->resume.release(); });
  PreviewServiceTestAccess::thumbnailStage(*view.controller.preview(),
                                           [barrier, stage](ThumbnailService::Stage current) {
                                             if (current == stage) {
                                               barrier->wait();
                                             }
                                           });
  view.open(directory.path());
  require(QTest::qWaitFor([&] { return barrier->reached.tryAcquire(); }), "Cancellation checkpoint not reached");
  PreviewServiceTestAccess::thumbnailStage(*view.controller.preview(), {});
  if (shutdown) {
    QSignalSpy finished(&view.controller, &DirectoryController::shutdownFinished);
    view.controller.shutdown();
    require(!view.controller.preview()->hasImage(), "Shutdown retained stale pixels");
    barrier->resume.release();
    require(QTest::qWaitFor([&] { return !finished.isEmpty(); }), "Shutdown did not retire the worker");
    view.shutdownDone = true;
  } else {
    view.controller.selectEntry(1);
    require(!view.controller.preview()->hasImage(), "Selection change retained stale pixels");
    barrier->resume.release();
    view.select(1);
    view.pane(Qt::blue);
  }
  require(cacheBytes() == before, "Cancelled operation committed cache bytes");
}
void timeout(const HolonightFileBrowser::ImageRuntimeOptions& runtime, const QString& fixtures) {
  QTemporaryDir directory;
  require(QFile::copy(QDir(fixtures).filePath("01-red.png"), directory.filePath("timeout.png")),
          "Timeout fixture missing");
  View view(runtime);
  auto barrier = std::make_shared<Barrier>();
  const auto release = qScopeGuard([&] { barrier->resume.release(); });
  PreviewServiceTestAccess::beforeFullDecode(*view.controller.preview(), [barrier] { barrier->wait(); });
  view.open(directory.path());
  require(QTest::qWaitFor([&] { return barrier->reached.tryAcquire(); }), "Timeout checkpoint not reached");
  require(QTest::qWaitFor(
              [&] {
                return view.controller.preview()->previewErrorKind() == PreviewService::PreviewErrorKind::DecodeTimeout;
              },
              5000),
          "Existing preview deadline did not fire");
  require(!view.controller.preview()->hasImage(), "Timeout displayed stale pixels");
  auto* notice = view.window->findChild<QQuickItem*>("previewErrorNotice");
  require(notice != nullptr && QTest::qWaitFor([&] { return notice->isVisible(); }), "Timeout notice not presented");
  barrier->resume.release();
  require(QTest::qWaitFor([&] { return !PreviewServiceTestAccess::activeJob(*view.controller.preview()); }),
          "Late timeout reply did not retire");
  require(!view.controller.preview()->hasImage(), "Late timeout reply published pixels");
}
void unavailable(const QString& fixtures) {
  View view({"/nonexistent/holonight-acceptance-runtime"});
  view.open(fixtures);
  require(QTest::qWaitFor([&] { return !view.controller.preview()->busy(); }), "Unavailable runtime did not settle");
  require(view.controller.preview()->previewErrorKind() == PreviewService::PreviewErrorKind::RuntimeUnavailable,
          "Unavailable runtime error lost");
  require(!view.controller.preview()->hasImage(), "Unavailable runtime displayed pixels");
  auto* notice = view.window->findChild<QQuickItem*>("previewErrorNotice");
  require(notice != nullptr && QTest::qWaitFor([&] { return notice->isVisible(); }),
          "Unavailable notice not presented");
  require(view.controller.handleKey(" "), "Unavailable Quick Look did not open");
  require(QTest::qWaitFor([&] { return !view.controller.preview()->quickLookBusy(); }), "Quick Look did not settle");
  require(!view.controller.preview()->hasImage(), "Unavailable Quick Look displayed pixels");
  auto* quickNotice = view.window->findChild<QQuickItem*>("quickLookErrorNotice");
  require(quickNotice != nullptr && QTest::qWaitFor([&] { return quickNotice->isVisible(); }),
          "Unavailable Quick Look notice not presented");
}
}  // namespace
int verifyIsolatedPreview(const HolonightFileBrowser::ImageRuntimeOptions& runtime, const QString& fixtures) {
  try {
    require(QGuiApplication::platformName() == "offscreen",
            "Automated acceptance requires offscreen; native checks are manual");
    require(!runtime.directory.isEmpty() && QDir(runtime.directory).exists(), "Explicit prepared runtime missing");
    for (const auto* name : {"01-red.png", "02-blue.png", "03-vector.svg"}) {
      require(QFile::exists(QDir(fixtures).filePath(name)), "Required preview fixture missing");
    }
    presentation(runtime, fixtures, false);
    presentation(runtime, fixtures, true);
    for (auto stage : {
             ThumbnailService::Stage::OriginalDecode,
             ThumbnailService::Stage::BeforeCommit,
             ThumbnailService::Stage::CacheInspect,
             ThumbnailService::Stage::CacheDecode,
         }) {
      const bool cached =
          stage == ThumbnailService::Stage::CacheInspect || stage == ThumbnailService::Stage::CacheDecode;
      cancellation(runtime, fixtures, stage, cached, false);
    }
    cancellation(runtime, fixtures, ThumbnailService::Stage::CacheDecode, true, true);
    timeout(runtime, fixtures);
    unavailable(fixtures);
    qInfo("Files cancellation, cache commits, late replies, timeout and unavailable-runtime checks passed");
    return 0;
  } catch (const std::exception& error) {
    qCritical() << "Files isolated acceptance failed:" << error.what();
    return 1;
  }
}
