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
  setConfigurationLoader([] {
    SearchConfiguration result;
    const auto path = XdgPaths::configFilePath();
    const auto parsed = TomlDocument::parseFile(path);
    result.valid = parsed.diagnostics.empty();
    for (const auto& diagnostic : parsed.diagnostics) {
      result.diagnostics.append(QStringLiteral("%1:%2: %3")
                                    .arg(path)
                                    .arg(diagnostic.line)
                                    .arg(tr("Cannot read search configuration (%1). Retaining the last valid policy.")
                                             .arg(diagnostic.message)));
    }
    if (result.valid) {
      SettingsRegistry registry;
      GeneralSettings::declare(registry);
      SearchSettings::declare(registry);
      for (const auto& diagnostic : registry.apply(parsed.document)) {
        result.diagnostics.append(QStringLiteral("%1:%2: %3").arg(path).arg(diagnostic.line).arg(diagnostic.message));
      }
      result.exclusions = SearchSettings::read(registry);
    }
    StderrWarningSink warnings;
    for (const auto& diagnostic : result.diagnostics) {
      warnings.warn(diagnostic);
    }
    return result;
  });
}
