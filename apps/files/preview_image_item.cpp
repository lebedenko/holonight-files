#include "preview_image_item.h"

#include <QPainter>

PreviewImageItem::PreviewImageItem(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void PreviewImageItem::setImage(const QImage& image) {
  if (image_.cacheKey() == image.cacheKey()) {
    return;
  }
  image_ = image;
  update();
  emit imageChanged();
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
  painter->drawImage(destination, image_);
}
