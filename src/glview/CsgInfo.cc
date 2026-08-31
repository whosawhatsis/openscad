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
#include "io/ipc_channel.h"
#include "io/ipc_geometry.h"
#include "json/json.hpp"
#include "utils/printutils.h"

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
                      // Material identity lives on the leaf, not the mesh, so a preview
                      // composited in the window from the worker's product list would
                      // otherwise lose material() -- and with it the surface parameters the
                      // viewport shades from.
                      {"materialName", object.leaf->materialName},
                      {"finish",
                       {object.leaf->finish.roughness, object.leaf->finish.metallic,
                        object.leaf->finish.reflectance, object.leaf->finish.emission}},
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

namespace {

//! Resolves a leaf by the name its payload arrived under. Decoded meshes are shared: a name that
//! appears in several products is one PolySet, as it was on the worker's side.
std::shared_ptr<const PolySet> readGeometry(
  const std::string& name, const std::map<std::string, std::string>& payloads,
  std::map<std::string, std::shared_ptr<const PolySet>>& decoded)
{
  const auto already = decoded.find(name);
  if (already != decoded.end()) return already->second;

  const auto payload = payloads.find(ipc_payload_name(name));
  if (payload == payloads.end()) return {};
  std::shared_ptr<const PolySet> polyset =
    import_ipc_polyset_buffer(payload->second.data(), payload->second.size(), name);
  if (!polyset) return {};
  decoded.emplace(name, polyset);
  return polyset;
}

bool readChain(const json& input, std::vector<CSGChainObject>& output,
               const std::map<std::string, std::string>& payloads,
               std::map<std::string, std::shared_ptr<const PolySet>>& decoded)
{
  for (const auto& item : input) {
    const auto name = item.value("geometry", std::string{});
    auto polyset = readGeometry(name, payloads, decoded);
    if (!polyset) {
      LOG(message_group::Error, "Preview refers to geometry '%1$s', which did not arrive.", name);
      return false;
    }

    Transform3d matrix = Transform3d::Identity();
    const auto values = item.value("matrix", std::vector<double>{});
    if (values.size() != 16) return false;
    for (int row = 0; row < 4; ++row) {
      for (int column = 0; column < 4; ++column) {
        matrix.matrix()(row, column) = values[row * 4 + column];
      }
    }

    const auto channels = item.value("color", std::vector<float>{});
    if (channels.size() != 4) return false;

    // A leaf's surface finish is what the viewport shades from; without it an isolated preview
    // draws every body with the default material.
    SurfaceFinish finish;
    if (const auto values = item.value("finish", std::vector<float>{}); values.size() == 4) {
      finish.roughness = values[0];
      finish.metallic = values[1];
      finish.reflectance = values[2];
      finish.emission = values[3];
    }
    auto leaf = std::make_shared<CSGLeaf>(polyset, matrix,
                                          Color4f(channels[0], channels[1], channels[2], channels[3]),
                                          item.value("materialName", std::string{}), finish,
                                          item.value("label", std::string{}), item.value("index", 0));
    output.emplace_back(leaf, static_cast<CSGNode::Flag>(item.value("flags", 0)));
  }
  return true;
}

bool readProducts(const json& input, std::shared_ptr<CSGProducts>& output,
                  const std::map<std::string, std::string>& payloads,
                  std::map<std::string, std::shared_ptr<const PolySet>>& decoded)
{
  if (!input.is_array() || input.empty()) return true;  // nothing of this kind is not a failure
  auto products = std::make_shared<CSGProducts>();
  // CSGProducts starts with one empty product of its own. Appending to that would return one more
  // product than was sent, and the extra would compound on every hop.
  products->products.clear();
  for (const auto& item : input) {
    CSGProduct product;
    if (!readChain(item.value("intersections", json::array()), product.intersections, payloads,
                   decoded) ||
        !readChain(item.value("subtractions", json::array()), product.subtractions, payloads, decoded)) {
      return false;
    }
    products->products.push_back(std::move(product));
  }
  output = std::move(products);
  return true;
}

}  // namespace

bool import_csg_products(CsgInfo& csgInfo, const std::string& document,
                         const std::map<std::string, std::string>& payloads)
{
  json parsed;
  try {
    parsed = json::parse(document);
  } catch (const std::exception&) {
    return false;
  }
  // A document without this key is something else entirely -- an error report, a truncated write --
  // and treating it as an empty preview would blank the window instead of saying so.
  if (!parsed.is_object() || !parsed.contains("products")) return false;

  // Shared across the three lists, so a leaf appearing in more than one is decoded once, exactly as
  // it was sent once.
  std::map<std::string, std::shared_ptr<const PolySet>> decoded;
  try {
    return readProducts(parsed["products"], csgInfo.root_products, payloads, decoded) &&
           readProducts(parsed.value("highlights", json::array()), csgInfo.highlights_products, payloads,
                        decoded) &&
           readProducts(parsed.value("background", json::array()), csgInfo.background_products, payloads,
                        decoded);
  } catch (const std::exception& e) {
    // A malformed product list has to be reported, not thrown. This runs on the GUI thread from a
    // queued reply, where an escaping exception kills the request silently and leaves the window
    // waiting for an answer that will never come.
    LOG(message_group::Error, "Preview could not be read: %1$s", e.what());
    return false;
  }
}
