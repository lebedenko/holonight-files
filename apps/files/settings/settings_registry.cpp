#include "settings/settings_registry.h"

#include <QCoreApplication>

#include <algorithm>

namespace {
QString typeName(TomlValue::Type type) {
  switch (type) {
    case TomlValue::Type::Bool:
      return QStringLiteral("a boolean");
    case TomlValue::Type::String:
      return QStringLiteral("a string");
    case TomlValue::Type::Integer:
      return QStringLiteral("an integer");
    case TomlValue::Type::Missing:
    case TomlValue::Type::Other:
      break;
  }
  return QStringLiteral("another type");
}

QString keyPath(const QString& section, const QString& key) {
  return section.isEmpty() ? key : QStringLiteral("[%1] %2").arg(section, key);
}

TomlDiagnostic unknownEntry(const QString& what, int line) {
  return {.kind = TomlDiagnostic::Kind::UnknownEntry,
          .message = QCoreApplication::translate("settings", "unknown %1; ignored").arg(what),
          .line = line};
}
}  // namespace

void SettingsRegistry::declare(const QString& section, const Setting<bool>& setting) {
  Entry entry{.info = {.section = section,
                       .key = QString::fromUtf8(setting.key),
                       .description = QString::fromUtf8(setting.description),
                       .type = TomlValue::Type::Bool,
                       .source = SettingSource::Default},
              .resolved = {.type = TomlValue::Type::Bool, .bool_value = setting.default_value}};
  entries_.push_back(std::move(entry));
}

std::vector<TomlDiagnostic> SettingsRegistry::apply(const TomlDocument& document) {
  std::vector<TomlDiagnostic> diagnostics;
  for (const auto& key : document.rootKeys()) {
    const auto line = document.value({}, key).line;
    if (declaresSection(key)) {
      diagnostics.push_back(
          {.kind = TomlDiagnostic::Kind::WrongType,
           .message = QCoreApplication::translate("settings", "[%1] must be a table; ignored").arg(key),
           .line = line});
    } else {
      diagnostics.push_back(unknownEntry(QCoreApplication::translate("settings", "key %1").arg(key), line));
    }
  }
  for (const auto& section : document.sections()) {
    const auto keys = document.keys(section);
    if (!declaresSection(section) && keys.empty()) {
      diagnostics.push_back(unknownEntry(QCoreApplication::translate("settings", "section [%1]").arg(section),
                                         document.sectionLine(section)));
    }
    for (const auto& key : keys) {
      const auto found = document.value(section, key);
      if (find(section, key) == nullptr) {
        diagnostics.push_back(
            unknownEntry(QCoreApplication::translate("settings", "key %1").arg(keyPath(section, key)), found.line));
      }
    }
  }
  for (auto& entry : entries_) {
    const auto found = document.value(entry.info.section, entry.info.key);
    if (found.type == TomlValue::Type::Missing) {
      continue;
    }
    if (found.type != entry.info.type) {
      diagnostics.push_back(
          {.kind = TomlDiagnostic::Kind::WrongType,
           .message =
               QCoreApplication::translate("settings", "%1: expected %2, found %3; using the default")
                   .arg(keyPath(entry.info.section, entry.info.key), typeName(entry.info.type), typeName(found.type)),
           .line = found.line});
      continue;
    }
    entry.resolved = found;
    entry.info.source = SettingSource::ConfigFile;
  }
  std::ranges::stable_sort(diagnostics, {}, &TomlDiagnostic::line);
  return diagnostics;
}

bool SettingsRegistry::value(const QString& section, const Setting<bool>& setting) const {
  const auto* entry = find(section, QString::fromUtf8(setting.key));
  return entry == nullptr ? setting.default_value : entry->resolved.bool_value;
}

std::vector<SettingInfo> SettingsRegistry::settings() const {
  std::vector<SettingInfo> infos;
  infos.reserve(entries_.size());
  for (const auto& entry : entries_) {
    infos.push_back(entry.info);
  }
  return infos;
}

const SettingsRegistry::Entry* SettingsRegistry::find(const QString& section, const QString& key) const {
  const auto found = std::ranges::find_if(
      entries_, [&](const Entry& entry) { return entry.info.section == section && entry.info.key == key; });
  return found == entries_.end() ? nullptr : &*found;
}

bool SettingsRegistry::declaresSection(const QString& section) const {
  return std::ranges::any_of(entries_, [&](const Entry& entry) { return entry.info.section == section; });
}
