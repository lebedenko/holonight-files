#include "fs_isolation.h"

#include <QDir>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>

namespace fs_isolation {
namespace {

bool writeProcFile(const char* path, const QByteArray& content, QString* error) {
  const int procFd = ::open(path, O_WRONLY);  // NOLINT(cppcoreguidelines-pro-type-vararg)
  if (procFd < 0) {
    *error = QStringLiteral("open %1: %2").arg(path).arg(std::strerror(errno));
    return false;
  }
  const bool succeeded = ::write(procFd, content.constData(), content.size()) == content.size();
  if (!succeeded) {
    *error = QStringLiteral("write %1: %2").arg(path).arg(std::strerror(errno));
  }
  ::close(procFd);
  return succeeded;
}

// Function-local static rather than a namespace-scope global.
std::atomic_int& mountCounter() {
  static std::atomic_int counter{0};
  return counter;
}

}  // namespace

SetupResult setUp() {
  SetupResult result;
  const uid_t uid = ::getuid();
  const gid_t gid = ::getgid();
  if (::unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0) {
    result.unavailableReason = QStringLiteral("unshare(CLONE_NEWUSER|CLONE_NEWNS): %1").arg(std::strerror(errno));
    return result;
  }
  QString error;
  if (!writeProcFile("/proc/self/uid_map", QByteArrayLiteral("0 ") + QByteArray::number(uid) + " 1\n", &error) ||
      !writeProcFile("/proc/self/setgroups", QByteArrayLiteral("deny"), &error) ||
      !writeProcFile("/proc/self/gid_map", QByteArrayLiteral("0 ") + QByteArray::number(gid) + " 1\n", &error)) {
    result.unavailableReason = error;
    return result;
  }
  if (::mount(nullptr, "/", nullptr, MS_REC | MS_PRIVATE, nullptr) != 0) {
    result.unavailableReason = QStringLiteral("mount --make-rprivate: %1").arg(std::strerror(errno));
    return result;
  }
  result.available = true;
  return result;
}

QString mountFreshTmpfs(const char* options) {
  auto path = QDir::tempPath() +
              QStringLiteral("/holonight-files-fsops-%1-%2").arg(::getpid()).arg(mountCounter().fetch_add(1));
  if (!QDir().mkpath(path)) {
    return {};
  }
  if (::mount("tmpfs", path.toLocal8Bit().constData(), "tmpfs", 0, options) != 0) {
    return {};
  }
  return path;
}

}  // namespace fs_isolation
