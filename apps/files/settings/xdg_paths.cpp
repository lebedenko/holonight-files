#include "settings/xdg_paths.h"

#include "settings/warning_sink.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>

namespace {
constexpr auto kApplicationDirectory = "holonight-files";

QString baseDirectory(const char* variable, const QString& homeRelativeFallback) {
  const auto value = qEnvironmentVariable(variable);
  if (!value.isEmpty() && QDir::isAbsolutePath(value)) {
    return QDir::cleanPath(value);
  }
  return QDir::cleanPath(QDir::homePath() + u'/' + homeRelativeFallback);
}
}  // namespace

namespace XdgPaths {
QString sessionOverridePath(const QString& configPath, WarningSink& warnings) {
  const QDir directory(QFileInfo(configPath).absolutePath());
  for (auto name : qEnvironmentVariable("XDG_CURRENT_DESKTOP").split(':', Qt::SkipEmptyParts)) {
    bool valid = true;
    for (auto& character : name) {
      const auto code = character.unicode();
      const bool allowed = (code >= 'a' && code <= 'z') || (code >= '0' && code <= '9') || code == '-' || code == '_';
      if (code >= 'A' && code <= 'Z') {
        character = QChar(code + ('a' - 'A'));
      } else if (!allowed) {
        valid = false;
      }
    }
    if (!valid) {
      warnings.warn(QCoreApplication::translate("settings", "invalid desktop name %1; ignored").arg(name));
      continue;
    }
    const auto path = directory.filePath(QStringLiteral("config-%1.toml").arg(name));
    if (QFileInfo::exists(path)) {
      return path;
    }
  }
  return {};
}

QString configFilePath() {
  return baseDirectory("XDG_CONFIG_HOME", QStringLiteral(".config")) + u'/' + QLatin1String(kApplicationDirectory) +
         QStringLiteral("/config.toml");
}
QString stateDirPath() {
  return baseDirectory("XDG_STATE_HOME", QStringLiteral(".local/state")) + u'/' + QLatin1String(kApplicationDirectory);
}
QString stateFilePath() { return stateDirPath() + QStringLiteral("/state.toml"); }
QString userDirsFilePath() {
  // Not under kApplicationDirectory: user-dirs.dirs is a shared file other apps also read/write.
  return baseDirectory("XDG_CONFIG_HOME", QStringLiteral(".config")) + QStringLiteral("/user-dirs.dirs");
}
QString dataDirPath() {
  // Under an extra holonight/ segment (unlike config/state): shared data-home namespace for
  // holonight-* applications, reserved for future cross-app data sharing.
  return baseDirectory("XDG_DATA_HOME", QStringLiteral(".local/share")) + QStringLiteral("/holonight/") +
         QLatin1String(kApplicationDirectory);
}
QString placesFilePath() { return dataDirPath() + QStringLiteral("/places.toml"); }
}  // namespace XdgPaths
