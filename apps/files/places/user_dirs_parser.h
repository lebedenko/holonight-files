#pragma once

#include <QString>

#include <vector>

// Pure parser for ${XDG_CONFIG_HOME:-~/.config}/user-dirs.dirs (SPEC.md REQ-F-002..006, REQ-F-010).
// No QObject, no file I/O in the parse() overload -- file reading is a one-line wrapper so tests can
// feed literal strings without touching disk.
namespace UserDirsParser {

// One of the 9 known XDG_*_DIR keys, in SPEC.md's required display order (REQ-F-005).
enum class Key { Desktop, Documents, Downloads, Pictures, Music, Videos, Projects, Templates, Public };

struct Entry {
  Key key = Key::Desktop;
  QString path;  // already unescaped, $HOME-substituted, QDir::cleanPath()'d
};

// Parses user-dirs.dirs text. home is QDir::homePath() (injected so tests don't depend on the real
// user's home). Malformed lines, unknown keys, and entries whose resolved path equals home are
// silently dropped (REQ-C-007). Result order follows Key's declaration order, not file order
// (REQ-F-005), keeping only keys actually present in the file.
std::vector<Entry> parse(const QString& text, const QString& home);

// Returns {} (parse() of empty text) if the file does not exist; this is not an error (REQ-F-010).
std::vector<Entry> parseFile(const QString& path, const QString& home);

// Translated label + theme icon name for a key (REQ-F-006), e.g. Key::Documents ->
// ("Documents", "folder-documents"). Translation context is "PlacesModel", matching the existing
// places_model_test.cpp PlaceTranslator convention.
QString label(Key key);
QString iconName(Key key);

}  // namespace UserDirsParser
