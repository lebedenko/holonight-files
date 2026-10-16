#include "path_index_store.h"

#include <QDir>
QString PathIndexStore::cachePath() {
  const auto value = qEnvironmentVariable("XDG_CACHE_HOME");
  const auto base = QDir::isAbsolutePath(value) ? value : QDir::homePath() + QStringLiteral("/.cache");
  return QDir::cleanPath(base) + QStringLiteral("/holonight-files/search/home-v1.index");
}
