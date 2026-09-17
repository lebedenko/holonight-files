#include "settings/app_settings.h"

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
  return registry;
}

void AppSettings::readAll(const SettingsRegistry& registry) {
  general_ = GeneralSettings::read(registry);
  info_ = registry.settings();
}

AppSettings AppSettings::defaults() {
  AppSettings settings;
  settings.readAll(declareAll());
  return settings;
}

AppSettings AppSettings::load(const QString& configPath, WarningSink& warnings) {
  const auto parsed = TomlDocument::parseFile(configPath);
  if (!parsed.file_exists) {
    return defaults();
  }
  if (!parsed.diagnostics.empty()) {
    const auto& error = parsed.diagnostics.front();
    warnings.warn(located(
        configPath, error.line,
        QCoreApplication::translate("settings", "cannot read configuration (%1); using defaults").arg(error.message)));
    return defaults();
  }
  auto registry = declareAll();
  for (const auto& diagnostic : registry.apply(parsed.document)) {
    warnings.warn(located(configPath, diagnostic.line, diagnostic.message));
  }
  AppSettings settings;
  settings.readAll(registry);
  return settings;
}
