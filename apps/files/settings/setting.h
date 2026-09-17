#pragma once

// A typed setting declaration (SPEC.md REQ-F-029): pure data, grouped one file per config section.
template <typename T>
struct Setting {
  const char* key;
  T default_value;
  const char* description;
};
