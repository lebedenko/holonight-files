#include "quick_look_presentation_model.h"

#include <algorithm>
#include <cmath>

namespace {
qreal nonnegative(qreal value) { return std::isfinite(value) ? std::max(qreal{0}, value) : 0; }
}  // namespace

QuickLookPresentationModel::QuickLookPresentationModel(QObject* parent) : QObject(parent) {}

void QuickLookPresentationModel::classBegin() { component_complete_ = false; }

void QuickLookPresentationModel::componentComplete() {
  component_complete_ = true;
  updateGeometry();
  reportRequestedSize();
}

void QuickLookPresentationModel::setPreview(PreviewService* preview) {
  if (preview_ == preview) {
    return;
  }
  disconnect(preview_changed_connection_);
  disconnect(preview_destroyed_connection_);
  preview_ = preview;
  if (preview_ != nullptr) {
    preview_changed_connection_ =
        connect(preview_, &PreviewService::changed, this, &QuickLookPresentationModel::updateGeometry);
    preview_destroyed_connection_ = connect(preview_, &QObject::destroyed, this, [this] {
      preview_ = nullptr;
      resetPreviewState();
      emit previewChanged();
    });
  }
  resetPreviewState();
  emit previewChanged();
}

void QuickLookPresentationModel::resetPreviewState() {
  retained_kind_ = Kind::Compact;
  retained_source_size_ = {};
  last_requested_size_ = {};
  updateGeometry();
  reportRequestedSize();
}

void QuickLookPresentationModel::setWindowSize(QSizeF value) {
  value = QSizeF(nonnegative(value.width()), nonnegative(value.height()));
  if (window_size_ == value) {
    return;
  }
  window_size_ = value;
  inputsUpdated();
}

void QuickLookPresentationModel::setDevicePixelRatio(qreal value) {
  value = std::isfinite(value) && value > 0 ? value : 1;
  if (device_pixel_ratio_ == value) {
    return;
  }
  device_pixel_ratio_ = value;
  inputsUpdated();
}

void QuickLookPresentationModel::setCardPadding(qreal value) {
  value = nonnegative(value);
  if (card_padding_ == value) {
    return;
  }
  card_padding_ = value;
  inputsUpdated();
}

void QuickLookPresentationModel::setCaptionReserve(qreal value) {
  value = nonnegative(value);
  if (caption_reserve_ == value) {
    return;
  }
  caption_reserve_ = value;
  inputsUpdated();
}

void QuickLookPresentationModel::setFrameCaptionGap(qreal value) {
  value = nonnegative(value);
  if (frame_caption_gap_ == value) {
    return;
  }
  frame_caption_gap_ = value;
  inputsUpdated();
}

void QuickLookPresentationModel::setMinCompactWidth(qreal value) {
  value = nonnegative(value);
  if (min_compact_width_ == value) {
    return;
  }
  min_compact_width_ = value;
  inputsUpdated();
}

void QuickLookPresentationModel::setIconExtent(qreal value) {
  value = nonnegative(value);
  if (icon_extent_ == value) {
    return;
  }
  icon_extent_ = value;
  inputsUpdated();
}

void QuickLookPresentationModel::setHintImplicitWidth(qreal value) {
  value = nonnegative(value);
  if (hint_implicit_width_ == value) {
    return;
  }
  hint_implicit_width_ = value;
  inputsUpdated();
}

void QuickLookPresentationModel::inputsUpdated() {
  updateGeometry();
  reportRequestedSize();
  emit inputsChanged();
}

QuickLookPresentationModel::Kind QuickLookPresentationModel::classify(bool hasEntry, bool busy, const QString& mimeType,
                                                                      bool hasText, bool hasError) {
  if (!hasEntry) {
    return Kind::None;
  }
  if (hasError) {
    return Kind::Compact;
  }
  // setTarget() publishes metadata before dispatch sets busy: empty MIME already means pending.
  if (mimeType.isEmpty()) {
    return Kind::Pending;
  }
  if (mimeType.startsWith(QStringLiteral("image/"))) {
    return Kind::Image;
  }
  if (hasText) {
    return Kind::Text;
  }
  return busy ? Kind::Pending : Kind::Compact;
}

QSizeF QuickLookPresentationModel::previewBounds() const {
  return {
      std::max(qreal{0}, (window_size_.width() * 0.92) - (2 * card_padding_)),
      std::max(qreal{0}, (window_size_.height() * 0.92) - (2 * card_padding_) - frame_caption_gap_ - caption_reserve_)};
}

void QuickLookPresentationModel::updateGeometry() {
  const auto kind = preview_ != nullptr
                        ? classify(preview_->hasEntry(), preview_->busy(), preview_->mimeType(), preview_->hasText(),
                                   preview_->previewErrorKind() != PreviewService::PreviewErrorKind::None)
                        : Kind::None;
  if (kind != Kind::Pending && kind != Kind::None) {
    retained_kind_ = kind;
    retained_source_size_ = preview_->sourcePixelSize();
  }
  const auto bounds = window_size_ * 0.92;
  QSizeF card;
  QSizeF frame;
  if (retained_kind_ == Kind::Compact) {
    frame = QSizeF(icon_extent_, icon_extent_);
    card = QSizeF(
        std::floor(std::min(bounds.width(), std::max({min_compact_width_, hint_implicit_width_ + (2 * card_padding_),
                                                      icon_extent_ + (2 * card_padding_)}))),
        std::floor(
            std::min(bounds.height(), (2 * card_padding_) + icon_extent_ + frame_caption_gap_ + caption_reserve_)));
  } else {
    frame = previewBounds();
    if (retained_kind_ == Kind::Image && retained_source_size_.width() > 0 && retained_source_size_.height() > 0) {
      // Preserve the previous zero-bounds result and allow small images to upscale.
      frame = frame.isEmpty() ? QSizeF(0, 0) : QSizeF(retained_source_size_).scaled(frame, Qt::KeepAspectRatio);
    }
    frame = QSizeF(std::floor(frame.width()), std::floor(frame.height()));
    card =
        QSizeF(std::floor(std::min(bounds.width(), std::max(frame.width() + (2 * card_padding_), min_compact_width_))),
               std::floor(std::min(bounds.height(),
                                   frame.height() + (2 * card_padding_) + frame_caption_gap_ + caption_reserve_)));
  }
  const bool changed = kind_ != kind || card_size_ != card || frame_size_ != frame;
  kind_ = kind;
  card_size_ = card;
  frame_size_ = frame;
  if (changed) {
    emit presentationChanged();
  }
}

void QuickLookPresentationModel::reportRequestedSize() {
  if (!component_complete_ || preview_ == nullptr) {
    return;
  }
  const auto bounds = previewBounds();
  const QSize pixels(qRound(bounds.width() * device_pixel_ratio_), qRound(bounds.height() * device_pixel_ratio_));
  if (pixels.isEmpty() || pixels == last_requested_size_) {
    return;
  }
  // Cache before calling out: service signals must never feed geometry back into decode requests.
  last_requested_size_ = pixels;
  ++requested_size_call_count_;
  preview_->setRequestedSize(PreviewService::PreviewConsumer::QuickLook, pixels);
}
