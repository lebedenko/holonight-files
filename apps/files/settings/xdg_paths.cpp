#include "settings/xdg_paths.h"

#include <QDir>

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
QString configFilePath() {
  return baseDirectory("XDG_CONFIG_HOME", QStringLiteral(".config")) + u'/' + QLatin1String(kApplicationDirectory) +
         QStringLiteral("/config.toml");
}
QString stateDirPath() {
  return baseDirectory("XDG_STATE_HOME", QStringLiteral(".local/state")) + u'/' + QLatin1String(kApplicationDirectory);
}
QString stateFilePath() { return stateDirPath() + QStringLiteral("/state.toml"); }
}  // namespace XdgPaths
