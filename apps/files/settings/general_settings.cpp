#include "settings/general_settings.h"

void GeneralSettings::declare(SettingsRegistry& registry) {
  registry.declare(QLatin1String(kSection), kRestoreLastLocation);
}

GeneralSettings GeneralSettings::read(const SettingsRegistry& registry) {
  GeneralSettings settings;
  settings.restore_last_location_ = registry.value(QLatin1String(kSection), kRestoreLastLocation);
  return settings;
}
