#pragma once

#include "preview_service.h"

// Internal synchronization seam, configured before a target dispatches its worker job. Mirrors
// DirectoryModelTestAccess: a test-only hook, not part of the production API surface.
struct PreviewServiceTestAccess {
  static void beforeFullDecode(PreviewService& service, std::function<void()> callback) {
    service.before_full_decode_for_test_ = std::move(callback);
  }
  static void beforeDispatch(PreviewService& service, std::function<void()> callback) {
    service.before_dispatch_for_test_ = std::move(callback);
  }
  static QString path(const PreviewService& service) { return service.path_; }
  static QSize requestedSize(const PreviewService& service) { return service.requested_size_; }
  static QSize paneSize(const PreviewService& service) { return service.pane_size_; }
  static bool resizePending(const PreviewService& service) { return service.resize_debounce_timer_.isActive(); }
  // The size last passed to setRequestedSize(QuickLook, ...), whether or not it is the active one.
  static QSize quickLookRequestedSize(const PreviewService& service) { return service.quick_look_size_; }
};
