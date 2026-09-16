#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "geometry/Geometry.h"
#include "geometry/PolySet.h"
#include "io/export.h"

namespace {

std::shared_ptr<const Geometry> body()
{
  return std::make_shared<const PolySet>(3);
}

std::shared_ptr<const Geometry> bodies(int count)
{
  Geometry::Geometries children;
  for (int i = 0; i < count; ++i) children.emplace_back(nullptr, body());
  return std::make_shared<const GeometryList>(children);
}

}  // namespace

TEST_CASE("multi-file STL is only offered when the model has more than one body")
{
  // Nothing to separate, so the export dialog must not ask -- which is what
  // forces Qt's non-native save panel, since a native panel cannot host the
  // extra checkbox.
  CHECK_FALSE(multi_stl_available(body()));
  CHECK_FALSE(multi_stl_available(bodies(1)));
  CHECK_FALSE(multi_stl_available(nullptr));

  CHECK(multi_stl_available(bodies(2)));
  CHECK(multi_stl_available(bodies(5)));
}
