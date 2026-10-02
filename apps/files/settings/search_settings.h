#pragma once

#include "settings/settings_registry.h"

struct SearchSettings {
  static inline const Setting<QStringList> kPaths{"exclude_paths", {}, "Excluded paths and subtrees"};
  static inline const Setting<QStringList> kPatterns{"exclude_directory_patterns", {}, "Excluded directory basenames"};
  QStringList paths;
  QStringList patterns;
  static void declare(SettingsRegistry& registry) {
    registry.declare(QStringLiteral("search"), kPaths);
    registry.declare(QStringLiteral("search"), kPatterns);
  }
  static SearchSettings read(const SettingsRegistry& registry) {
    return {.paths = registry.value(QStringLiteral("search"), kPaths),
            .patterns = registry.value(QStringLiteral("search"), kPatterns)};
  }
};
