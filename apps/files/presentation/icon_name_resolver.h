#pragma once

#include <QString>
#include <QStringList>

// Pure, QObject-free derivation of the ordered freedesktop icon-name candidate chain for one
// directory entry (SPEC.md REQ-F-006, REQ-C-002). Takes only primitive stat()/name data, so it is
// unit-testable without a DirectoryModel or real filesystem access. Called on DirectoryModel's
// worker thread; the only I/O is QMimeDatabase's filename/extension glob matching (REQ-F-005),
// never content sniffing. Theme lookup of the resulting names happens later, on the GUI thread, in
// IconImageProvider (REQ-NF-001).
namespace IconNameResolver {

// Separator used to carry the whole chain in one DirectoryModel::IconNameRole string and one
// image://icon/<name1>/<name2>/... URL. Freedesktop icon names never contain '/'.
inline constexpr QChar kChainSeparator = u'/';

// `mode` is a stat() st_mode value, already resolved through symlinks by the caller. `fileName` is
// the entry's own name, used for MIME extension matching. Returns an ordered list with duplicates
// removed (first occurrence wins); never empty, always ending in the generic file fallback for
// regular files.
[[nodiscard]] QStringList candidateIconNames(quint32 mode, const QString& fileName);

// The REQ-F-007 fallback name alone, for entries whose stat() data cannot be trusted (dangling
// symlink, stat() failure) or does not exist yet (the INSERT-mode placeholder row, REQ-F-011).
[[nodiscard]] QString genericFallbackName(bool isDir);

}  // namespace IconNameResolver
