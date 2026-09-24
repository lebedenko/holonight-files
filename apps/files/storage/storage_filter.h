#pragma once
#include <QDir>
#include <QSet>

#include <StorageTypes.h>

namespace StorageFilter {
inline bool infrastructure(const HoloNight::System::StorageVolume& volume) {
  static const QSet<QString> purposes{"c12a7328-f81f-11d2-ba4b-00a0c93ec93b",
                                      "21686148-6449-6e6f-744e-656564454649",
                                      "bc13c2ff-59e6-4262-a352-b275fd6f7172",
                                      "0657fd6d-a4ab-43c4-84e5-0933c84b4f4f",
                                      "933ac7e1-2eb4-4f13-b844-0e14e2aef915",
                                      "4f68bce3-e8cd-4db1-96e7-fbcaf984b709",
                                      "44479540-f297-41b2-9af7-d131d5f0458a",
                                      "b921b045-1df0-41c3-af44-4c6f280d3fae",
                                      "8484680c-9521-48c6-9c11-b0720656f69e",
                                      "4d21b016-b534-45c2-a9fb-5c16e091fd2d",
                                      "3b8f8425-20e0-4f3b-907f-1a25a76f98e8",
                                      "0x82",
                                      "0xef"};
  if (purposes.contains(volume.partitionType.toLower())) return true;
  for (const auto& mount : volume.mountPoints) {
    const auto path = QDir::cleanPath(mount);
    if (path == "/") return true;
    if (path.startsWith("/run/media/")) continue;
    for (const auto* directory : {"/home", "/boot", "/efi", "/usr", "/var", "/etc", "/opt", "/bin", "/sbin", "/lib",
                                  "/lib64", "/run", "/tmp", "/root", "/srv"}) {
      if (path == QLatin1String(directory) || path.startsWith(QString::fromLatin1(directory) + '/')) return true;
    }
  }
  return false;
}
inline bool eligible(const HoloNight::System::StorageVolume& volume) {
  return !volume.hintIgnore && !volume.loop && !volume.partitionContainer && !infrastructure(volume) &&
         (volume.usage == "filesystem" || volume.locked);
}
}  // namespace StorageFilter
