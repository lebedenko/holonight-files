#include "native_preview_observer.h"

#include "preview_image_item.h"
#include "preview_service_test_access.h"

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQuickItem>
#include <QSGRendererInterface>
#include <QScreen>

#include <utility>

namespace {
QJsonArray dimensions(QSize size) { return {size.width(), size.height()}; }
QJsonObject memoryUsage() {
  QFile status(QStringLiteral("/proc/self/status"));
  QJsonObject result;
  if (status.open(QIODevice::ReadOnly)) {
    for (const auto& line : status.readAll().split('\n')) {
      if (line.startsWith("VmRSS:") || line.startsWith("VmHWM:")) {
        result.insert(line.startsWith("VmRSS:") ? "rss_kib" : "peak_rss_kib",
                      line.simplified().split(' ').value(1).toLongLong());
      }
    }
  }
  return result;
}
}  // namespace

NativePreviewObserver::NativePreviewObserver(DirectoryController& controller, QElapsedTimer clock)
    : controller_(controller), clock_(clock), output_(qEnvironmentVariable("FILES_NATIVE_EVIDENCE")) {
  if (!output_.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    qFatal("Native preview lab requires a new FILES_NATIVE_EVIDENCE path");
  }
  write({{"event", "start"}, {"qt", qVersion()}, {"platform", QGuiApplication::platformName()}});
  PreviewServiceTestAccess::beforeFullDecode(*controller_.preview(), [counter = attempts_] { ++*counter; });
  connect(controller_.preview(), &PreviewService::changed, this, [this] { snapshot("preview"); });
  connect(&controller_, &DirectoryController::changed, this, [this] { snapshot("controller"); });
  connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
    snapshot("quit");
    write({{"event", "end"}});
    output_.flush();
    ended_ = true;
    timer_.stop();
  });
  connect(&timer_, &QTimer::timeout, this, [this] {
    const auto now = clock_.nsecsElapsed();
    max_gap_ = std::max(max_gap_, now - last_tick_);
    last_tick_ = now;
    ++ticks_;
    if (ticks_ % 10 == 0) {
      snapshot("sample");
    }
  });
  last_tick_ = clock_.nsecsElapsed();
  timer_.start(10);
  // Observer wiring check only; native sessions must always be closed by the operator.
  if (QGuiApplication::platformName() == QStringLiteral("offscreen")) {
    const int duration = qEnvironmentVariableIntValue("FILES_NATIVE_EXIT_AFTER_MS");
    if (duration > 0) {
      QTimer::singleShot(duration, &controller_, &DirectoryController::shutdown);
    }
  }
}

void NativePreviewObserver::attach(QObject* root) {
  window_ = qobject_cast<QQuickWindow*>(root);
  if (!window_) {
    qFatal("Native preview lab requires the real Quick window");
  }
  connect(window_, &QQuickWindow::frameSwapped, this, [counter = frames_] { ++*counter; }, Qt::DirectConnection);
  connect(window_, &QWindow::windowStateChanged, this, [this] { snapshot("window"); });
  for (const auto& name : {"previewImageArea", "quickLookImageArea"}) {
    auto* item = root->findChild<QQuickItem*>(QString::fromLatin1(name));
    if (item == nullptr) {
      qFatal("Native preview lab cannot find a preview consumer");
    }
    connect(item, &QQuickItem::visibleChanged, this, [this] { snapshot("visibility"); });
    connect(item, &QQuickItem::widthChanged, this, [this] { snapshot("geometry"); }, Qt::QueuedConnection);
    connect(item, &QQuickItem::heightChanged, this, [this] { snapshot("geometry"); }, Qt::QueuedConnection);
    for (auto* image : item->findChildren<PreviewImageItem*>()) {
      connect(image, &PreviewImageItem::imageChanged, this, [this] { snapshot("consumer"); }, Qt::QueuedConnection);
    }
  }
  snapshot("attached");
}

void NativePreviewObserver::write(QJsonObject event) {
  event.insert("ns", clock_.nsecsElapsed());
  const auto line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
  if (output_.write(line) != line.size()) {
    qFatal("Native preview evidence write failed");
  }
}

void NativePreviewObserver::snapshot(const QString& reason) {
  if (ended_) {
    return;
  }
  const auto& preview = *controller_.preview();
  const auto pixels = preview.image();
  QJsonObject event{{"event", "snapshot"},
                    {"reason", reason},
                    {"path", PreviewServiceTestAccess::path(preview)},
                    {"name", preview.name()},
                    {"file_bytes", preview.size()},
                    {"modified_ms", preview.modified().toMSecsSinceEpoch()},
                    {"source", dimensions(preview.sourcePixelSize())},
                    {"decoded", dimensions(pixels.size())},
                    {"image_key", QString::number(pixels.cacheKey())},
                    {"busy", preview.busy()},
                    {"error", static_cast<int>(preview.previewErrorKind())},
                    {"requested", dimensions(PreviewServiceTestAccess::requestedSize(preview))},
                    {"pane_requested", dimensions(PreviewServiceTestAccess::paneSize(preview))},
                    {"quick_requested", dimensions(PreviewServiceTestAccess::quickLookRequestedSize(preview))},
                    {"resize_pending", PreviewServiceTestAccess::resizePending(preview)},
                    {"quick_open", controller_.quickLookOpen()},
                    {"decode_attempts", attempts_->load()},
                    {"frames", frames_->load()},
                    {"max_gui_gap_ns", max_gap_}};
  if (window_) {
    event.insert("dpr", window_->devicePixelRatio());
    event.insert("backend", static_cast<int>(window_->rendererInterface()->graphicsApi()));
    event.insert("window_state", static_cast<int>(window_->windowState()));
    event.insert("window", QJsonArray{window_->x(), window_->y(), window_->width(), window_->height()});
    if (window_->screen() != nullptr) {
      event.insert("screen", window_->screen()->name());
      event.insert("refresh_hz", window_->screen()->refreshRate());
    }
    QJsonObject consumers;
    for (const auto& name : {"previewImageArea", "quickLookImageArea"}) {
      const auto* item = window_->findChild<QQuickItem*>(QString::fromLatin1(name));
      if (item != nullptr) {
        const auto position = item->mapToScene(QPointF());
        const auto* image = item->findChild<PreviewImageItem*>();
        consumers.insert(
            name, QJsonObject{{"logical", QJsonArray{position.x(), position.y(), item->width(), item->height()}},
                              {"visible", item->isVisible()},
                              {"image_key", image != nullptr ? QString::number(image->image().cacheKey()) : "0"}});
      }
    }
    event.insert("consumers", consumers);
  }
  event.insert("memory", memoryUsage());
  write(std::move(event));
}
