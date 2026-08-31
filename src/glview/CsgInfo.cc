#include "glview/CsgInfo.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <ostream>
#include <string>
#include <vector>

#include "geometry/PolySet.h"
#include "io/ipc_endpoint.h"
#include "io/ipc_geometry.h"
#include "json/json.hpp"

using json = nlohmann::json;

namespace {

/*!
   Emits the mesh for one leaf, once, and returns the name to refer to it by.

   Leaves are shared: three copies of the same cube are one PolySet behind three transforms, so the
   mesh is keyed by pointer and sent once. Sending it per copy would multiply the bytes on the
   channel for nothing.
 */
std::string geometryName(const PolySet& polyset, const std::string& filename,
                         std::map<const PolySet *, std::string>& sent)
{
  const auto known = sent.find(&polyset);
  if (known != sent.end()) return known->second;

  const auto name = filename + ".leaf-" + std::to_string(sent.size()) + kIpcGeometrySuffix;
  if (ipc_payload_sink::collecting()) {
    export_ipc_geometry(polyset, ipc_payload_sink::open(name));
  } else {
    // Outside a worker there is no channel, so the same naming scheme goes to disk. The reference
    // the product list carries then needs no special case at either end.
    std::ofstream stream(std::filesystem::u8path(name), std::ios::binary);
    export_ipc_geometry(polyset, stream);
  }
  sent.emplace(&polyset, name);
  return name;
}

json writeChain(const std::vector<CSGChainObject>& chain, const std::string& filename,
                std::map<const PolySet *, std::string>& sent)
{
  json output = json::array();
  for (const auto& object : chain) {
    if (!object.leaf || !object.leaf->polyset) continue;

    json matrix = json::array();
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        matrix.push_back(object.leaf->matrix.matrix()(row, column));
      }
    }
    output.push_back({{"geometry", geometryName(*object.leaf->polyset, filename, sent)},
                      {"matrix", std::move(matrix)},
                      // Convexity drives how many depth-peeling passes the preview needs; a concave
                      // object rendered with the default renders wrongly.
                      {"convexity", object.leaf->polyset->getConvexity()},
                      // What color(), # and % produce. It does not live in the mesh, so it travels here.
                      {"color",
                       {object.leaf->color.r(), object.leaf->color.g(), object.leaf->color.b(),
                        object.leaf->color.a()}},
                      {"label", object.leaf->label},
                      {"index", object.leaf->index},
                      {"flags", object.flags}});
  }
  return output;
}

json writeProducts(const std::shared_ptr<CSGProducts>& products, const std::string& filename,
                   std::map<const PolySet *, std::string>& sent)
{
  json output = json::array();
  if (!products) return output;
  for (const auto& product : products->products) {
    // Intersections and subtractions stay apart: a difference() flattened into a union would
    // render as solid.
    output.push_back({{"intersections", writeChain(product.intersections, filename, sent)},
                      {"subtractions", writeChain(product.subtractions, filename, sent)}});
  }
  return output;
}

}  // namespace

std::string export_csg_products(const CsgInfo& csgInfo, const std::string& filename)
{
  // Shared across all three lists, so a leaf appearing in both a product and a highlight is still
  // sent once.
  std::map<const PolySet *, std::string> sent;
  const json document = {{"products", writeProducts(csgInfo.root_products, filename, sent)},
                         {"highlights", writeProducts(csgInfo.highlights_products, filename, sent)},
                         {"background", writeProducts(csgInfo.background_products, filename, sent)}};
  // Deliberately returned, not written: every leaf above went out through the payload sink, and
  // opening a payload sends the previous one. Writing this into a stream taken before those calls
  // would append it to the last leaf instead.
  return document.dump();
}
