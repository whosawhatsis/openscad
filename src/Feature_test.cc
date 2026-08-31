// The flag that gates process isolation.
//
// It is the switch a user actually reaches for, and the one MainWindow reads to decide whether a
// window computes in its own process. What matters here is that it exists under the name the
// documentation and the command line use, and that it is off unless asked for -- an experimental
// feature that defaulted to on would change every user's behaviour on upgrade.

#include "Feature.h"

#include <catch2/catch_all.hpp>
#include <algorithm>
#include <string>

TEST_CASE("Process isolation is an experimental feature, off by default", "[Feature]")
{
  CHECK(Feature::ExperimentalProcessIsolation.get_name() == "process-isolation");
  CHECK_FALSE(Feature::ExperimentalProcessIsolation.is_enabled());

  SECTION("it can be enabled and disabled by name")
  {
    // The name is what --enable takes and what the preference stores, so it has to work as a
    // string and not only as the symbol.
    Feature::enable_feature("process-isolation");
    CHECK(Feature::ExperimentalProcessIsolation.is_enabled());
    Feature::enable_feature("process-isolation", false);
    CHECK_FALSE(Feature::ExperimentalProcessIsolation.is_enabled());
  }

  SECTION("it is listed with the other experimental features")
  {
    // --enable prints this list, so a feature missing from it is undiscoverable.
    const auto found = std::find_if(Feature::begin(), Feature::end(), [](const Feature *feature) {
      return feature->get_name() == "process-isolation";
    });
    CHECK(found != Feature::end());
  }

  SECTION("it describes itself")
  {
    // Shown in the preferences list and by --enable; an empty description leaves a user guessing.
    CHECK_FALSE(Feature::ExperimentalProcessIsolation.get_description().empty());
  }
}
