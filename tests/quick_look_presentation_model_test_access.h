#pragma once

#include "quick_look_presentation_model.h"

// Test-only access; no counters or classification hooks are exposed to QML.
struct QuickLookPresentationModelTestAccess {
  static int requestedSizeCallCount(const QuickLookPresentationModel& model) {
    return model.requested_size_call_count_;
  }
  static QuickLookPresentationModel::Kind classify(bool hasEntry, bool busy, const QString& mimeType, bool hasText,
                                                   bool hasError) {
    return QuickLookPresentationModel::classify(hasEntry, busy, mimeType, hasText, hasError);
  }
};
