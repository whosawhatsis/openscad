#include <catch2/catch_test_macros.hpp>

#include "glview/RenderSettings.h"

TEST_CASE("OpenCASCADE backend availability follows the build", "[render-settings]")
{
#ifdef ENABLE_OPENCSCADE
  REQUIRE(renderBackend3DFromString("opencascade") == RenderBackend3D::OpenCASCADEBackend);
#else
  REQUIRE_FALSE(renderBackend3DFromString("opencascade"));
#endif
}
