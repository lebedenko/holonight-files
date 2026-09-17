#include "places/user_dirs_parser.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>

#include <array>

namespace UserDirsParser {
namespace {

struct KeyInfo {
  Key key;
  const char* xdgName;   // the <KEY> in XDG_<KEY>_DIR
  const char* label;     // translatable source text
  const char* iconName;  // theme icon name
};

// Order matches Key's declaration order, which is also REQ-F-005's required display order.
constexpr std::array<KeyInfo, 9> kKeys{{
    {Key::Desktop, "DESKTOP", QT_TRANSLATE_NOOP("PlacesModel", "Desktop"), "user-desktop"},
    {Key::Documents, "DOCUMENTS", QT_TRANSLATE_NOOP("PlacesModel", "Documents"), "folder-documents"},
    {Key::Downloads, "DOWNLOAD", QT_TRANSLATE_NOOP("PlacesModel", "Downloads"), "folder-download"},
    {Key::Pictures, "PICTURES", QT_TRANSLATE_NOOP("PlacesModel", "Pictures"), "folder-pictures"},
    {Key::Music, "MUSIC", QT_TRANSLATE_NOOP("PlacesModel", "Music"), "folder-music"},
    {Key::Videos, "VIDEOS", QT_TRANSLATE_NOOP("PlacesModel", "Videos"), "folder-videos"},
    {Key::Projects, "PROJECTS", QT_TRANSLATE_NOOP("PlacesModel", "Projects"), "folder-development"},
    {Key::Templates, "TEMPLATES", QT_TRANSLATE_NOOP("PlacesModel", "Templates"), "folder-templates"},
    {Key::Public, "PUBLICSHARE", QT_TRANSLATE_NOOP("PlacesModel", "Public"), "folder-publicshare"},
}};

const KeyInfo* infoFor(Key key) {
  for (const auto& info : kKeys) {
    if (info.key == key) {
      return &info;
    }
  }
  return nullptr;
}

// XDG_<name>_DIR -> Key, or nullptr for an unrecognized key (silently dropped, REQ-C-007).
const KeyInfo* infoForXdgName(const QString& name) {
  for (const auto& info : kKeys) {
    if (name == QLatin1String(info.xdgName)) {
      return &info;
    }
  }
  return nullptr;
}

// REQ-F-003: \" -> ", \\ -> \, \$ -> $, \` -> `; any other character (escaped or not) is copied
// through unchanged -- no other backslash sequence is documented, so others keep their backslash.
QString unescape(const QString& value) {
  QString result;
  result.reserve(value.size());
  for (qsizetype i = 0; i < value.size(); ++i) {
    const QChar ch = value.at(i);
    if (ch == u'\\' && i + 1 < value.size()) {
      const QChar next = value.at(i + 1);
      if (next == u'"' || next == u'\\' || next == u'$' || next == u'`') {
        result += next;
        ++i;
        continue;
      }
    }
    result += ch;
  }
  return result;
}

// Resolves an unescaped value to an absolute path, or {} if it is neither $HOME-relative nor
// absolute (REQ-C-007's "This is intentional" -- other forms are silently dropped).
QString resolve(const QString& unescaped, const QString& home) {
  if (unescaped == QLatin1String("$HOME") || unescaped.startsWith(QLatin1String("$HOME/"))) {
    return QDir::cleanPath(home + unescaped.mid(5));
  }
  if (unescaped.startsWith(u'/')) {
    return QDir::cleanPath(unescaped);
  }
  return {};
}

}  // namespace

std::vector<Entry> parse(const QString& text, const QString& home) {
  // Optional leading whitespace, then XDG_<KEY>_DIR="<value>"; anything else on the line
  // (comments, blank lines, unrecognized keys, missing quotes, trailing garbage) is malformed.
  static const QRegularExpression lineForm(QStringLiteral(R"re(^\s*XDG_([A-Z_]+)_DIR="((?:[^"\\]|\\.)*)"\s*$)re"));
  const auto cleanedHome = QDir::cleanPath(home);
  std::vector<Entry> byKey(kKeys.size());
  std::vector<bool> present(kKeys.size(), false);
  for (const auto& rawLine : text.split(u'\n')) {
    const auto match = lineForm.match(rawLine);
    if (!match.hasMatch()) {
      continue;
    }
    const auto* info = infoForXdgName(match.captured(1));
    if (info == nullptr) {
      continue;
    }
    const auto resolved = resolve(unescape(match.captured(2)), home);
    if (resolved.isEmpty() || resolved == cleanedHome) {
      continue;  // malformed, or $HOME itself (REQ-F-004): disabled, no warning
    }
    const auto slot = static_cast<std::size_t>(info->key);
    byKey[slot] = Entry{.key = info->key, .path = resolved};
    present[slot] = true;
  }
  std::vector<Entry> result;
  result.reserve(byKey.size());
  for (std::size_t i = 0; i < byKey.size(); ++i) {
    if (present[i]) {
      result.push_back(byKey[i]);
    }
  }
  return result;
}

std::vector<Entry> parseFile(const QString& path, const QString& home) {
  QFile file(path);
  if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return parse(QString::fromUtf8(file.readAll()), home);
}

QString label(Key key) {
  const auto* info = infoFor(key);
  return info == nullptr ? QString() : QCoreApplication::translate("PlacesModel", info->label);
}

QString iconName(Key key) {
  const auto* info = infoFor(key);
  return info == nullptr ? QString() : QLatin1String(info->iconName);
}

}  // namespace UserDirsParser
