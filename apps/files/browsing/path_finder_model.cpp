#include "path_finder_model.h"

#include "path_index_store.h"
#include "settings/general_settings.h"
#include "settings/search_settings.h"
#include "settings/warning_sink.h"
#include "settings/xdg_paths.h"

PathFinderModel::PathFinderModel(QObject* parent) : BrowserPathFinderModel(parent) { configure(); }
PathFinderModel::PathFinderModel(PathScanFunction scanner, QObject* parent)
    : BrowserPathFinderModel(std::move(scanner), parent) {
  configure();
}
void PathFinderModel::configure() {
  setCachePath(PathIndexStore::cachePath());
  StderrWarningSink selectionWarnings;
  const auto basePath = XdgPaths::configFilePath();
  const auto overridePath = XdgPaths::sessionOverridePath(basePath, selectionWarnings);
  setConfigurationLoader([basePath, overridePath] {
    SearchConfiguration result;
    result.valid = true;
    SettingsRegistry registry;
    GeneralSettings::declare(registry);
    SearchSettings::declare(registry);
    for (const auto& path : {basePath, overridePath}) {
      if (path.isEmpty()) {
        continue;
      }
      const auto parsed = TomlDocument::parseFile(path);
      if (!parsed.diagnostics.empty()) {
        result.valid = false;
      }
      for (const auto& diagnostic : parsed.diagnostics) {
        result.diagnostics.append(QStringLiteral("%1:%2: %3")
                                      .arg(path)
                                      .arg(diagnostic.line)
                                      .arg(tr("Cannot read search configuration (%1). Retaining the last valid policy.")
                                               .arg(diagnostic.message)));
      }
      const auto source = path == basePath ? SettingSource::ConfigFile : SettingSource::SessionOverride;
      if (parsed.diagnostics.empty()) {
        for (const auto& diagnostic : registry.apply(parsed.document, source)) {
          result.diagnostics.append(QStringLiteral("%1:%2: %3").arg(path).arg(diagnostic.line).arg(diagnostic.message));
        }
      }
    }
    if (result.valid) {
      result.exclusions = SearchSettings::read(registry);
    }
    StderrWarningSink warnings;
    for (const auto& diagnostic : result.diagnostics) {
      warnings.warn(diagnostic);
    }
    return result;
  });
}
