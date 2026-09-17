#include "state/state_store.h"

#include "settings/toml_document.h"
#include "warning_sink.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <utility>

namespace {
constexpr auto kNavigationSection = "navigation";
constexpr auto kLastLocationKey = "last_location";
constexpr auto kVersionKey = "version";

QString tr(const char* text) { return QCoreApplication::translate("state", text); }

// mkdir -p where only directories this call creates get mode 0700 (XDG: "0700").
bool makeStateDirectory(const QString& path, QString& error) {
  const auto encoded = QFile::encodeName(path);
  struct stat info{};
  if (::stat(encoded.constData(), &info) == 0) {
    if (!S_ISDIR(info.st_mode)) {
      error = QString::fromLocal8Bit(std::strerror(ENOTDIR));
      return false;
    }
    return true;
  }
  const auto parent = QFileInfo(path).path();
  if (parent != path && !makeStateDirectory(parent, error)) {
    return false;
  }
  if (::mkdir(encoded.constData(), 0700) != 0 && errno != EEXIST) {
    error = QString::fromLocal8Bit(std::strerror(errno));
    return false;
  }
  return true;
}
}  // namespace

StateStore::StateStore(QString stateFilePath, QString stateDirPath)
    : file_path_(std::move(stateFilePath)), dir_path_(std::move(stateDirPath)) {}

StateLoadResult StateStore::load(WarningSink& warnings) const {
  const auto parsed = TomlDocument::parseFile(file_path_);
  if (!parsed.file_exists) {
    return {};
  }
  const auto ignore = [&](const QString& problem) {
    warnings.warn(QStringLiteral("%1: %2; ignoring stored state").arg(file_path_, problem));
    return StateLoadResult{};
  };
  if (!parsed.diagnostics.empty()) {
    const auto& error = parsed.diagnostics.front();
    return ignore(error.line > 0 ? tr("line %1: %2").arg(error.line).arg(error.message) : error.message);
  }
  const auto version = parsed.document.value({}, QLatin1String(kVersionKey));
  if (version.type != TomlValue::Type::Integer || version.int_value != kVersion) {
    return ignore(tr("unsupported state version"));
  }
  const auto location = parsed.document.value(QLatin1String(kNavigationSection), QLatin1String(kLastLocationKey));
  if (location.type != TomlValue::Type::String) {
    return ignore(tr("missing or invalid [navigation] last_location"));
  }
  if (!QDir::isAbsolutePath(location.string_value)) {
    return ignore(tr("last_location is not an absolute path"));  // REQ-C-007
  }
  return {.last_location = QDir::cleanPath(location.string_value)};
}

bool StateStore::save(const QString& lastLocation, WarningSink& warnings) const {
  const auto fail = [&](const QString& problem) {
    warnings.warn(QStringLiteral("%1: %2").arg(file_path_, tr("cannot save state (%1)").arg(problem)));
    return false;
  };
  QString error;
  if (!makeStateDirectory(dir_path_, error)) {
    return fail(error);
  }
  const auto content = QStringLiteral("%1 = %2\n\n[%3]\n%4 = %5\n")
                           .arg(QLatin1String(kVersionKey))
                           .arg(kVersion)
                           .arg(QLatin1String(kNavigationSection), QLatin1String(kLastLocationKey),
                                TomlDocument::quoteString(lastLocation))
                           .toUtf8();
  QSaveFile file(file_path_);
  if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
    return fail(file.errorString());
  }
  if (!file.commit()) {
    return fail(file.errorString());
  }
  return true;
}
