#pragma once

#include "settings/setting.h"
#include "settings/settings_registry.h"

// The [general] config section. Adding a setting here touches only this file (SPEC.md REQ-F-032).
class GeneralSettings {
 public:
  static constexpr auto kSection = "general";
  static constexpr Setting<bool> kRestoreLastLocation{
      .key = "restore_last_location",
      .default_value = false,
      .description = "Reopen the last local folder when started without a folder argument."};

  static void declare(SettingsRegistry& registry);
  static GeneralSettings read(const SettingsRegistry& registry);

  bool restoreLastLocation() const { return restore_last_location_; }

 private:
  bool restore_last_location_ = kRestoreLastLocation.default_value;
};
