#pragma once

#include "preview_service.h"

#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QSizeF>
#include <QtQml/qqmlregistration.h>

// Per-overlay presentation state. The shared preview service remains independent of card layout.
class QuickLookPresentationModel : public QObject, public QQmlParserStatus {
  Q_OBJECT
  QML_ELEMENT
  Q_INTERFACES(QQmlParserStatus)
 public:
  enum class Kind { None, Pending, Image, Text, Compact };
  Q_ENUM(Kind)

  Q_PROPERTY(PreviewService* preview READ preview WRITE setPreview NOTIFY previewChanged)
  Q_PROPERTY(QSizeF windowSize READ windowSize WRITE setWindowSize NOTIFY inputsChanged)
  Q_PROPERTY(qreal devicePixelRatio READ devicePixelRatio WRITE setDevicePixelRatio NOTIFY inputsChanged)
  Q_PROPERTY(qreal cardPadding READ cardPadding WRITE setCardPadding NOTIFY inputsChanged)
  Q_PROPERTY(qreal captionReserve READ captionReserve WRITE setCaptionReserve NOTIFY inputsChanged)
  Q_PROPERTY(qreal frameCaptionGap READ frameCaptionGap WRITE setFrameCaptionGap NOTIFY inputsChanged)
  Q_PROPERTY(qreal minCompactWidth READ minCompactWidth WRITE setMinCompactWidth NOTIFY inputsChanged)
  Q_PROPERTY(qreal iconExtent READ iconExtent WRITE setIconExtent NOTIFY inputsChanged)
  Q_PROPERTY(qreal hintImplicitWidth READ hintImplicitWidth WRITE setHintImplicitWidth NOTIFY inputsChanged)
  Q_PROPERTY(Kind kind READ kind NOTIFY presentationChanged)
  Q_PROPERTY(QSizeF cardSize READ cardSize NOTIFY presentationChanged)
  Q_PROPERTY(QSizeF frameSize READ frameSize NOTIFY presentationChanged)

  explicit QuickLookPresentationModel(QObject* parent = nullptr);
  void classBegin() override;
  void componentComplete() override;

  PreviewService* preview() const { return preview_.data(); }
  void setPreview(PreviewService* preview);
  QSizeF windowSize() const { return window_size_; }
  void setWindowSize(QSizeF value);
  qreal devicePixelRatio() const { return device_pixel_ratio_; }
  void setDevicePixelRatio(qreal value);
  qreal cardPadding() const { return card_padding_; }
  void setCardPadding(qreal value);
  qreal captionReserve() const { return caption_reserve_; }
  void setCaptionReserve(qreal value);
  qreal frameCaptionGap() const { return frame_caption_gap_; }
  void setFrameCaptionGap(qreal value);
  qreal minCompactWidth() const { return min_compact_width_; }
  void setMinCompactWidth(qreal value);
  qreal iconExtent() const { return icon_extent_; }
  void setIconExtent(qreal value);
  qreal hintImplicitWidth() const { return hint_implicit_width_; }
  void setHintImplicitWidth(qreal value);
  Kind kind() const { return kind_; }
  QSizeF cardSize() const { return card_size_; }
  QSizeF frameSize() const { return frame_size_; }

 signals:
  void previewChanged();
  void inputsChanged();
  void presentationChanged();

 private:
  friend struct QuickLookPresentationModelTestAccess;
  static Kind classify(bool hasEntry, bool busy, const QString& mimeType, bool hasText, bool hasError);
  void resetPreviewState();
  void updateGeometry();
  void reportRequestedSize();
  void inputsUpdated();
  QSizeF previewBounds() const;

  QPointer<PreviewService> preview_;
  QMetaObject::Connection preview_changed_connection_;
  QMetaObject::Connection preview_destroyed_connection_;
  bool component_complete_ = true;
  QSizeF window_size_ = QSizeF(0, 0);
  qreal device_pixel_ratio_ = 1;
  qreal card_padding_ = 0;
  qreal caption_reserve_ = 0;
  qreal frame_caption_gap_ = 0;
  qreal min_compact_width_ = 0;
  qreal icon_extent_ = 0;
  qreal hint_implicit_width_ = 0;
  Kind kind_ = Kind::None;
  Kind retained_kind_ = Kind::Compact;
  QSize retained_source_size_;
  QSizeF card_size_ = QSizeF(0, 0);
  QSizeF frame_size_ = QSizeF(0, 0);
  QSize last_requested_size_;
  int requested_size_call_count_ = 0;
};
