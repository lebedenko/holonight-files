#pragma once

#include "settings/setting.h"
#include "settings/toml_document.h"

#include <QString>

#include <vector>

enum class SettingSource { Default, ConfigFile };

// What a future settings UI renders for one declared setting (SPEC.md REQ-F-030).
struct SettingInfo {
  QString section;
  QString key;
  QString description;
  TomlValue::Type type = TomlValue::Type::Missing;
  SettingSource source = SettingSource::Default;
};

// Generic validation engine (SPEC.md REQ-F-004/005/030/032). Sections declare their settings, apply()
// resolves every declared value against a document once, and sections read the typed values back.
// Holds no section- or key-specific names; a new value type adds one declare()/value() overload.
class SettingsRegistry {
 public:
  void declare(const QString& section, const Setting<bool>& setting);
  // Wrong-typed declared keys fall back to their defaults; undeclared sections and keys are
  // reported and ignored. Diagnostics are in source order.
  std::vector<TomlDiagnostic> apply(const TomlDocument& document);
  bool value(const QString& section, const Setting<bool>& setting) const;
  std::vector<SettingInfo> settings() const;

 private:
  struct Entry {
    SettingInfo info;
    TomlValue resolved;
  };
  const Entry* find(const QString& section, const QString& key) const;
  bool declaresSection(const QString& section) const;
  std::vector<Entry> entries_;
};
