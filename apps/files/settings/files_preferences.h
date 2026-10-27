#pragma once

#include "settings/app_settings.h"
#include "settings/warning_sink.h"

#include <QFileSystemWatcher>
#include <QObject>
#include <QTimer>

class FilesPreferences : public QObject {
  Q_OBJECT
  Q_PROPERTY(bool showCloseButton READ showCloseButton NOTIFY showCloseButtonChanged)
 public:
  explicit FilesPreferences(QObject* parent = nullptr);
  FilesPreferences(const QString& configPath, const QString& desktop, WarningSink& warnings, QObject* parent = nullptr);
  [[nodiscard]] bool showCloseButton() const { return settings_.header().showCloseButton(); }
  [[nodiscard]] const AppSettings& settings() const { return settings_; }
 signals:
  void showCloseButtonChanged();

 private:
  void reload();
  void refreshWatches();
  StderrWarningSink default_warnings_;
  WarningSink* warnings_ = &default_warnings_;
  QString config_path_;
  QStringList candidates_;
  AppSettings settings_ = AppSettings::defaults();
  QFileSystemWatcher watcher_;
  QTimer debounce_;
  void initialize(const QString& desktop);
};
