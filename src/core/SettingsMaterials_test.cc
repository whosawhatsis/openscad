#include <catch2/catch_test_macros.hpp>

#include "core/Settings.h"

// The Preferences table is stored as one "name=color;..." string. It is only
// ever read in the GUI - the CLI does not load user preferences - so this is
// where its parsing is pinned down.
TEST_CASE("material default colors are looked up by name")
{
  const auto previous = Settings::SettingsMaterials::materialColors.value();
  Settings::SettingsMaterials::materialColors.setValue("PLA=#ffff00ff;PETG=yellow;ABS=#0000ff");

  CHECK(Settings::SettingsMaterials::defaultColor("PLA") == "#ffff00ff");
  CHECK(Settings::SettingsMaterials::defaultColor("PETG") == "yellow");
  CHECK(Settings::SettingsMaterials::defaultColor("ABS") == "#0000ff");

  // Absent, and not a prefix or suffix match of anything present.
  CHECK(Settings::SettingsMaterials::defaultColor("NYLON").empty());
  CHECK(Settings::SettingsMaterials::defaultColor("PL").empty());
  CHECK(Settings::SettingsMaterials::defaultColor("LA").empty());
  CHECK(Settings::SettingsMaterials::defaultColor("").empty());

  Settings::SettingsMaterials::materialColors.setValue("");
  CHECK(Settings::SettingsMaterials::defaultColor("PLA").empty());

  // A malformed entry is skipped rather than derailing the ones after it.
  Settings::SettingsMaterials::materialColors.setValue("junk;PLA=red");
  CHECK(Settings::SettingsMaterials::defaultColor("PLA") == "red");

  Settings::SettingsMaterials::materialColors.setValue(previous);
}
