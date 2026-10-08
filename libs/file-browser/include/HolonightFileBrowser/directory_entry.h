#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QString>

struct DirectoryEntry {
  QString name;
  QString absolute_path;
  // Native bytes for protocol consumers; absolute_path is a display/legacy QString.
  QByteArray native_path;
  bool is_dir = false;
  qint64 size = -1;
  QDateTime modified;
  quint32 mode = 0;
  bool stat_failed = false;
  QString stat_error;
  // IconNameResolver's candidate chain joined with IconNameResolver::kChainSeparator, computed on
  // the worker thread alongside stat(). Participates in operator== so a refresh diff that changes it
  // emits dataChanged() like any other metadata.
  QString icon_name;
  // Reserved for application-owned placeholder rows; never produced by enumeration.
  bool is_placeholder = false;
  bool is_parent = false;
  // True when the entry's own listed path is a symlink (via lstat()), regardless of whether its
  // target resolves or what type the target is. Independent of is_dir/mode, which stay
  // target-resolved so icon resolution and directories-first sort are unaffected.
  bool is_symlink = false;
  bool operator==(const DirectoryEntry&) const = default;
};
