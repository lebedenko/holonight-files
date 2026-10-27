#include "settings/app_settings.h"

#include "settings/xdg_paths.h"
#include "warning_sink.h"

#include <QCoreApplication>

namespace {
QString located(const QString& path, int line, const QString& message) {
  return line > 0 ? QStringLiteral("%1:%2: %3").arg(path).arg(line).arg(message)
                  : QStringLiteral("%1: %2").arg(path, message);
}
}  // namespace

SettingsRegistry AppSettings::declareAll() {
  SettingsRegistry registry;
  GeneralSettings::declare(registry);
  SearchSettings::declare(registry);
  HeaderSettings::declare(registry);
  return registry;
}

void AppSettings::readAll(const SettingsRegistry& registry) {
  header_ = HeaderSettings::read(registry);
  general_ = GeneralSettings::read(registry);
  search_ = SearchSettings::read(registry);
  info_ = registry.settings();
}

AppSettings AppSettings::defaults() {
  AppSettings settings;
  settings.readAll(declareAll());
  return settings;
}

AppSettings AppSettings::load(const QString& configPath, WarningSink& warnings) {
  return load(configPath, XdgPaths::sessionOverridePath(configPath, warnings), warnings);
}

AppSettings AppSettings::load(const QString& configPath, const QString& overridePath, WarningSink& warnings) {
  auto registry = declareAll();
  for (const auto& path : {configPath, overridePath}) {
    if (path.isEmpty()) {
      continue;
    }
    const auto parsed = TomlDocument::parseFile(path);
    if (!parsed.diagnostics.empty()) {
      const auto& error = parsed.diagnostics.front();
      warnings.warn(
          located(path, error.line,
                  QCoreApplication::translate("settings", "cannot read configuration (%1); retaining previous values")
                      .arg(error.message)));
      continue;
    }
    const auto source = path == configPath ? SettingSource::ConfigFile : SettingSource::SessionOverride;
    for (const auto& diagnostic : registry.apply(parsed.document, source)) {
      warnings.warn(located(path, diagnostic.line, diagnostic.message));
    }
  }
  AppSettings settings;
  settings.readAll(registry);
  return settings;
}
