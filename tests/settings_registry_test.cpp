#include "settings/settings_registry.h"

#include <gtest/gtest.h>

namespace {
// A section that exists only in this test: declaring and reading it needs no change to the
// registry or to any production section (SPEC.md REQ-F-032).
struct TestOnlySettings {
  static constexpr auto kSection = "test_only";
  static constexpr Setting<bool> kAlpha{.key = "alpha", .default_value = true, .description = "First."};
  static constexpr Setting<bool> kBeta{.key = "beta", .default_value = false, .description = "Second."};
  static void declare(SettingsRegistry& registry) {
    registry.declare(kSection, kAlpha);
    registry.declare(kSection, kBeta);
  }
};

std::vector<TomlDiagnostic> applyText(SettingsRegistry& registry, const QByteArray& text) {
  const auto parsed = TomlDocument::parse(text);
  EXPECT_TRUE(parsed.diagnostics.empty());
  return registry.apply(parsed.document);
}

const SettingInfo* infoFor(const std::vector<SettingInfo>& infos, const QString& key) {
  for (const auto& info : infos) {
    if (info.key == key) {
      return &info;
    }
  }
  return nullptr;
}
}  // namespace

TEST(SettingsRegistry, EmptyDocumentKeepsDefaultsWithDefaultSource) {
  SettingsRegistry registry;
  TestOnlySettings::declare(registry);
  EXPECT_TRUE(applyText(registry, "").empty());
  EXPECT_TRUE(registry.value(TestOnlySettings::kSection, TestOnlySettings::kAlpha));
  EXPECT_FALSE(registry.value(TestOnlySettings::kSection, TestOnlySettings::kBeta));
  const auto infos = registry.settings();
  ASSERT_EQ(infos.size(), 2U);
  EXPECT_EQ(infos[0].section, "test_only");
  EXPECT_EQ(infos[0].description, "First.");
  EXPECT_EQ(infos[0].type, TomlValue::Type::Bool);
  EXPECT_EQ(infos[0].source, SettingSource::Default);
}

TEST(SettingsRegistry, ConfiguredValueAppliesAndRecordsConfigFileSource) {
  SettingsRegistry registry;
  TestOnlySettings::declare(registry);
  EXPECT_TRUE(applyText(registry, "[test_only]\nbeta = true\n").empty());
  EXPECT_TRUE(registry.value(TestOnlySettings::kSection, TestOnlySettings::kBeta));
  const auto infos = registry.settings();
  EXPECT_EQ(infoFor(infos, "beta")->source, SettingSource::ConfigFile);
  EXPECT_EQ(infoFor(infos, "alpha")->source, SettingSource::Default);
}

TEST(SettingsRegistry, WrongTypeFallsBackToDefaultWithOneWarningAndOtherKeysStillApply) {
  SettingsRegistry registry;
  TestOnlySettings::declare(registry);
  const auto diagnostics = applyText(registry, "[test_only]\nalpha = \"no\"\nbeta = true\n");
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_EQ(diagnostics[0].kind, TomlDiagnostic::Kind::WrongType);
  EXPECT_EQ(diagnostics[0].line, 2);
  EXPECT_TRUE(diagnostics[0].message.contains("[test_only] alpha"));
  EXPECT_TRUE(diagnostics[0].message.contains("expected a boolean"));
  EXPECT_TRUE(registry.value(TestOnlySettings::kSection, TestOnlySettings::kAlpha));
  EXPECT_TRUE(registry.value(TestOnlySettings::kSection, TestOnlySettings::kBeta));
  EXPECT_EQ(infoFor(registry.settings(), "alpha")->source, SettingSource::Default);
}

TEST(SettingsRegistry, UnknownKeysAndSectionsWarnByPathAndAreIgnored) {
  SettingsRegistry registry;
  TestOnlySettings::declare(registry);
  const auto diagnostics =
      applyText(registry, "stray = 1\n[test_only]\nbeta = true\ntypo = 2\n[mystery]\nkey = 3\n[empty]\n");
  ASSERT_EQ(diagnostics.size(), 4U);
  for (const auto& diagnostic : diagnostics) {
    EXPECT_EQ(diagnostic.kind, TomlDiagnostic::Kind::UnknownEntry);
  }
  EXPECT_TRUE(diagnostics[0].message.contains("stray"));
  EXPECT_TRUE(diagnostics[1].message.contains("[test_only] typo"));
  EXPECT_TRUE(diagnostics[2].message.contains("[mystery] key"));
  EXPECT_TRUE(diagnostics[3].message.contains("[empty]"));
  EXPECT_TRUE(registry.value(TestOnlySettings::kSection, TestOnlySettings::kBeta));
}

TEST(SettingsRegistry, DeclaredSectionGivenAsScalarIsReported) {
  SettingsRegistry registry;
  TestOnlySettings::declare(registry);
  const auto diagnostics = applyText(registry, "test_only = true\n");
  ASSERT_EQ(diagnostics.size(), 1U);
  EXPECT_TRUE(diagnostics[0].message.contains("[test_only] must be a table"));
  EXPECT_TRUE(registry.value(TestOnlySettings::kSection, TestOnlySettings::kAlpha));
}
