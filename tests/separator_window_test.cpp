// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Andrii L <lebeden@gmail.com>

#include "directory_controller.h"
#include "directory_fixtures.h"
#include "engine_setup.h"

#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQmlApplicationEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QTest>

#include <algorithm>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>

namespace {

// Render the real Files item tree without the offscreen platform's incorrectly sized
// accelerated backing buffer. The production window still owns its layout and dimensions.
class FilesGpuCapture {
 public:
  Q_DISABLE_COPY_MOVE(FilesGpuCapture)
  explicit FilesGpuCapture(QQuickWindow* source) : source_(source) {
    if (!context_.create()) {
      return;
    }
    surface_.setFormat(context_.format());
    surface_.create();
    if (!context_.makeCurrent(&surface_)) {
      return;
    }
    qInfo("Files OpenGL renderer: %s", context_.functions()->glGetString(GL_RENDERER));
    // OpenGL exposes its UTF-8 renderer name as unsigned bytes.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
    const QByteArray renderer(reinterpret_cast<const char*>(context_.functions()->glGetString(GL_RENDERER)));
    if (renderer.toLower().contains("llvmpipe") || renderer.toLower().contains("softpipe")) {
      return;
    }
    window_ = std::make_unique<QQuickWindow>(&control_);
    window_->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(&context_));
    if (!control_.initialize()) {
      return;
    }
    children_ = source_->contentItem()->childItems();
    for (auto* child : children_) {
      child->setParentItem(window_->contentItem());
    }
    ready_ = true;
  }
  ~FilesGpuCapture() {
    for (auto* child : children_) {
      child->setParentItem(source_->contentItem());
    }
    context_.makeCurrent(&surface_);
    control_.invalidate();
    window_.reset();
  }
  [[nodiscard]] bool ready() const { return ready_; }
  QImage grab() {
    QCoreApplication::processEvents();
    context_.makeCurrent(&surface_);
    const qreal dpr = source_->effectiveDevicePixelRatio();
    window_->setColor(source_->color());
    window_->resize(source_->size());
    window_->contentItem()->setSize(source_->size());
    const QSize pixels = source_->size() * dpr;
    if (!buffer_ || buffer_->size() != pixels) {
      auto buffer = std::make_unique<QOpenGLFramebufferObject>(pixels, QOpenGLFramebufferObject::CombinedDepthStencil);
      auto target = QQuickRenderTarget::fromOpenGLTexture(buffer->texture(), pixels);
      target.setDevicePixelRatio(dpr);
      window_->setRenderTarget(target);
      QEvent changed(QEvent::DevicePixelRatioChange);
      QCoreApplication::sendEvent(window_.get(), &changed);
      buffer_ = std::move(buffer);
    }
    control_.polishItems();
    control_.beginFrame();
    control_.sync();
    control_.render();
    control_.endFrame();
    return buffer_->toImage();
  }

 private:
  QQuickWindow* source_;
  QOpenGLContext context_;
  QOffscreenSurface surface_;
  std::unique_ptr<QOpenGLFramebufferObject> buffer_;
  QQuickRenderControl control_;
  std::unique_ptr<QQuickWindow> window_;
  QList<QQuickItem*> children_;
  bool ready_ = false;
};

qreal pixelEdge(qreal logical, qreal dpr) { return std::floor((logical * dpr) + 0.5); }

QRectF expectedStroke(QQuickItem* separator, qreal dpr) {
  const QPointF first = separator->mapToScene({});
  const QPointF last = separator->mapToScene(QPointF(separator->width(), separator->height()));
  const bool vertical = separator->property("orientation").toInt() == Qt::Vertical;
  const bool trailing = separator->property("crossAxisAlignment").toInt() == 2;
  const qreal left = vertical && trailing ? pixelEdge(last.x(), dpr) - 1 : pixelEdge(first.x(), dpr);
  const qreal top = !vertical && trailing ? pixelEdge(last.y(), dpr) - 1 : pixelEdge(first.y(), dpr);
  return {left, top, vertical ? 1 : pixelEdge(last.x(), dpr) - left, vertical ? pixelEdge(last.y(), dpr) - top : 1};
}

void expectJunctionPixels(const QImage& background, const QImage& rendered, const QList<QQuickItem*>& separators,
                          const QList<QRectF>& bounds) {
  // Check complete junction neighborhoods, including the surrounding unpainted pixels.
  QList<QPointF> junctions = {{bounds[1].center().x(), bounds[0].bottom()},
                              {bounds[1].center().x(), bounds[5].top()},
                              bounds[2].topLeft(),
                              bounds[2].topRight()};
  for (int column : {3, 4}) {
    junctions.append(QPointF(bounds[column].center().x(), bounds[0].bottom()));
    junctions.append(QPointF(bounds[column].center().x(), bounds[2].top()));
  }
  for (const auto& junction : junctions) {
    for (int pixel_y = qFloor(junction.y()) - 4; pixel_y <= qFloor(junction.y()) + 4; ++pixel_y) {
      for (int pixel_x = qFloor(junction.x()) - 4; pixel_x <= qFloor(junction.x()) + 4; ++pixel_x) {
        if (!rendered.rect().contains(pixel_x, pixel_y)) {
          continue;
        }
        const auto owners = std::count_if(separators.cbegin(), separators.cend(), [&](QQuickItem* separator) {
          return separator->isVisible() &&
                 bounds[separators.indexOf(separator)].contains(QPointF(pixel_x + 0.5, pixel_y + 0.5));
        });
        ASSERT_LE(owners, 1) << "overlap at " << pixel_x << ',' << pixel_y;
        const QColor base = background.pixelColor(pixel_x, pixel_y);
        const QColor actual = rendered.pixelColor(pixel_x, pixel_y);
        const auto expected = [owners](int value) { return owners ? value + ((255 - value) * 0.5) : value; };
        ASSERT_NEAR(actual.red(), expected(base.red()), 2) << "red at " << pixel_x << ',' << pixel_y;
        ASSERT_NEAR(actual.green(), expected(base.green()), 2) << "green at " << pixel_x << ',' << pixel_y;
        ASSERT_NEAR(actual.blue(), expected(base.blue()), 2) << "blue at " << pixel_x << ',' << pixel_y;
      }
    }
  }
}

}  // namespace

TEST(Files, WindowSeparatorJunctions) {
  QTemporaryDir directory(files_test::fixturePattern("separator-junctions"));
  ASSERT_FALSE(files_test::writeFile(directory, "example.txt").isEmpty());
  DirectoryController controller;
  QQmlApplicationEngine engine;
  initializeFilesEngine(engine);
  QObject::connect(&engine, &QQmlEngine::warnings, &engine, [](const QList<QQmlError>& warnings) {
    for (const auto& warning : warnings) {
      if (warning.description().contains(QStringLiteral("Binding loop"))) {
        ADD_FAILURE() << warning.toString().toStdString();
      }
    }
  });
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  controller.open(directory.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  ASSERT_TRUE(QTest::qWaitForWindowExposed(window));
  const qreal dpr = window->effectiveDevicePixelRatio();
  const bool gpu = qEnvironmentVariable("FILES_SEPARATOR_BACKEND") == QStringLiteral("opengl");
  ASSERT_EQ(window->rendererInterface()->graphicsApi(),
            gpu ? QSGRendererInterface::OpenGL : QSGRendererInterface::Software);
  if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
    ASSERT_DOUBLE_EQ(dpr, qEnvironmentVariable("QT_SCALE_FACTOR").toDouble());
  }
  qInfo("Files separator backend=%d effectiveDpr=%g", window->rendererInterface()->graphicsApi(), dpr);
  std::unique_ptr<FilesGpuCapture> gpu_capture;
  if (gpu) {
    window->hide();
    gpu_capture = std::make_unique<FilesGpuCapture>(window);
    ASSERT_TRUE(gpu_capture->ready()) << "A real OpenGL renderer is required; fallback is not accepted";
  }
  const auto grab = [&] { return gpu_capture ? gpu_capture->grab() : window->grabWindow(); };
  QList<QQuickItem*> separators;
  for (const char* name : {"headerDivider", "sidebarDivider", "columnHeaderDivider", "sizeColumnDivider",
                           "modifiedColumnDivider", "footerDivider"}) {
    auto* item = window->findChild<QQuickItem*>(name);
    ASSERT_NE(item, nullptr) << name;
    ASSERT_EQ(item->property("thickness").toInt(), 1) << name;
    separators.append(item);
  }
  const auto original = separators.first()->property("color").value<QColor>();
  auto* listing = window->findChild<QQuickItem*>("directoryListing");
  ASSERT_NE(listing, nullptr);
  for (int width : {1000, 850, 740, 420, 1000}) {
    SCOPED_TRACE(::testing::Message() << "width=" << width << " DPR=" << dpr);
    window->resize(width, 500);
    // grabWindow synchronizes and polishes the real application layout before inspection.
    ASSERT_TRUE(QTest::qWaitFor([&] { return grab().size() == QSize(qRound(width * dpr), qRound(500 * dpr)); }));
    EXPECT_EQ(separators[3]->isVisible(), listing->property("showSize").toBool());
    EXPECT_EQ(separators[4]->isVisible(), listing->property("showModified").toBool());
    QList<QRectF> bounds;
    for (auto* item : separators) {
      bounds.append(expectedStroke(item, dpr));
      item->setOpacity(0);
    }
    const QImage background = grab();
    ASSERT_FALSE(background.isNull());
    for (auto* item : separators) {
      item->setProperty("color", QColor(Qt::white));
      item->setOpacity(0.5);
    }
    const QImage rendered = grab();
    ASSERT_EQ(rendered.size(), QSize(qRound(width * dpr), qRound(500 * dpr)));
    ASSERT_EQ(background.size(), rendered.size());
    EXPECT_DOUBLE_EQ(bounds[0].bottom(), bounds[1].top());
    EXPECT_DOUBLE_EQ(bounds[1].bottom(), bounds[5].top());
    for (int column : {3, 4}) {
      if (!separators[column]->isVisible()) {
        continue;
      }
      EXPECT_DOUBLE_EQ(bounds[0].bottom(), bounds[column].top());
      EXPECT_DOUBLE_EQ(bounds[column].bottom(), bounds[2].top());
    }
    expectJunctionPixels(background, rendered, separators, bounds);
    for (auto* item : separators) {
      item->setProperty("color", original);
      item->setOpacity(1);
    }
    const QString capture = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
    if (!capture.isEmpty()) {
      EXPECT_TRUE(grab().save(capture + QStringLiteral("-junctions-%1.png").arg(width)));
    }
  }
}
