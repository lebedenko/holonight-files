#pragma once

#include <Qt>

#include <cstdint>

struct DirectoryRoles {
  // Qt model roles/QML properties require implicit integer conversion.
  // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
  enum Role : std::uint16_t {
    NameRole = Qt::UserRole + 1,
    PathRole,
    IsDirRole,
    SizeRole,
    ModifiedRole,
    ModeRole,
    IsHiddenRole,
    StatFailedRole,
    StatErrorRole,
    // SPEC.md REQ-C-001 (main-view-icons): the '/'-joined icon-name candidate chain, consumed from
    // QML only as "image://icon/" + iconName and split apart only by IconImageProvider.
    IconNameRole,
    IsParentRole,
    IsSymlinkRole,
    NativePathRole,
  };
};
