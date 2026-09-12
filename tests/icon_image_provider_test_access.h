#pragma once

#include "icon_image_provider.h"

// Test-only view of IconImageProvider's theme-lookup counter (REQ-F-020/021). Mirrors
// DirectoryModelTestAccess/PreviewServiceTestAccess.
struct IconImageProviderTestAccess {
  static int themeLookupCount(const IconImageProvider& provider) { return provider.theme_lookup_count_for_test_; }
};
