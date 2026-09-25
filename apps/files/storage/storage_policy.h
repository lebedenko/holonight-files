#pragma once
#include <QString>

#include <StorageTypes.h>
#include <optional>

// Presentation policy for the Devices panel. Pure functions over
// StorageDrive: they read only optical, connectionBus, mediaRemovable, canEject and canPowerOff
// (REQ-C-001, REQ-C-004), so no hardware-specific identifier can steer a decision.
namespace StoragePolicy {

// Decides whether a removal control exists and which glyph it carries (REQ-F-001..003).
// Declaration order is the Devices panel's display order: internal drives first.
enum class DeviceClass { Internal, External, Optical };

inline DeviceClass classify(const HoloNight::System::StorageDrive& drive) {
  if (drive.optical) {
    return DeviceClass::Optical;  // Tested first: an optical drive on an external bus is still Optical.
  }
  // Any non-empty bus is external; the bus value itself is never compared (REQ-F-002).
  return drive.connectionBus.isEmpty() ? DeviceClass::Internal : DeviceClass::External;
}

// nullopt means the row carries no removal control (REQ-F-008). Internal drives never reach a
// drive-level verb, whatever their firmware advertises (REQ-F-009).
inline std::optional<HoloNight::System::StorageOperation> removalVerb(const HoloNight::System::StorageDrive& drive,
                                                                      bool volumeCanUnmount) {
  using Op = HoloNight::System::StorageOperation;
  const std::optional<Op> unmount = volumeCanUnmount ? std::optional{Op::Unmount} : std::nullopt;
  if (classify(drive) == DeviceClass::Internal) {
    return unmount;
  }
  // A card or disc is removed from its drive; powering off the enclosing drive is not a fallback.
  if (drive.mediaRemovable) {
    return drive.canEject ? std::optional{Op::Eject} : unmount;  // REQ-F-004, REQ-F-006
  }
  if (drive.canPowerOff) {
    return Op::PowerOff;
  }
  if (drive.canEject) {
    return Op::Eject;  // REQ-F-006
  }
  return unmount;  // REQ-F-007, REQ-F-008
}

// Freedesktop icon names (REQ-F-036).
inline QString iconName(const HoloNight::System::StorageDrive& drive) {
  switch (classify(drive)) {
    case DeviceClass::Optical:
      return QStringLiteral("media-optical-symbolic");
    case DeviceClass::External:
      return drive.mediaRemovable ? QStringLiteral("media-flash-symbolic")
                                  : QStringLiteral("drive-removable-media-symbolic");
    case DeviceClass::Internal:
      break;
  }
  return QStringLiteral("drive-harddisk-symbolic");
}

}  // namespace StoragePolicy
