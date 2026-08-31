// Round trip for the CSG product list a worker sends and a window reads back.
//
// The window never sees the worker's files -- only the payloads that arrived -- so what has to hold
// is that a product list serialized on one side reconstructs on the other with its leaves resolved
// by name. Everything the preview needs and the mesh does not carry travels in the list: the
// transform, the colour, the convexity, and whether a leaf is subtracted or intersected.
//
// The channel here is a collecting double rather than a real socket pair: a real one would need a
// reader running concurrently with the writer, and the test has no way to know how many payloads to
// expect before it stops reading. Collecting them is what the window does with them anyway, and the
// sink's own rule -- that opening a payload closes the previous one -- is exercised either way,
// since it is the sink that decides when to write.

#include "glview/CsgInfo.h"

#include <catch2/catch_all.hpp>
#include <map>
#include <memory>
#include <string>

#include "core/CSGNode.h"
#include "geometry/PolySet.h"
#include "io/ipc_endpoint.h"
#include "io/ipc_message.h"

namespace {

std::shared_ptr<PolySet> triangle(double size)
{
  auto ps = std::make_shared<PolySet>(3);
  ps->vertices = {{0, 0, 0}, {size, 0, 0}, {0, size, 0}};
  ps->indices = {{0, 1, 2}};
  ps->setConvexity(4);
  return ps;
}

std::shared_ptr<CSGLeaf> leaf(const std::shared_ptr<const PolySet>& ps, const Color4f& color,
                              const std::string& label, int index)
{
  Transform3d matrix = Transform3d::Identity();
  matrix.translate(Vector3d(index * 10.0, 0, 0));
  return std::make_shared<CSGLeaf>(ps, matrix, color, std::string{}, SurfaceFinish{}, label, index);
}

// Stands in for the far end of the channel, keeping what the sink sends. This is what makes the
// IpcChannel interface worth having: the sink talks to it, so a test can be the other end.
class CollectingChannel : public IpcChannel
{
public:
  void write(const std::string& name, const std::string& payload) override { payloads[name] = payload; }
  bool read(IpcMessage&) override { return false; }

  std::map<std::string, std::string> payloads;
};

//! Serializes and hands back both the document and the payloads that went with it, which is exactly
//! what the window will have received.
std::string serialize(const CsgInfo& csgInfo, const std::string& name,
                      std::map<std::string, std::string>& payloads)
{
  CollectingChannel channel;
  ipc_payload_sink::begin(channel);
  const std::string document = export_csg_products(csgInfo, name);
  ipc_payload_sink::end();
  payloads = std::move(channel.payloads);
  return document;
}

}  // namespace

TEST_CASE("A product list round-trips through the channel", "[glview][CsgProducts]")
{
  auto mesh = triangle(10);
  // CSGProducts constructs with one empty product; fill that rather than adding a second.
  auto products = std::make_shared<CSGProducts>();
  REQUIRE(products->products.size() == 1);
  auto& product = products->products.front();
  product.intersections.emplace_back(leaf(mesh, Color4f(1.0f, 0.0f, 0.0f, 1.0f), "solid", 0));
  product.subtractions.emplace_back(leaf(triangle(4), Color4f(0.0f, 0.0f, 1.0f, 0.5f), "hole", 1));

  CsgInfo written;
  written.root_products = products;

  std::map<std::string, std::string> payloads;
  const std::string document = serialize(written, "preview.json", payloads);

  CsgInfo read;
  REQUIRE(import_csg_products(read, document, payloads));
  REQUIRE(read.root_products);
  REQUIRE(read.root_products->products.size() == 1);

  const auto& restored = read.root_products->products.front();
  REQUIRE(restored.intersections.size() == 1);
  REQUIRE(restored.subtractions.size() == 1);

  SECTION("the mesh comes back")
  {
    const auto& restoredLeaf = restored.intersections.front().leaf;
    REQUIRE(restoredLeaf);
    REQUIRE(restoredLeaf->polyset);
    CHECK(restoredLeaf->polyset->vertices.size() == 3);
    CHECK(restoredLeaf->polyset->getConvexity() == 4);
  }

  SECTION("the transform comes back")
  {
    // A leaf placed by a translate() that arrived at the origin would stack every copy of a
    // repeated object on top of the first.
    const auto& restoredLeaf = restored.subtractions.front().leaf;
    CHECK(restoredLeaf->matrix.translation().x() == Catch::Approx(10.0));
  }

  SECTION("the colour comes back")
  {
    const auto& restoredLeaf = restored.intersections.front().leaf;
    CHECK(restoredLeaf->color.r() == Catch::Approx(1.0f));
    CHECK(restoredLeaf->color.a() == Catch::Approx(1.0f));
    CHECK(restored.subtractions.front().leaf->color.a() == Catch::Approx(0.5f));
  }

  SECTION("subtractions do not become intersections")
  {
    CHECK(restored.subtractions.front().leaf->label == "hole");
    CHECK(restored.intersections.front().leaf->label == "solid");
  }
}

TEST_CASE("A product list naming a payload that never arrived is refused", "[glview][CsgProducts]")
{
  // A worker killed partway through leaves a list referring to leaves the window never got.
  // Compositing that would silently drop geometry; failing says so.
  auto products = std::make_shared<CSGProducts>();
  products->products.front().intersections.emplace_back(
    leaf(triangle(5), Color4f(1, 1, 1, 1), "solid", 0));

  CsgInfo written;
  written.root_products = products;
  std::map<std::string, std::string> payloads;
  const std::string document = serialize(written, "preview.json", payloads);

  payloads.clear();  // the leaves never made it
  CsgInfo read;
  CHECK_FALSE(import_csg_products(read, document, payloads));
}

TEST_CASE("A document that is not a product list is refused", "[glview][CsgProducts]")
{
  CsgInfo read;
  const std::map<std::string, std::string> none;
  CHECK_FALSE(import_csg_products(read, "", none));
  CHECK_FALSE(import_csg_products(read, "{ not json", none));
  CHECK_FALSE(import_csg_products(read, "{\"unrelated\": true}", none));
}
