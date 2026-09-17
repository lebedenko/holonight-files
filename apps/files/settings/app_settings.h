#pragma once

#include "settings/general_settings.h"
#include "settings/settings_registry.h"

#include <QString>

#include <vector>

class WarningSink;

// Immutable, read-only view of config.toml (SPEC.md REQ-F-031). Loaded once in main() before any
// folder opens (REQ-C-005); config.toml is only ever read, never created or written (REQ-C-003).
// Adding a section means one member, one accessor and one declare()/read() line in load().
class AppSettings {
 public:
  static AppSettings load(const QString& configPath, WarningSink& warnings);
  static AppSettings defaults();

  const GeneralSettings& general() const { return general_; }
  const std::vector<SettingInfo>& settingInfo() const { return info_; }

 private:
  AppSettings() = default;
  static SettingsRegistry declareAll();
  void readAll(const SettingsRegistry& registry);
  GeneralSettings general_;
  std::vector<SettingInfo> info_;
};
