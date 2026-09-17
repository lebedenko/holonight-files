#include "places/bookmark_store.h"

#include "settings/toml_document.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace BookmarkStore {
namespace {

constexpr auto kVersionKey = "version";
constexpr auto kBookmarksKey = "bookmarks";
constexpr auto kPathKey = "path";
constexpr auto kNameKey = "name";
constexpr int kVersion = 1;

// "~/..." (home-relative) or "/..." (absolute) only (REQ-F-009); anything else (relative, $VAR,
// bare "~") is rejected and reported by the caller.
QString resolvePath(const QString& raw) {
  if (raw.startsWith(QLatin1String("~/"))) {
    return QDir::cleanPath(QDir::homePath() + raw.mid(1));
  }
  if (raw.startsWith(u'/')) {
    return QDir::cleanPath(raw);
  }
  return {};
}

}  // namespace

std::vector<Bookmark> read(const QString& path, WarningSink& warnings) {
  const auto parsed = TomlDocument::parseFile(path);
  if (!parsed.file_exists) {
    return {};  // REQ-F-011: silent, no file created
  }
  if (!parsed.diagnostics.empty()) {
    const auto& error = parsed.diagnostics.front();
    warnings.warn(QStringLiteral("%1: line %2: %3").arg(path).arg(error.line).arg(error.message));
    return {};  // REQ-F-012
  }
  const auto& doc = parsed.document;
  const auto version = doc.value({}, QLatin1String(kVersionKey));
  if (version.type != TomlValue::Type::Integer || version.int_value != kVersion) {
    warnings.warn(QStringLiteral("%1: unsupported places version").arg(path));
    return {};  // REQ-F-013
  }
  // Unknown top-level keys (REQ-F-015): anything other than "version"/"bookmarks". An array of
  // tables (bookmarks) is not itself a table node, so it surfaces from rootKeys(), not sections().
  for (const auto& key : doc.rootKeys()) {
    if (key == QLatin1String(kVersionKey) || key == QLatin1String(kBookmarksKey)) {
      continue;
    }
    warnings.warn(QStringLiteral("%1: line %2: unknown key \"%3\"").arg(path).arg(doc.value({}, key).line).arg(key));
  }
  for (const auto& key : doc.sections()) {
    if (key == QLatin1String(kBookmarksKey)) {
      continue;
    }
    warnings.warn(QStringLiteral("%1: line %2: unknown key \"%3\"").arg(path).arg(doc.sectionLine(key)).arg(key));
  }
  std::vector<Bookmark> result;
  const auto count = doc.arrayOfTablesSize(QLatin1String(kBookmarksKey));
  result.reserve(static_cast<std::size_t>(std::max(0, count)));
  for (int i = 0; i < count; ++i) {
    const auto line = doc.arrayOfTablesLine(QLatin1String(kBookmarksKey), i);
    const auto pathValue = doc.arrayOfTablesValue(QLatin1String(kBookmarksKey), i, QLatin1String(kPathKey));
    if (pathValue.type != TomlValue::Type::String) {
      warnings.warn(QStringLiteral("%1: line %2: bookmark missing a valid \"path\"").arg(path).arg(line));
      continue;  // REQ-F-014
    }
    const auto resolved = resolvePath(pathValue.string_value);
    if (resolved.isEmpty()) {
      warnings.warn(QStringLiteral("%1: line %2: bookmark path \"%3\" is not absolute or ~/-relative")
                        .arg(path)
                        .arg(line)
                        .arg(pathValue.string_value));
      continue;  // REQ-F-009/014
    }
    const auto nameValue = doc.arrayOfTablesValue(QLatin1String(kBookmarksKey), i, QLatin1String(kNameKey));
    const auto name =
        nameValue.type == TomlValue::Type::String ? nameValue.string_value : QFileInfo(resolved).fileName();
    for (const auto& key : doc.arrayOfTablesKeys(QLatin1String(kBookmarksKey), i)) {
      if (key == QLatin1String(kPathKey) || key == QLatin1String(kNameKey)) {
        continue;
      }
      warnings.warn(QStringLiteral("%1: line %2: unknown key \"%3\" in bookmark").arg(path).arg(line).arg(key));
    }
    result.push_back({.name = name, .path = resolved});  // REQ-F-015
  }
  return result;
}

}  // namespace BookmarkStore
