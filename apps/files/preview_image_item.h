#pragma once

#include <QImage>
#include <QQuickPaintedItem>
#include <QtQml/qqmlregistration.h>

// A small QQuickPaintedItem, the same base class holonight-viewer's ImageCanvas uses: Qt Quick has
// no built-in way to bind a QML Image element's source to a live, in-memory QImage that changes at
// arbitrary times. Unlike ImageCanvas, PreviewImageItem has no pan/zoom/magnification state —
// Stage 2 preview is read-only and non-interactive (SPEC.md REQ-C-006).
class PreviewImageItem : public QQuickPaintedItem {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)
 public:
  explicit PreviewImageItem(QQuickItem* parent = nullptr);
  QImage image() const { return image_; }
  void setImage(const QImage& image);
  void paint(QPainter* painter) override;
  static QRectF fitRect(QSize image, QSizeF canvas);

 signals:
  void imageChanged();

 protected:
  void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

 private:
  QImage image_;
};
