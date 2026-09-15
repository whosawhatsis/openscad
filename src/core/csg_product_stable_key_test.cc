// The transparency compositor breaks exact depth ties between products with CSGProduct::stableKey().
// It is recomputed every time the preview's vertex buffers are rebuilt -- after every preview and
// on every shader switch -- so it has to be deterministic without costing time proportional to the
// mesh: a key built from a full text dump of each PolySet was half of a 2.2 s rebuild.

#include <catch2/catch_all.hpp>
#include <chrono>
#include <memory>
#include <string>

#include "core/CSGNode.h"
#include "geometry/PolySet.h"
#include "geometry/linalg.h"

namespace {

std::shared_ptr<PolySet> strip(size_t vertices, double offset = 0)
{
  auto ps = std::make_shared<PolySet>(3);
  ps->vertices.reserve(vertices);
  for (size_t i = 0; i < vertices; ++i) {
    ps->vertices.emplace_back(static_cast<double>(i) + offset, static_cast<double>(i % 2), 0.0);
  }
  for (size_t i = 0; i + 2 < vertices; ++i) {
    ps->indices.push_back({static_cast<int>(i), static_cast<int>(i + 1), static_cast<int>(i + 2)});
  }
  return ps;
}

CSGProduct product(const std::shared_ptr<PolySet>& ps, const Color4f& color)
{
  CSGProduct p;
  p.intersections.emplace_back(std::make_shared<CSGLeaf>(ps, Transform3d::Identity(), color, "leaf", 0));
  return p;
}

}  // namespace

TEST_CASE("CSGProduct::stableKey is equal for equal content in distinct objects", "[core][CSGProduct]")
{
  const Color4f red(1.0f, 0.0f, 0.0f, 0.5f);
  // A rebuild decodes fresh PolySet objects; ordering must not change because of it.
  CHECK(product(strip(64), red).stableKey() == product(strip(64), red).stableKey());
}

TEST_CASE("CSGProduct::stableKey distinguishes geometry and color", "[core][CSGProduct]")
{
  const Color4f red(1.0f, 0.0f, 0.0f, 0.5f);
  const Color4f blue(0.0f, 0.0f, 1.0f, 0.5f);
  const auto base = product(strip(64), red).stableKey();
  CHECK(base != product(strip(64, 1.0), red).stableKey());
  CHECK(base != product(strip(65), red).stableKey());
  CHECK(base != product(strip(64), blue).stableKey());
}

TEST_CASE("CSGProduct::stableKey does not scale with mesh size", "[core][CSGProduct]")
{
  const auto big = product(strip(1000000), Color4f(1.0f, 0.0f, 0.0f, 0.5f));
  const auto start = std::chrono::steady_clock::now();
  const auto key = big.stableKey();
  const auto elapsed = std::chrono::steady_clock::now() - start;
  (void)key;
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  INFO("stableKey took " << ms << " ms for a 1,000,000-vertex mesh");
  CHECK(ms < 50);
}
