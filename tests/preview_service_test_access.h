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
};
