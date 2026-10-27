#include "settings/files_preferences.h"

#include "settings/xdg_paths.h"

#include <QFileInfo>

FilesPreferences::FilesPreferences(QObject* parent) : QObject(parent), config_path_(XdgPaths::configFilePath()) {
  initialize(qEnvironmentVariable("XDG_CURRENT_DESKTOP"));
}

FilesPreferences::FilesPreferences(const QString& configPath, const QString& desktop, WarningSink& warnings,
                                   QObject* parent)
    : QObject(parent), warnings_(&warnings), config_path_(QFileInfo(configPath).absoluteFilePath()) {
  initialize(desktop);
}

void FilesPreferences::initialize(const QString& desktop) {
  candidates_ = XdgPaths::sessionOverrideCandidates(config_path_, desktop, *warnings_);
  debounce_.setSingleShot(true);
  debounce_.setInterval(100);
  connect(&watcher_, &QFileSystemWatcher::fileChanged, &debounce_, qOverload<>(&QTimer::start));
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, &debounce_, qOverload<>(&QTimer::start));
  connect(&debounce_, &QTimer::timeout, this, &FilesPreferences::reload);
  reload();
}

void FilesPreferences::reload() {
  QString selected;
  for (const auto& path : candidates_) {
    if (QFileInfo::exists(path)) {
      selected = path;
      break;
    }
  }
  const bool previous = showCloseButton();
  settings_ = AppSettings::load(config_path_, selected, *warnings_);
  refreshWatches();
  if (previous != showCloseButton()) {
    emit showCloseButtonChanged();
  }
}

void FilesPreferences::refreshWatches() {
  if (!watcher_.files().isEmpty()) {
    watcher_.removePaths(watcher_.files());
  }
  if (!watcher_.directories().isEmpty()) {
    watcher_.removePaths(watcher_.directories());
  }
  QStringList paths = candidates_;
  paths.append(config_path_);
  QStringList watches;
  for (auto path : paths) {
    while (!path.isEmpty()) {
      if (QFileInfo::exists(path) && !watches.contains(path)) {
        watches.append(path);
      }
      const auto parent = QFileInfo(path).absolutePath();
      if (parent == path) {
        break;
      }
      path = parent;
    }
  }
  if (!watches.isEmpty()) {
    watcher_.addPaths(watches);
  }
}
