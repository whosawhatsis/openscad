#ifndef NULLGL
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include "geometry/PolySet.h"
#include "glview/OffscreenView.h"
#include "glview/PolySetRenderer.h"
#include "platform/PlatformUtils.h"

TEST_CASE("feature edge rendering restores state and reuses its buffers", "[feature-edges][.opengl]")
{
  PlatformUtils::registerApplicationPath(
    std::filesystem::path(__FILE__).parent_path().parent_path().string());
  auto mesh = std::make_shared<PolySet>(3);
  mesh->vertices = {{-10, -10, 0}, {10, -10, 0}, {10, 10, 0}, {-10, 10, 0}};
  mesh->indices = {{0, 1, 2}, {0, 2, 3}};
  OffscreenView view(64, 64);
  Camera camera;
  camera.setup({0, 0, 80, 0, 0, 0});
  camera.projection = Camera::ProjectionType::ORTHOGONAL;
  camera.pixel_width = camera.pixel_height = 64;
  view.setCamera(camera);
  view.setRenderer(std::make_shared<PolySetRenderer>(mesh));
  view.updateColorScheme();
  view.paintGL();
  const auto original = view.ctx->getFramebuffer();
  view.setAnalysisMode(AnalysisMode::Canny);
  view.paintGL();
  REQUIRE(view.feature_edge_error.empty());
  const auto edges = view.ctx->getFramebuffer();
  CHECK(edges != original);
  const auto resources = view.feature_edge_resources.get();
  view.paintGL();
  CHECK(view.feature_edge_resources.get() == resources);
  CHECK(view.ctx->getFramebuffer() == edges);
  view.setAnalysisMode(AnalysisMode::Wireframe);
  view.paintGL();
  CHECK(view.ctx->getFramebuffer() == edges);
  view.setAnalysisMode(AnalysisMode::Default);
  view.paintGL();
  CHECK(view.ctx->getFramebuffer() == original);
  view.setShowEdges(true);
  view.paintGL();
  CHECK(view.ctx->getFramebuffer() != original);
  view.setShowEdges(false);
  view.paintGL();
  CHECK(view.ctx->getFramebuffer() == original);
  CHECK(glGetError() == GL_NO_ERROR);
}
#endif
