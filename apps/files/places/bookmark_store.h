#pragma once

#include "warning_sink.h"

#include <QString>

#include <vector>

// Reads $XDG_DATA_HOME/holonight/holonight-files/places.toml through the TomlDocument adapter only
// (SPEC.md REQ-C-003). One warning per distinct problem, all via WarningSink (REQ-C-007/008).
namespace BookmarkStore {

struct Bookmark {
  QString name;  // resolved: explicit "name" or the path's directory base name (REQ-F-008)
  QString path;  // absolute, cleaned with QDir::cleanPath (REQ-F-009)
};

// Missing file -> {} silently (REQ-F-011). Unparseable TOML, missing/mismatched version -> {} plus
// exactly one warning (REQ-F-012/013). Otherwise returns the valid entries, in file order, having
// already skipped invalid entries and warned once per skip/unknown-key (REQ-F-014/015). Does not
// deduplicate against Home/XDG/earlier bookmarks -- that is PlacesModel's job.
std::vector<Bookmark> read(const QString& path, WarningSink& warnings);

}  // namespace BookmarkStore
