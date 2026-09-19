#include "preview_image_item.h"

#include <QPainter>
#include <QPainterPath>

PreviewImageItem::PreviewImageItem(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void PreviewImageItem::setImage(const QImage& image) {
  if (image_.cacheKey() == image.cacheKey()) {
    return;
  }
  image_ = image;
  update();
  emit imageChanged();
}

void PreviewImageItem::setRadius(qreal radius) {
  radius = qMax(qreal{0}, radius);
  if (qFuzzyIsNull(radius_ - radius)) {
    return;
  }
  radius_ = radius;
  update();
  emit radiusChanged();
}

void PreviewImageItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
  QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
  if (newGeometry.size() != oldGeometry.size()) {
    update();
  }
}

QRectF PreviewImageItem::fitRect(QSize image, QSizeF canvas) {
  if (image.isEmpty() || canvas.isEmpty()) {
    return {};
  }
  const auto scaled = QSizeF(image).scaled(canvas, Qt::KeepAspectRatio);
  const QPointF origin((canvas.width() - scaled.width()) / 2.0, (canvas.height() - scaled.height()) / 2.0);
  return {origin, scaled};
}

void PreviewImageItem::paint(QPainter* painter) {
  if (image_.isNull()) {
    return;
  }
  const auto destination = fitRect(image_.size(), boundingRect().size());
  painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
  if (radius_ > 0) {
    // Clip inside paint() rather than with a layer or a clipping Rectangle: this stays in the
    // item's own DPR-correct buffer and gets antialiased edges (DESIGN.md §5.3).
    painter->setRenderHint(QPainter::Antialiasing, true);
    QPainterPath clip;
    clip.addRoundedRect(destination, radius_, radius_);
    painter->setClipPath(clip);
  }
  painter->drawImage(destination, image_);
}
