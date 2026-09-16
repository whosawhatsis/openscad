#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "geometry/PolySet.h"
#include "geometry/PolySetBuilder.h"
#include "geometry/smooth_normals.h"

namespace {

// A unit cube. Every edge is 90 degrees, so every edge is sharp at any sane threshold.
std::unique_ptr<PolySet> makeCube()
{
  PolySetBuilder builder;
  const Vector3d v[8] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                         {0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}};
  const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                           {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}};
  for (const auto& f : faces) {
    builder.beginPolygon(4);
    for (const int i : f) builder.addVertex(v[i]);
  }
  return builder.build();
}

// A closed prism approximating a cylinder: the side walls meet at 360/sides degrees.
std::unique_ptr<PolySet> makePrism(int sides)
{
  PolySetBuilder builder;
  std::vector<Vector3d> bottom, top;
  for (int i = 0; i < sides; ++i) {
    const double a = 2.0 * M_PI * i / sides;
    bottom.emplace_back(std::cos(a), std::sin(a), 0.0);
    top.emplace_back(std::cos(a), std::sin(a), 1.0);
  }
  for (int i = 0; i < sides; ++i) {
    const int j = (i + 1) % sides;
    builder.beginPolygon(4);
    builder.addVertex(bottom[i]);
    builder.addVertex(bottom[j]);
    builder.addVertex(top[j]);
    builder.addVertex(top[i]);
  }
  builder.beginPolygon(sides);
  for (int i = sides - 1; i >= 0; --i) builder.addVertex(bottom[i]);
  builder.beginPolygon(sides);
  for (int i = 0; i < sides; ++i) builder.addVertex(top[i]);
  return builder.build();
}

size_t cornerCount(const PolySet& ps)
{
  size_t n = 0;
  for (const auto& face : ps.indices) n += face.size();
  return n;
}

}  // namespace

TEST_CASE("a cube keeps its face normals", "[smooth_normals]")
{
  const auto cube = makeCube();
  const auto normals = computeSmoothNormals(*cube, 24.0);
  REQUIRE(normals.size() == cornerCount(*cube));

  // The regression this design exists to prevent: averaging per vertex position
  // instead of per corner rounds every corner of a cube.
  size_t corner = 0;
  for (const auto& face : cube->indices) {
    const Vector3d faceNormal = newellNormal(*cube, face);
    for (size_t i = 0; i < face.size(); ++i, ++corner) {
      INFO("corner " << corner << " of a cube was smoothed");
      CHECK(normals[corner].dot(faceNormal) > 0.999);
    }
  }
}

TEST_CASE("a finely faceted prism smooths across its side walls", "[smooth_normals]")
{
  const auto prism = makePrism(64);  // 5.625 degrees between adjacent side walls
  const auto normals = computeSmoothNormals(*prism, 24.0);
  REQUIRE(normals.size() == cornerCount(*prism));

  // The first face is a side wall. Its corners must no longer match its own flat
  // normal, because they now average the neighbouring walls.
  const Vector3d faceNormal = newellNormal(*prism, prism->indices[0]);
  bool anySmoothed = false;
  for (size_t i = 0; i < prism->indices[0].size(); ++i) {
    if (normals[i].dot(faceNormal) < 0.9999) anySmoothed = true;
    // ...but never across the sharp 90 degree edge into the end caps.
    CHECK(std::abs(normals[i].z()) < 0.01);
  }
  CHECK(anySmoothed);
}

TEST_CASE("the threshold decides, in both directions", "[smooth_normals]")
{
  // 20 sides => 18 degrees between adjacent walls: smoothed at 24, sharp at 10.
  const auto prism = makePrism(20);
  const Vector3d faceNormal = newellNormal(*prism, prism->indices[0]);

  const auto smoothed = computeSmoothNormals(*prism, 24.0);
  bool differs = false;
  for (size_t i = 0; i < prism->indices[0].size(); ++i) {
    if (smoothed[i].dot(faceNormal) < 0.9999) differs = true;
  }
  CHECK(differs);

  const auto sharp = computeSmoothNormals(*prism, 10.0);
  for (size_t i = 0; i < prism->indices[0].size(); ++i) {
    INFO("corner " << i << " smoothed despite exceeding the threshold");
    CHECK(sharp[i].dot(faceNormal) > 0.999);
  }
}
