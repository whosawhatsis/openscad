#include "core/ColorNode.h"

#include <catch2/catch_all.hpp>
#include <string>

#include "core/ModuleInstantiation.h"

// The node-level half of anisotropic roughness (registry row 64): what
// material(anisotropy=...) stores, what it hands the geometry layer, and what
// it contributes to the dump. Written before the implementation.

namespace {

ColorNode makeMaterial()
{
  static const ModuleInstantiation inst{"material", AssignmentList{}, Location::NONE};
  ColorNode node{&inst};
  node.isMaterial = true;
  return node;
}

}  // namespace

TEST_CASE("anisotropy reaches the surface finish", "[core][ColorNode][anisotropy]")
{
  ColorNode node = makeMaterial();
  node.anisotropy = 0.6;
  node.hasAnisotropy = true;

  CHECK(node.finish().anisotropy == Catch::Approx(0.6f));
}

TEST_CASE("an unset anisotropy leaves the finish isotropic", "[core][ColorNode][anisotropy]")
{
  // Not merely 0-valued: a material() that never mentioned anisotropy must
  // produce a finish indistinguishable from one that could not have.
  const ColorNode node = makeMaterial();
  CHECK(node.finish().anisotropy == 0.0f);
}

TEST_CASE("anisotropy appears in the dump only when set", "[core][ColorNode][anisotropy]")
{
  // toString() is the geometry cache key. If anisotropy is missing from it,
  // two materials differing only in anisotropy collide in the cache and the
  // second one renders with the first one's lobe. If it is emitted when unset,
  // every existing dump changes and every cached result is invalidated.
  ColorNode node = makeMaterial();
  CHECK(node.toString().find("anisotropy") == std::string::npos);

  node.anisotropy = -0.25;
  node.hasAnisotropy = true;
  const std::string dump = node.toString();
  CHECK(dump.find("anisotropy = -0.25") != std::string::npos);
}

TEST_CASE("the dump distinguishes opposite anisotropies", "[core][ColorNode][anisotropy]")
{
  // The sign is the whole difference between a lobe smeared along the layers
  // and one smeared across them, so it has to survive into the cache key.
  ColorNode along = makeMaterial();
  along.anisotropy = 0.5;
  along.hasAnisotropy = true;

  ColorNode across = makeMaterial();
  across.anisotropy = -0.5;
  across.hasAnisotropy = true;

  CHECK(along.toString() != across.toString());
  CHECK(along.finish() != across.finish());
}
