// A compute worker sends Manifold's implicit face colors as tags (PolySet::COLOR_INDEX_DEFAULT /
// COLOR_INDEX_CUTOUT) so the window can resolve them against its own color scheme. Geometry that
// arrives that way and is then exported must not lose those faces' colors: every exporter reads a
// negative index as "no color".

#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "geometry/PolySet.h"
#include "glview/ColorMap.h"
#include "io/export.h"

TEST_CASE("Exporting scheme-tagged faces writes their colors", "[IPC]")
{
  auto ps = std::make_shared<PolySet>(3);
  ps->vertices = {{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  ps->indices = {{0, 2, 1}, {0, 1, 3}, {0, 3, 2}, {1, 2, 3}};
  ps->color_indices = {PolySet::COLOR_INDEX_CUTOUT, PolySet::COLOR_INDEX_DEFAULT,
                       PolySet::COLOR_INDEX_CUTOUT, PolySet::COLOR_INDEX_DEFAULT};

  const auto path = std::filesystem::temp_directory_path() / "openscad-scheme-color-tags.off";
  // Built here rather than through ColorMap, which needs the application's resource directory.
  const ColorScheme scheme = {{RenderColor::CGAL_FACE_FRONT_COLOR, Color4f(1.0f, 1.0f, 0.0f, 1.0f)},
                              {RenderColor::CGAL_FACE_BACK_COLOR, Color4f(0.0f, 1.0f, 0.0f, 1.0f)}};
  const ExportInfo info{.format = FileFormat::OFF,
                        .info = fileformat::info(FileFormat::OFF),
                        .camera = nullptr,
                        .defaultColor = scheme.at(RenderColor::CGAL_FACE_FRONT_COLOR),
                        .colorScheme = &scheme};
  REQUIRE(exportFileByName(ps, path.string(), info));

  std::ifstream in(path);
  std::string line;
  std::vector<std::string> faces;
  std::getline(in, line);                                 // OFF
  std::getline(in, line);                                 // counts
  for (int i = 0; i < 4 && std::getline(in, line);) ++i;  // vertices
  while (std::getline(in, line)) faces.push_back(line);

  REQUIRE(faces.size() == 4);
  for (const auto& face : faces) {
    INFO(face);
    std::istringstream tokens(face);
    std::vector<std::string> fields{std::istream_iterator<std::string>(tokens), {}};
    // "3 a b c r g b": a colorless face has only the four index fields.
    CHECK(fields.size() >= 7);
  }
}
