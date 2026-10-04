#pragma once

#include "settings/settings_registry.h"

struct SearchSettings {
  static inline const Setting<QStringList> kPaths{
      .key = "exclude_paths",
      .default_value = {},
      .description = "Excluded paths and subtrees",
  };
  static inline const Setting<QStringList> kPatterns{
      .key = "exclude_directory_patterns",
      .default_value = {},
      .description = "Excluded directory basenames",
  };
  QStringList paths;
  QStringList patterns;
  static void declare(SettingsRegistry& registry) {
    registry.declare(QStringLiteral("search"), kPaths);
    registry.declare(QStringLiteral("search"), kPatterns);
  }
  static SearchSettings read(const SettingsRegistry& registry) {
    return {
        .paths = registry.value(QStringLiteral("search"), kPaths),
        .patterns = registry.value(QStringLiteral("search"), kPatterns),
    };
  }
};
