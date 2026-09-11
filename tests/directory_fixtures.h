#pragma once

#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>

#include <sys/stat.h>
#include <unistd.h>

// Shared real-filesystem fixture builders for DirectoryModel/DirectoryProxyModel/
// DirectoryController tests (SPEC.md REQ-C-002: QTemporaryDir, never a mocked filesystem).
namespace files_test {

// The root user bypasses filesystem permission bits, so permission-denied assertions built on
// chmod 000 don't hold under root (REQ-C-003) — callers must skip those specific assertions when
// this is true, not the whole test.
inline bool runningAsRoot() { return ::geteuid() == 0; }

inline QString fixturePattern(const QString& label) {
  QDir().mkpath(QStringLiteral(FILES_FIXTURE_DIR));
  return QStringLiteral(FILES_FIXTURE_DIR) + "/" + label + "-XXXXXX";
}

inline QString writeFile(const QTemporaryDir& dir, const QString& name, const QByteArray& content = "x") {
  const auto path = dir.filePath(name);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
    return {};
  }
  return path;
}

inline void populateEntries(const QTemporaryDir& dir, int count, const QString& prefix = QStringLiteral("entry")) {
  for (int i = 0; i < count; ++i) {
    writeFile(dir, QStringLiteral("%1-%2.txt").arg(prefix).arg(i, 5, 10, QLatin1Char('0')));
  }
}

inline void populateUnicodeEntries(const QTemporaryDir& dir) {
  for (const auto& name : {
           QStringLiteral("café.txt"),            // precomposed accent
           QStringLiteral("éclair.txt"),          // combining acute accent, decomposed
           QStringLiteral("файл.txt"),            // Cyrillic
           QStringLiteral("ファイル.txt"),        // Japanese
           QStringLiteral("📁emoji-folder.txt"),  // emoji
       }) {
    writeFile(dir, name);
  }
}

// Built by buildPermissionFixture(): a subdirectory with all permissions removed (exercises
// REQ-F-004, the directory-itself-unreadable case), a symlink whose target lives inside that
// unreadable directory (exercises REQ-F-005 via EACCES on resolution), and a dangling symlink
// (exercises REQ-F-005 via ENOENT). A chmod 000 *file* would not exercise REQ-F-005: on POSIX,
// stat()/lstat() on a path only needs search (execute) permission on its parent directories, not
// read permission on the file itself, so a permission-denied stat failure has to come from an
// unreadable directory somewhere in the path being resolved.
struct PermissionFixture {
  QString blocked_dir;
  QString broken_perm_link;
  QString dangling_link;
};

inline PermissionFixture buildPermissionFixture(const QTemporaryDir& dir) {
  PermissionFixture fixture{.blocked_dir = dir.filePath("blocked"),
                            .broken_perm_link = dir.filePath("broken-perm-link"),
                            .dangling_link = dir.filePath("dangling-link")};
  QDir().mkpath(fixture.blocked_dir);
  const auto secret = QDir(fixture.blocked_dir).filePath("secret.txt");
  writeFile(dir, "blocked/secret.txt");
  QFile::link(secret, fixture.broken_perm_link);
  QFile::link(dir.filePath("missing-target"), fixture.dangling_link);
  ::chmod(fixture.blocked_dir.toLocal8Bit().constData(), 0);
  return fixture;
}

// Must be called before the owning QTemporaryDir goes out of scope, or its cleanup (which needs
// to traverse blocked_dir) will silently fail to remove it.
inline void restorePermissionFixture(const PermissionFixture& fixture) {
  ::chmod(fixture.blocked_dir.toLocal8Bit().constData(), 0700);
}

// Redirects $XDG_DATA_HOME to an isolated fixture directory for the lifetime of the guard,
// restoring whatever was there before — used by TrashService/TaskManager tests so they never
// touch the real ~/.local/share/Trash (SPEC.md's redirected-XDG_DATA_HOME fixture requirement).
class ScopedXdgDataHome {
 public:
  explicit ScopedXdgDataHome(const QString& path) : had_previous_(qEnvironmentVariableIsSet("XDG_DATA_HOME")) {
    if (had_previous_) {
      previous_ = qEnvironmentVariable("XDG_DATA_HOME");
    }
    qputenv("XDG_DATA_HOME", path.toLocal8Bit());
  }
  ~ScopedXdgDataHome() {
    if (had_previous_) {
      qputenv("XDG_DATA_HOME", previous_.toLocal8Bit());
    } else {
      qunsetenv("XDG_DATA_HOME");
    }
  }
  ScopedXdgDataHome(const ScopedXdgDataHome&) = delete;
  ScopedXdgDataHome& operator=(const ScopedXdgDataHome&) = delete;

 private:
  bool had_previous_;
  QString previous_;
};

}  // namespace files_test
