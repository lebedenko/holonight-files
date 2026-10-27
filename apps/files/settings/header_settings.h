#pragma once

#include "settings/setting.h"
#include "settings/settings_registry.h"

class HeaderSettings {
 public:
  static constexpr Setting<bool> kShowCloseButton{
      .key = "show_close_button",
      .default_value = true,
      .description = "Show the application-header close button.",
  };
  static void declare(SettingsRegistry& registry) { registry.declare(QStringLiteral("header"), kShowCloseButton); }
  static HeaderSettings read(const SettingsRegistry& registry) {
    HeaderSettings settings;
    settings.show_close_button_ = registry.value(QStringLiteral("header"), kShowCloseButton);
    return settings;
  }
  [[nodiscard]] bool showCloseButton() const { return show_close_button_; }

 private:
  bool show_close_button_ = true;
};
