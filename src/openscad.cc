/*
 *  OpenSCAD (www.openscad.org)
 *  Copyright (C) 2009-2011 Clifford Wolf <clifford@clifford.at> and
 *                          Marius Kintel <marius@kintel.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  As a special exception, you have permission to link this program
 *  with the CGAL library and distribute executables, as long as you
 *  follow the requirements of the GNU GPL in regard to all of the
 *  software in the executable aside from CGAL.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include "openscad.h"

#include "version.h"

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>

#include <cassert>
#endif
#include <libintl.h>

#include <algorithm>
#include <array>
#include <boost/algorithm/string/case_conv.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/lexical_cast/bad_lexical_cast.hpp>
#include <boost/optional/optional.hpp>
#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/value_semantic.hpp>
#include <boost/program_options/variables_map.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/range/iterator_range_core.hpp>
#include <clocale>
#include <cstddef>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <istream>
#include <iterator>
#include <map>
#include <memory>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>
#ifdef ENABLE_CGAL
#include <CGAL/assertions.h>
#include <CGAL/assertions_behaviour.h>
#endif

#include "Feature.h"
#include "LibraryInfo.h"
#include "RenderStatistic.h"
#include "core/progress.h"
#include "core/AST.h"
#include "core/BuiltinContext.h"
#include "core/Builtins.h"
#include "core/CSGTreeEvaluator.h"
#include "core/Context.h"
#include "core/EvaluationSession.h"
#include "core/RenderVariables.h"
#include "core/ScopeContext.h"
#include "core/Settings.h"
#include "core/customizer/CommentParser.h"
#include "core/customizer/ParameterObject.h"
#include "core/customizer/ParameterSet.h"
#include "core/node.h"
#include "core/parsersettings.h"
#include "geometry/Geometry.h"
#include "geometry/GeometryEvaluator.h"
#include "geometry/GeometryUtils.h"
#include "geometry/PolySet.h"
#include "glview/Camera.h"
#include "glview/ColorMap.h"
#include "glview/OffscreenView.h"
#include "glview/RenderSettings.h"
#include "handle_dep.h"
#include "io/chromatic.h"
#include "io/coordinatemap.h"
#include "command_line.h"
#include "io/export.h"
#include "json/json.hpp"

#include "glview/CsgInfo.h"
#include "io/ipc_endpoint.h"
#include "io/VideoEncoder.h"
#include "lodepng/lodepng.h"
#include "openscad_gui.h"
#include "openscad_mimalloc.h"
#include "platform/PlatformUtils.h"
#include "platform/Subprocess.h"
#include "utils/StackCheck.h"
#include "utils/exceptions.h"
#include "utils/printutils.h"

#ifdef ENABLE_PYTHON
#include "python/python_public.h"
#endif

namespace po = boost::program_options;
namespace fs = std::filesystem;

std::string commandline_commands;
std::string arg_colorscheme;

namespace {

bool arg_info = false;

}  // namespace

class Echostream
{
public:
  Echostream(std::ostream& stream) : stream(stream)
  {
    set_output_handler(&Echostream::output, nullptr, this);
  }
  Echostream(const std::string& filename) : fstream(std::filesystem::u8path(filename)), stream(fstream)
  {
    set_output_handler(&Echostream::output, nullptr, this);
  }
  static void output(const Message& msgObj, void *userdata)
  {
    auto self = static_cast<Echostream *>(userdata);
    if (msgObj.group != message_group::HtmlLink) {
      self->stream << msgObj.str() << "\n";
    }
  }
  ~Echostream()
  {
    if (fstream.is_open()) fstream.close();
  }

private:
  std::ofstream fstream;
  std::ostream& stream;
};

struct AnimateArgs {
  unsigned frames = 0;
  unsigned num_shards = 1;
  unsigned shard = 1;
  unsigned fps = 30;              //!< only used by the animation container formats
  unsigned blend_remesh_samples = 256;
  unsigned processes = 1;         //!< >1 renders the frames in that many worker processes

};

/*!
   This process's own argv, kept so that a worker copy of ourselves can be handed the
   same options. Rebuilding the command line from the parsed values instead would mean
   knowing how to re-emit every option OpenSCAD accepts, and would silently drop any
   option added later.
 */
std::vector<std::string> original_args;

struct CommandLine {
  const bool is_stdin;
  const std::string& filename;
  const bool is_stdout;
  std::string output_file;
  const fs::path& original_path;
  const std::string& parameterFile;
  const std::string& setName;
  const ViewOptions& viewOptions;
  const Camera& camera;
  const boost::optional<FileFormat> export_format;
  const CmdLineExportOptions& exportOptions;
  const AnimateArgs animate;
  const std::vector<std::string> summaryOptions;
  const std::string summaryFile;
  /*!
     Where the source should be treated as living, when that is not where it was read from.

     A window renders what is in its editor, which it hands to its compute worker as a temporary
     file. `include <>` and `use <>` resolve relative to the document, so without this they would
     resolve relative to the temporary copy and a model that renders in the GUI would fail in the
     worker. Empty everywhere else, which leaves the filename doing both jobs as before.
   */
  const std::string sourcePath;
  /*!
     A file whose appearance means "abandon this". Empty everywhere but a compute worker.

     A worker cannot be told to stop on the channel it is already busy on, and killing it would
     cost the window the geometry caches that are most of the reason it has its own process. The
     parent creates this file; evaluation notices it at the next progress tick and unwinds.
   */
  const std::string cancelFile;
  const bool multiStl;
  const bool overwrite;
};

namespace {

#ifndef OPENSCAD_NOGUI
bool useGUI()
{
#ifdef Q_OS_X11
  // see <http://qt.nokia.com/doc/4.5/qapplication.html#QApplication-2>:
  // On X11, the window system is initialized if GUIenabled is true. If GUIenabled
  // is false, the application does not connect to the X server. On Windows and
  // Macintosh, currently the window system is always initialized, regardless of the
  // value of GUIenabled. This may change in future versions of Qt.
  return getenv("DISPLAY") != 0;
#else
  return true;
#endif
}
#endif  // OPENSCAD_NOGUI

bool checkAndExport(const std::shared_ptr<const Geometry>& root_geom, unsigned dimensions,
                    ExportInfo& exportInfo, const bool is_stdout, const std::string& filename)
{
  if (root_geom->getDimension() != dimensions) {
    LOG("Current top level object is not a %1$dD object.", dimensions);
    return false;
  }
  if (root_geom->isEmpty()) {
    LOG("Current top level object is empty.");
    return false;
  }

  if (is_stdout) {
    exportFileStdOut(root_geom, exportInfo);
  } else {
    exportFileByName(root_geom, filename, exportInfo);
  }
  return true;
}

void help(const char *arg0, const po::options_description& desc, bool failure = false)
{
  const fs::path progpath(arg0);
  if (failure) {
    // A usage error, as opposed to an explicit --help. Raise rather than exit so an
    // in-process caller (see command_line.h) can report it instead of dying.
    std::ostringstream usage;
    usage << "Usage: " << progpath.filename().string() << " [options] file.scad\n" << desc;
    throw CommandLineError(usage.str());
  }
  LOG("Usage: %1$s [options] file.scad\n%2$s", progpath.filename().string(), desc);
  exit(0);
}

template <std::size_t size>
void help_export(const std::array<const Settings::SettingsEntryBase *, size>& options)
{
  LOG("Section '%1$s':", options.at(0)->category());

  for (const auto option : options) {
    const auto [type, values] = option->help();
    LOG("  - %1$s (%2$s): %3$s", option->name(), type, values);
  }
}

void help_export()
{
  LOG("OpenSCAD version %1$s\n", openscad_versionnumber);
  LOG("List of settings that can be given using the -O option using the");
  LOG("format '<section>/<key>=value', e.g.:");
  LOG("openscad -O export-pdf/paper-size=a6 -O export-pdf/show-grid=false\n");
  help_export(Settings::SettingsExportPdf::cmdline);
  help_export(Settings::SettingsExport3mf::cmdline);
  help_export(Settings::SettingsExportSvg::cmdline);
  exit(0);
}

void version()
{
  LOG("OpenSCAD version %1$s", openscad_versionnumber);
  exit(0);
}

int info()
{
  std::cout << LibraryInfo::info() << "\n\n";

  try {
    OffscreenView const glview(512, 512);
    std::cout << glview.getRendererInfo() << "\n";
  } catch (const OffscreenViewException& ex) {
    LOG("Can't create OpenGL OffscreenView: %1$s. Exiting.\n", ex.what());
    return 1;
  }

  return 0;
}

template <typename F>
bool with_output(const bool is_stdout, const std::string& filename, const F& f,
                 std::ios::openmode mode = std::ios::out)
{
  if (is_stdout) {
#ifdef _WIN32
    if ((mode & std::ios::binary) != 0) {
      _setmode(_fileno(stdout), _O_BINARY);
    }
#endif
    f(std::cout);
    return true;
  }
  // A compute worker returns its outputs over the channel instead of writing them, named for the
  // file they would have been. This covers the product list and the sidecars; the geometry itself
  // goes through exportFileByName, which has the same check.
  if (ipc_payload_sink::collecting()) {
    f(ipc_payload_sink::open(filename));
    return true;
  }
  std::ofstream fstream(std::filesystem::u8path(filename), mode);
  if (!fstream.is_open()) {
    LOG("Can't open file \"%1$s\" for export", filename);
    return false;
  } else {
    f(fstream);
    return true;
  }
}

AnimateArgs get_animate(const po::variables_map& vm)
{
  AnimateArgs animate;
  if (vm.count("animate")) {
    animate.frames = vm["animate"].as<unsigned>();
  }
  if (vm.count("animate-processes")) {
    animate.processes = vm["animate-processes"].as<unsigned>();
    if (animate.processes == 0) animate.processes = 1;
  }
  if (vm.count("animate_fps")) {
    animate.fps = vm["animate_fps"].as<unsigned>();
    if (animate.fps == 0 || animate.fps > 100) {
      LOG("--animate_fps needs to be in range <1..100>");
      exit(1);
    }
  }
  if (vm.count("blend-remesh-samples")) {
    animate.blend_remesh_samples = vm["blend-remesh-samples"].as<unsigned>();
    if (animate.blend_remesh_samples == 0 || animate.blend_remesh_samples > 256) {
      LOG("--blend-remesh-samples needs to be in range <1..256>");
      exit(1);
    }
  }
  if (vm.count("animate_sharding")) {
    std::vector<std::string> strs;
    boost::split(strs, vm["animate_sharding"].as<std::string>(), boost::is_any_of("/"));
    if (strs.size() != 2) {
      throw CommandLineError("--animate_sharding requires <shard>/<num_shards>");
    }
    try {
      animate.shard = boost::lexical_cast<unsigned>(strs[0]);
      animate.num_shards = boost::lexical_cast<unsigned>(strs[1]);
    } catch (const boost::bad_lexical_cast&) {
      throw CommandLineError("--animate_sharding parameters need to be positive integers");
    }
    if (animate.shard > animate.num_shards || animate.shard == 0) {
      throw CommandLineError("--animate_sharding: shard needs to be in range <1..num_shards>");
    }
  }
  return animate;
}

Camera get_camera(const po::variables_map& vm)
{
  Camera camera;

  if (vm.count("camera")) {
    std::vector<std::string> strs;
    std::vector<double> cam_parameters;
    boost::split(strs, vm["camera"].as<std::string>(), boost::is_any_of(","));
    if (strs.size() == 6 || strs.size() == 7) {
      try {
        for (const auto& s : strs) {
          cam_parameters.push_back(boost::lexical_cast<double>(s));
        }
        camera.setup(cam_parameters);
      } catch (boost::bad_lexical_cast&) {
        LOG("Camera setup requires numbers as parameters");
      }
    } else {
      throw CommandLineError(
        "Camera setup requires either 7 numbers for Gimbal Camera or 6 numbers for Vector Camera");
    }
  } else {
    camera.viewall = true;
    camera.autocenter = true;
  }

  if (vm.count("viewall")) {
    camera.viewall = true;
  }

  if (vm.count("autocenter")) {
    camera.autocenter = true;
  }

  if (vm.count("projection")) {
    auto proj = vm["projection"].as<std::string>();
    if (proj == "o" || proj == "ortho" || proj == "orthogonal") {
      camera.projection = Camera::ProjectionType::ORTHOGONAL;
    } else if (proj == "p" || proj == "perspective") {
      camera.projection = Camera::ProjectionType::PERSPECTIVE;
    } else {
      throw CommandLineError("projection needs to be 'o' or 'p' for ortho or perspective");
    }
  }

  if (vm.count("imgsize")) {
    std::vector<std::string> strs;
    boost::split(strs, vm["imgsize"].as<std::string>(), boost::is_any_of(","));
    if (strs.size() != 2) {
      throw CommandLineError("Need 2 numbers for imgsize");
    } else {
      try {
        int const w = boost::lexical_cast<int>(strs[0]);
        int const h = boost::lexical_cast<int>(strs[1]);
        camera.pixel_width = w;
        camera.pixel_height = h;
      } catch (boost::bad_lexical_cast&) {
        LOG("Need 2 numbers for imgsize");
      }
    }
  }

  return camera;
}

//! The lighting mode an export format asks the preview shader for, or Default
//! for every format that is not one of the AgentSCAD image outputs.
static AnalysisMode analysis_mode_for(FileFormat format)
{
  switch (format) {
  case FileFormat::NORMALMAP_PNG:     return AnalysisMode::Normal;
  case FileFormat::CANNYMAP_PNG:      return AnalysisMode::Canny;
  case FileFormat::COORDINATEMAP_PNG: return AnalysisMode::Coordinate;
  case FileFormat::FLATMAP_PNG:       return AnalysisMode::Flat;
  case FileFormat::CHROMATIC_PNG:     return AnalysisMode::Chromatic;
  default:                            return AnalysisMode::Default;
  }
}

//! Read a boolean -O option, defaulting when it is absent or unparseable.
static bool export_option_flag(const CmdLineExportOptions& exportOptions, const std::string& section,
                               const std::string& name, bool fallback)
{
  const auto s = exportOptions.find(section);
  if (s == exportOptions.end()) return fallback;
  const auto entry = s->second.find(name);
  if (entry == s->second.end()) return fallback;
  const std::string& v = entry->second;
  if (v == "0" || v == "false" || v == "no" || v == "off") return false;
  if (v == "1" || v == "true" || v == "yes" || v == "on") return true;
  LOG(message_group::Warning, "Unrecognized value \"%1$s\" for %2$s/%3$s; using the default.", v,
      section, name);
  return fallback;
}

/*!
   Write the light directions a chromatic image was lit by, if asked for with
   -O chromatic/lights=<path>. A consumer that does not know which direction lit
   which channel cannot interpret the image, so a failed write fails the export.
 */
static bool write_chromatic_lights_sidecar(const CmdLineExportOptions& exportOptions)
{
  const auto section = exportOptions.find("chromatic");
  if (section == exportOptions.end()) return true;
  const auto entry = section->second.find("lights");
  if (entry == section->second.end() || entry->second.empty()) return true;

  std::ofstream sidecar(entry->second);
  if (!sidecar.is_open()) {
    LOG(message_group::Error, "Unable to open chromatic lights file \"%1$s\"", entry->second);
    return false;
  }
  sidecar << serialize_lights_json(chromatic_lights());
  return sidecar.good();
}

/*!
   Write the bounding box a coordinate map decodes against, if asked for with
   -O coordinatemap/bounds=<path>. Without it the image is a picture rather than
   data, so a failure to write it fails the export instead of being warned about.
 */
static bool write_coordinate_bounds_sidecar(const OffscreenView& glview,
                                            const CmdLineExportOptions& exportOptions)
{
  const auto section = exportOptions.find("coordinatemap");
  if (section == exportOptions.end()) return true;
  const auto entry = section->second.find("bounds");
  if (entry == section->second.end() || entry->second.empty()) return true;

  std::ofstream sidecar(entry->second);
  if (!sidecar.is_open()) {
    LOG(message_group::Error, "Unable to open coordinate bounds file \"%1$s\"", entry->second);
    return false;
  }
  sidecar << serialize_bounds_json(glview.coordinateBounds());
  return sidecar.good();
}

bool collectUsdAnimationObjects(const std::shared_ptr<CSGNode>& node,
                                std::vector<UsdAnimationObject>& objects)
{
  if (!node || node->isEmptySet()) return true;
  if (const auto leaf = std::dynamic_pointer_cast<CSGLeaf>(node)) {
    if (leaf->polyset) {
      objects.push_back({leaf->polyset, leaf->matrix, leaf->color, leaf->index});
    }
    return true;
  }
  const auto operation = std::dynamic_pointer_cast<CSGOperation>(node);
  if (!operation || operation->getType() != OpenSCADOperator::UNION) return false;
  return collectUsdAnimationObjects(operation->left(), objects) &&
         collectUsdAnimationObjects(operation->right(), objects);
}

int do_export(const CommandLine& cmd, const RenderVariables& render_variables, FileFormat export_format,
              SourceFile *root_file, VideoEncoder *videoEncoder,
              std::vector<UsdAnimationFrame> *usdFrames)
{
  auto filename_str = fs::path(cmd.output_file).generic_string();
  // Avoid possibility of fs::absolute throwing when passed an empty path
  auto fpath = cmd.filename.empty() ? fs::current_path() : fs::absolute(fs::path(cmd.filename));
  auto fparent = fpath.parent_path();

  // set CWD relative to source file
  fs::current_path(fparent);

  EvaluationSession session{fparent.string()};
  ContextHandle<BuiltinContext> builtin_context{Context::create<BuiltinContext>(&session)};
  render_variables.applyToContext(builtin_context);

#ifdef DEBUG
  PRINTDB("BuiltinContext:\n%s", builtin_context->dump());
#endif

  AbstractNode::resetIndexCounter();
  std::shared_ptr<const FileContext> file_context;
  std::shared_ptr<AbstractNode> absolute_root_node;

#ifdef ENABLE_PYTHON
  if (python_result_node != NULL && python_active) {
    absolute_root_node = python_result_node;
  } else {
#endif
    absolute_root_node = root_file->instantiate(*builtin_context, &file_context);
#ifdef ENABLE_PYTHON
  }
#endif

  Camera camera = cmd.camera;
  if (file_context) {
    camera.updateView(file_context, true);
  }

  // restore CWD after module instantiation finished
  fs::current_path(cmd.original_path);

  // Do we have an explicit root node (! modifier)?
  std::shared_ptr<const AbstractNode> root_node;
  const Location *nextLocation = nullptr;
  if (!(root_node = find_root_tag(absolute_root_node, &nextLocation))) {
    root_node = absolute_root_node;
  }
  if (nextLocation) {
    LOG(message_group::Warning, *nextLocation, builtin_context->documentRoot(),
        "More than one Root Modifier (!)");
  }
  Tree tree(root_node, fparent.string());

  // Only progress_prepare() writes through this, and only to number the nodes for progress
  // reporting -- the tree itself is not modified.
  struct CancelWatch {
    const std::string& file;
    ~CancelWatch() { progress_report_fin(); }
  };
  std::unique_ptr<CancelWatch> cancelWatch;
  if (!cmd.cancelFile.empty()) {
    cancelWatch = std::make_unique<CancelWatch>(CancelWatch{cmd.cancelFile});
    progress_report_prep(
      std::const_pointer_cast<AbstractNode>(root_node),
      [](const std::shared_ptr<const AbstractNode>&, void *userdata, int) {
        if (fs::exists(static_cast<CancelWatch *>(userdata)->file)) throw ProgressCancelException();
      },
      cancelWatch.get());
  }

  if (export_format == FileFormat::CSG) {
    // https://github.com/openscad/openscad/issues/128
    // When I use the csg ouptput from the command line the paths in 'import'
    // statements become relative. But unfortunately they become relative to
    // the current working dir and neither to the location of the input nor
    // the output.
    fs::current_path(fparent);  // Force exported filenames to be relative to document path
    with_output(cmd.is_stdout, filename_str, [&tree, root_node](std::ostream& stream) {
      stream << tree.getString(*root_node, "\t") << "\n";
    });
    fs::current_path(cmd.original_path);
  } else if (export_format == FileFormat::IPC_PRODUCTS) {
    // A preview is a product list plus one payload per distinct leaf, not a single mesh: the
    // parent composites it and needs the structure, not a merged result.
    CsgInfo csgInfo;
    csgInfo.compile_products(tree);
    // The leaves are sent first and the list afterwards: emitting a payload closes the previous
    // one, so the list cannot be held open while the leaves it names are still being written.
    const std::string products = export_csg_products(csgInfo, filename_str);
    with_output(
      cmd.is_stdout, filename_str, [&products](std::ostream& stream) { stream << products; },
      std::ios::out | std::ios::binary);
  } else if (export_format == FileFormat::AST) {
    fs::current_path(fparent);  // Force exported filenames to be relative to document path
    with_output(cmd.is_stdout, filename_str,
                [root_file](std::ostream& stream) { stream << root_file->dump(""); });
    fs::current_path(cmd.original_path);
  } else if (export_format == FileFormat::PARAM) {
    with_output(cmd.is_stdout, filename_str,
                [&root_file, &fpath](std::ostream& stream) { export_param(root_file, fpath, stream); });
  } else if (export_format == FileFormat::TERM) {
    CSGTreeEvaluator csgRenderer(tree);
    auto root_raw_term = csgRenderer.buildCSGTree(*root_node);
    with_output(cmd.is_stdout, filename_str, [root_raw_term](std::ostream& stream) {
      if (!root_raw_term || root_raw_term->isEmptySet()) {
        stream << "No top-level CSG object\n";
      } else {
        stream << root_raw_term->dump() << "\n";
      }
    });
  } else if (export_format == FileFormat::ECHO) {
    // echo -> don't need to evaluate any geometry
  } else {
    // start measuring render time
    RenderStatistic renderStatistic;
    const bool preserveBodies = cmd.multiStl || export_format == FileFormat::_3MF ||
                                export_format == FileFormat::POV;
    GeometryEvaluator geomevaluator(tree, preserveBodies);
    std::unique_ptr<OffscreenView> glview;
    std::shared_ptr<const Geometry> root_geom;
    // Parsed before anything renders: --view=depth shades with the same range the
    // depthmap export encodes with, so the preview and the file agree.
    DepthmapOptions depthmapOptions;
    if (const auto section = cmd.exportOptions.find("depthmap"); section != cmd.exportOptions.end()) {
      if (const auto r = section->second.find("range"); r != section->second.end()) {
        std::string error;
        if (!parse_depth_range(r->second, depthmapOptions.explicit_near, depthmapOptions.explicit_far,
                               error)) {
          LOG("Invalid depthmap range '%1$s': %2$s.", r->second, error);
          return 1;
        }
        depthmapOptions.has_explicit_range = true;
      }
    }

    if ((export_format == FileFormat::ECHO || export_format == FileFormat::PNG ||
         export_format == FileFormat::DEPTHMAP || export_format == FileFormat::PFM ||
         fileformat::isAnimation(export_format) ||
         analysis_mode_for(export_format) != AnalysisMode::Default) &&
        (cmd.viewOptions.renderer == RenderType::OPENCSG ||
         cmd.viewOptions.renderer == RenderType::THROWNTOGETHER)) {
      // OpenCSG or throwntogether png -> just render a preview
      glview =
        prepare_preview(tree, cmd.viewOptions, camera, depthmapOptions, analysis_mode_for(export_format),
                        export_option_flag(cmd.exportOptions, "chromatic", "gauge", true));
      if (!glview) return 1;
    } else {
      // Force creation of concrete geometry (mostly for testing)
      // FIXME: Consider adding MANIFOLD as a valid --render argument and ViewOption, to be able to
      // distinguish from CGAL

      constexpr bool allownef = true;
      root_geom = geomevaluator.evaluateGeometry(*tree.root(), allownef);
      if (!root_geom) root_geom = std::make_shared<PolySet>(3);
      if (cmd.viewOptions.renderer == RenderType::BACKEND_SPECIFIC && root_geom->getDimension() == 3) {
        if (auto geomlist = std::dynamic_pointer_cast<const GeometryList>(root_geom)) {
          auto flatlist = geomlist->flatten();
          for (auto& child : flatlist) {
            if (child.second->getDimension() == 3) {
              child.second = GeometryUtils::getBackendSpecificGeometry(child.second);
            }
          }
          root_geom = std::make_shared<GeometryList>(flatlist);
        } else {
          root_geom = GeometryUtils::getBackendSpecificGeometry(root_geom);
          assert(root_geom != nullptr);
        }
        LOG("Converted to backend-specific geometry");
      }
    }

    const std::string input_filename = cmd.is_stdin ? "<stdin>" : cmd.filename;
    const int dim = fileformat::is3D(export_format) ? 3 : fileformat::is2D(export_format) ? 2 : 0;
    ExportInfo exportInfo = createExportInfo(export_format, fileformat::info(export_format),
                                             input_filename, &camera, cmd.exportOptions);
    if (cmd.multiStl) {
      if (!Feature::ExperimentalMultiMaterial.is_enabled()) {
        LOG("Option --multi-stl requires --enable=multi-material.");
        return 1;
      }
      if (export_format != FileFormat::ASCII_STL && export_format != FileFormat::BINARY_STL) {
        LOG("Option --multi-stl requires STL output.");
        return 1;
      }
      if (cmd.is_stdout) {
        LOG("Option --multi-stl is not supported when exporting to stdout.");
        return 1;
      }
      if (!root_geom || root_geom->getDimension() != 3 || root_geom->isEmpty()) {
        LOG("Current top level object is not a non-empty 3D object.");
        return 1;
      }
      if (!export_stl_files(root_geom, filename_str, exportInfo, cmd.overwrite)) return 1;
    } else if (usdFrames != nullptr) {
      // Animated USD: one stage covers every frame, so the geometry is collected here and the
      // caller writes the single file once the loop has finished.
      GeometryEvaluator usdGeometryEvaluator(tree);
      CSGTreeEvaluator usdCsgEvaluator(tree, &usdGeometryEvaluator);
      std::vector<UsdAnimationObject> objects;
      const auto csgRoot = usdCsgEvaluator.buildCSGTree(*tree.root());
      if (!collectUsdAnimationObjects(csgRoot, objects)) objects.clear();
      usdFrames->push_back({root_geom, std::move(objects), camera});
    } else if (dim > 0 && !checkAndExport(root_geom, dim, exportInfo, cmd.is_stdout, filename_str)) {
      return 1;
    }

    if (videoEncoder != nullptr) {
      // One animation frame: hand the pixels to the encoder rather than writing a file.
      const bool success = (cmd.viewOptions.renderer == RenderType::BACKEND_SPECIFIC ||
                            cmd.viewOptions.renderer == RenderType::GEOMETRY)
                             ? export_video_frame(root_geom, cmd.viewOptions, camera, *videoEncoder)
                             : export_video_frame(*glview, *videoEncoder);
      if (!success) {
        LOG(message_group::Error, "Failed to encode animation frame.");
        return 1;
      }
    }

    if (export_format == FileFormat::DEPTHMAP) {
      DepthmapOptions opts = depthmapOptions;
      const auto section = cmd.exportOptions.find("depthmap");
      if (section != cmd.exportOptions.end()) {
        const auto p_entry = section->second.find("profile");
        if (p_entry != section->second.end()) {
          if (p_entry->second == "visual") {
            opts.profile = DepthProfile::visual;
          } else if (p_entry->second == "metric10um") {
            opts.profile = DepthProfile::metricFine;
          } else if (p_entry->second != "metric") {
            LOG("Unknown depthmap profile '%1$s'. Expected 'metric', 'metric10um' or 'visual'.",
                p_entry->second);
            return 1;
          }
        }
        const auto c_entry = section->second.find("camera");
        if (c_entry != section->second.end()) {
          opts.camera_sidecar_path = c_entry->second;
        }
      }

      bool success = true;
      bool const wrote = with_output(
        cmd.is_stdout, filename_str,
        [&success, &root_geom, &cmd, &camera, &glview, opts](std::ostream& stream) {
          if (cmd.viewOptions.renderer == RenderType::BACKEND_SPECIFIC ||
              cmd.viewOptions.renderer == RenderType::GEOMETRY) {
            success = export_depthmap(root_geom, cmd.viewOptions, camera, opts, stream);
          } else {
            success = export_depthmap(*glview, opts, stream);
          }
        },
        std::ios::out | std::ios::binary);
      if (!success || !wrote) {
        return 1;
      }
    }

    if (export_format == FileFormat::PFM) {
      bool success = true;
      bool const wrote = with_output(
        cmd.is_stdout, filename_str,
        [&success, &root_geom, &cmd, &camera, &glview](std::ostream& stream) {
          if (cmd.viewOptions.renderer == RenderType::BACKEND_SPECIFIC ||
              cmd.viewOptions.renderer == RenderType::GEOMETRY) {
            success = export_pfm(root_geom, cmd.viewOptions, camera, stream);
          } else {
            success = export_pfm(*glview, stream);
          }
        },
        std::ios::out | std::ios::binary);
      if (!success || !wrote) {
        return 1;
      }
    }

    const AnalysisMode agent_mode = analysis_mode_for(export_format);
    if (export_format == FileFormat::PNG || agent_mode != AnalysisMode::Default) {
      if (agent_mode != AnalysisMode::Default) {
        if (!glview && agent_mode != AnalysisMode::Canny) {
          // These formats are produced by a shader on the preview path. --render
          // routes to export_png(root_geom, ...), which builds its own view and
          // would silently write an ordinary shaded image instead - a wrong
          // answer is worse here than a refusal, since the output looks
          // plausible and decodes to nonsense.
          LOG(message_group::Error,
              "%1$s export requires the preview renderer; drop --render or use "
              "--preview=throwntogether.",
              fileformat::info(export_format).identifier);
          return 1;
        }
        // The mode was already applied by prepare_preview, before it painted.
        if (agent_mode == AnalysisMode::Coordinate &&
            !write_coordinate_bounds_sidecar(*glview, cmd.exportOptions)) {
          return 1;
        }
        if (agent_mode == AnalysisMode::Chromatic &&
            !write_chromatic_lights_sidecar(cmd.exportOptions)) {
          return 1;
        }
      }
      bool success = true;
      bool const wrote = with_output(
        cmd.is_stdout, filename_str,
        [&success, &root_geom, &cmd, &camera, &glview, &depthmapOptions,
         agent_mode](std::ostream& stream) {
          if (cmd.viewOptions.renderer == RenderType::BACKEND_SPECIFIC ||
              cmd.viewOptions.renderer == RenderType::GEOMETRY) {
            auto options = cmd.viewOptions;
            options.canny = agent_mode == AnalysisMode::Canny;
            success = export_png(root_geom, options, camera, depthmapOptions, stream);
          } else {
            success = export_png(*glview, stream);
          }
        },
        std::ios::out | std::ios::binary);
      if (!success || !wrote) {
        return 1;
      }
    }

    renderStatistic.printAll(root_geom, camera, cmd.summaryOptions, cmd.summaryFile);
  }
  return 0;
}

/*
   Builds the command line for one worker copy of ourselves.

   Everything this process was given is passed through, minus the options a worker
   must not inherit, plus its own output path and shard. Dropping
   --animate-processes here is what stops a worker from spawning workers of its own;
   -o is dropped because it accepts multiple values, so appending would add a second
   output rather than replace the first.

   The forms are the ones boost::program_options accepts: "--name value",
   "--name=value", "-o value" and "-ovalue".
 */
std::vector<std::string> worker_command_line(const std::string& executable,
                                             const std::string& output_file, unsigned shard,
                                             unsigned num_shards)
{
  std::vector<std::string> args{executable};

  for (size_t i = 1; i < original_args.size(); ++i) {
    const std::string& arg = original_args[i];
    bool dropped = false;
    // --animate_sharding is dropped and reissued below: the worker's shard index is
    // composed from this process's own shard, so passing the original through would
    // both be wrong and make boost reject the duplicate option.
    for (const std::string name : {"--animate-processes", "--animate_sharding", "--o"}) {
      if (arg == name) {  // value is the next argument
        ++i;
        dropped = true;
        break;
      }
      if (arg.rfind(name + "=", 0) == 0) {
        dropped = true;
        break;
      }
    }
    if (dropped) continue;
    if (arg == "-o") {  // value is the next argument
      ++i;
      continue;
    }
    if (arg.rfind("-o", 0) == 0 && arg.size() > 2) continue;  // "-ovalue"
    args.push_back(arg);
  }

  args.push_back("-o");
  args.push_back(output_file);
  args.push_back("--animate_sharding");
  args.push_back(std::to_string(shard) + "/" + std::to_string(num_shards));
  return args;
}

//! A directory of our own under the system temp directory, or an empty path on failure.
fs::path make_temp_directory()
{
  std::error_code ec;
  const fs::path base = fs::temp_directory_path(ec);
  if (ec) {
    LOG(message_group::Error, "Can't locate a temporary directory: %1$s.", ec.message());
    return {};
  }
  // Racing another OpenSCAD is handled by create_directory returning false rather
  // than by trying to pick a name nobody else could have chosen.
  for (unsigned attempt = 0; attempt < 1000; ++attempt) {
    const fs::path candidate = base / ("openscad-animate-" + std::to_string(attempt));
    if (fs::create_directory(candidate, ec)) return candidate;
  }
  LOG(message_group::Error, "Can't create a temporary directory under %1$s.", base.generic_string());
  return {};
}

/*
   Renders the frames of --animate in several worker processes and combines them.

   Separate processes rather than threads because the renderer's OpenGL and OpenCSG
   state is process-global: workers that share an address space have to serialize
   around it, which costs most of the parallelism, while separate processes each get
   a private copy and need no locking at all.

   For a still-image sequence the workers write the final numbered files themselves
   and there is nothing left to do. For an animation container there is exactly one
   encoder and it lives here, in the parent: the workers render PNGs into a
   temporary directory and this function feeds them to the encoder in order.
 */
int run_sharded_animation(const CommandLine& cmd, FileFormat export_format)
{
  if (cmd.is_stdin) {
    LOG(message_group::Error, "--animate-processes can't read the model from stdin.");
    return 1;
  }
  if (cmd.is_stdout) {
    LOG(message_group::Error, "--animate-processes can't write to stdout.");
    return 1;
  }

  /*
     This process may itself be one shard of a larger render spread across machines,
     so the frames to cover are this shard's range rather than the whole animation.
     For an unsharded run that is simply [0, frames).
   */
  const unsigned start_frame = ((cmd.animate.shard - 1) * cmd.animate.frames) / cmd.animate.num_shards;
  const unsigned limit_frame = (cmd.animate.shard * cmd.animate.frames) / cmd.animate.num_shards;
  const unsigned shard_frames = limit_frame - start_frame;
  if (shard_frames == 0) {
    LOG(message_group::Warning, "--animate_sharding %1$d/%2$d covers no frames of %3$d.",
        cmd.animate.shard, cmd.animate.num_shards, cmd.animate.frames);
    return 0;
  }

  // More workers than frames would leave some with nothing to do. One worker is a
  // legitimate outcome of that clamp and still works - it renders every frame and
  // the parent muxes as usual.
  const unsigned workers = std::min(cmd.animate.processes, shard_frames);

  const bool container = fileformat::isAnimation(export_format);
  fs::path temp_dir;
  std::string frame_output = cmd.output_file;
  if (container) {
    temp_dir = make_temp_directory();
    if (temp_dir.empty()) return 1;
    frame_output = (temp_dir / "frame.png").generic_string();
  }

  if (container && cmd.animate.num_shards != 1) {
    LOG(message_group::Warning,
        "--animate_sharding %1$d/%2$d writes only frames %3$d-%4$d of %5$d to this %6$s. "
        "The file is one slice of the animation, not the whole of it; concatenate the "
        "shards in order to reassemble it.",
        cmd.animate.shard, cmd.animate.num_shards, start_frame, limit_frame - 1, cmd.animate.frames,
        fileformat::info(export_format).description);
  }

  /*
     Sharding and worker processes split the same frame list at two levels, so the
     indices compose: worker j of P on shard s of m is global shard (s-1)*P + j of m*P.
     Because every boundary is the same integer division, the composed range for j=1
     starts exactly where this shard starts and for j=P ends exactly where it ends -
     no frame is dropped or rendered twice for any combination of frames, m and P.
   */
  const std::string executable = boost::dll::program_location().generic_string();
  const unsigned global_shards = cmd.animate.num_shards * workers;
  std::vector<std::vector<std::string>> commands;
  commands.reserve(workers);
  for (unsigned worker = 1; worker <= workers; ++worker) {
    const unsigned global_shard = (cmd.animate.shard - 1) * workers + worker;
    commands.push_back(worker_command_line(executable, frame_output, global_shard, global_shards));
  }

  LOG("Rendering %1$d frames in %2$d processes...", shard_frames, workers);
  const bool spawned = Subprocess::runAllAndWait(commands);

  auto cleanup = [&temp_dir]() {
    if (temp_dir.empty()) return;
    std::error_code ec;
    fs::remove_all(temp_dir, ec);
  };

  if (!spawned) {
    cleanup();
    return 1;
  }
  if (!container) return 0;

  auto encoder = VideoEncoder::create(fileformat::toSuffix(export_format));
  assert(encoder != nullptr);

  bool opened = false;
  // Workers number their output by global frame index, so a shard's files start at
  // start_frame rather than at zero.
  for (unsigned frame = start_frame; frame < limit_frame; ++frame) {
    const std::string path = numberedFramePath(frame_output, frame);
    std::vector<unsigned char> png;
    if (lodepng::load_file(png, path) != 0) {
      LOG(message_group::Error, "Worker did not produce frame %1$d (%2$s).", frame, path);
      cleanup();
      return 1;
    }
    // Only the header is read here. Whether the pixels have to be decoded at all is
    // the encoder's business - APNG copies the frame's compressed data across as it
    // stands, which at 4K is the difference between seconds per frame and none.
    unsigned width = 0, height = 0;
    LodePNGState inspect_state;
    lodepng_state_init(&inspect_state);
    const unsigned inspect_error =
      lodepng_inspect(&width, &height, &inspect_state, png.data(), png.size());
    lodepng_state_cleanup(&inspect_state);
    if (inspect_error != 0) {
      LOG(message_group::Error, "Can't read frame %1$d (%2$s).", frame, path);
      cleanup();
      return 1;
    }
    // Frame size comes from the frames themselves rather than from the camera, so
    // that whatever the workers actually rendered is what gets encoded.
    if (!opened) {
      if (!encoder->open(cmd.output_file, width, height, cmd.animate.fps)) {
        LOG(message_group::Error, "Can't open %1$s for writing.", cmd.output_file);
        cleanup();
        return 1;
      }
      opened = true;
    }
    if (!encoder->addPngFrame(png.data(), png.size())) {
      LOG(message_group::Error, "Failed to encode frame %1$d.", frame);
      cleanup();
      return 1;
    }
  }

  if (opened && !encoder->close()) {
    LOG(message_group::Error, "Failed to finalize %1$s.", cmd.output_file);
    cleanup();
    return 1;
  }
  cleanup();
  return 0;
}

int cmdline(const CommandLine& cmd)
{
  FileFormat export_format;

  // Determine output file format and assign it to formatName
  if (cmd.export_format.is_initialized()) {
    export_format = cmd.export_format.get();
  } else {
    // else extract format from file extension
    const auto path = fs::path(cmd.output_file);
    std::string suffix = path.has_extension() ? path.extension().generic_string().substr(1) : "";
    boost::algorithm::to_lower(suffix);

    if (!fileformat::fromIdentifier(suffix, export_format)) {
      LOG(
        "Invalid suffix %1$s. Either add a valid suffix or specify one using the --export-format "
        "option.",
        suffix);
      return 1;
    }
  }

  // Do some minimal checking of output directory before rendering (issue #432)
  auto output_dir = fs::path(cmd.output_file).parent_path();
  if (output_dir.empty()) {
    // If output_file_str has no directory prefix, set output directory to current directory.
    output_dir = fs::current_path();
  }
  if (!fs::is_directory(output_dir)) {
    LOG("\n'%1$s' is not a directory for output file %2$s - Skipping\n", output_dir.generic_string(),
        cmd.output_file);
    return 1;
  }

  set_render_color_scheme(arg_colorscheme, true);

  std::shared_ptr<Echostream> echostream;
  if (export_format == FileFormat::ECHO) {
    echostream.reset(cmd.is_stdout ? new Echostream(std::cout) : new Echostream(cmd.output_file));
  }

  std::string text;
  if (cmd.is_stdin) {
    text = std::string((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
  } else {
    std::ifstream ifs(std::filesystem::u8path(cmd.filename));
    if (!ifs.is_open()) {
      LOG("Can't open input file '%1$s'!\n", cmd.filename);
      return 1;
    }
    handle_dep(cmd.filename);
    text = std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  }

#ifdef ENABLE_PYTHON
  python_active = false;
  if (cmd.filename.c_str() != NULL) {
    if (boost::algorithm::ends_with(cmd.filename, ".py")) {
      if (python_trusted == true) python_active = true;
      else LOG("Python is not enabled");
    }
  }

  if (python_active) {
    auto fulltext_py = text;
    initPython("", 0.0);
    auto error = evaluatePython(fulltext_py, false);
    if (error.size() > 0) LOG(error.c_str());
    text = "\n";
  }
#endif  // ifdef ENABLE_PYTHON
  text += "\n\x03\n" + commandline_commands;

  SourceFile *root_file = nullptr;
  const std::string& documentPath = cmd.sourcePath.empty() ? cmd.filename : cmd.sourcePath;
  if (!parse(root_file, text, documentPath, documentPath, false)) {
    delete root_file;  // parse failed
    root_file = nullptr;
  }
  if (!root_file) {
    LOG("Can't parse file '%1$s'!\n", cmd.filename);
    return 1;
  }

  // add parameter to AST
  CommentParser::collectParameters(text.c_str(), root_file);
  if (!cmd.parameterFile.empty() && !cmd.setName.empty()) {
    ParameterObjects parameters = ParameterObjects::fromSourceFile(root_file);
    ParameterSets sets;
    sets.readFile(cmd.parameterFile);
    for (const auto& set : sets) {
      if (set.name() == cmd.setName) {
        parameters.importValues(set);
        parameters.apply(root_file);
        break;
      }
    }
  }

  root_file->handleDependencies();

  RenderVariables render_variables = {
    .preview = fileformat::canPreview(export_format)
                 ? (cmd.viewOptions.renderer == RenderType::OPENCSG ||
                    cmd.viewOptions.renderer == RenderType::THROWNTOGETHER)
                 : false,
    .camera = cmd.camera,
  };

  if (cmd.animate.frames == 0) {
    if (fileformat::isAnimation(export_format)) {
      LOG(message_group::Error, "%1$s output needs --animate <frames>.",
          fileformat::info(export_format).description);
      return 1;
    }
    render_variables.time = 0;
    return do_export(cmd, render_variables, export_format, root_file, nullptr, nullptr);
  } else if (cmd.animate.processes > 1) {
    // Hand the frames to worker processes. This process renders none of them itself;
    // it only combines what the workers produced.
    return run_sharded_animation(cmd, export_format);
  } else {
    // export the requested number of animated frames
    const unsigned start_frame = ((cmd.animate.shard - 1) * cmd.animate.frames) / cmd.animate.num_shards;
    const unsigned limit_frame = (cmd.animate.shard * cmd.animate.frames) / cmd.animate.num_shards;
    /*
       An animation container collects every frame into one file, so it is opened once
       here and each frame is handed to it; the still formats keep writing one
       numbered file per frame as before.
     */
    std::unique_ptr<VideoEncoder> encoder;
    /*
       USD is animatable but not an animation-only container: it collects the frames'
       geometry and writes one time-sampled stage at the end.
     */
    std::vector<UsdAnimationFrame> usdFrames;
    const bool collectsGeometry =
      fileformat::canAnimate(export_format) && !fileformat::isAnimation(export_format);
    if (fileformat::isAnimation(export_format)) {
      if (cmd.is_stdout) {
        LOG(message_group::Error, "Animation output cannot be written to stdout.");
        return 1;
      }
      /*
         A shard is a *contiguous* range of frames, so a container holding one is a valid
         animation of part of the timeline, and the shards concatenate in order - which is
         a legitimate way to spread a render across machines. What is not acceptable is
         doing it silently: a truncated file is indistinguishable from a complete one. So
         warn, naming the frames this file actually holds, and carry on.
       */
      if (cmd.animate.num_shards != 1) {
        LOG(message_group::Warning,
            "--animate_sharding %1$d/%2$d writes only frames %3$d-%4$d of %5$d to this %6$s. "
            "The file is one slice of the animation, not the whole of it; concatenate the "
            "shards in order to reassemble it.",
            cmd.animate.shard, cmd.animate.num_shards, start_frame, limit_frame - 1, cmd.animate.frames,
            fileformat::info(export_format).description);
      }
      encoder = VideoEncoder::create(fileformat::toSuffix(export_format));
      assert(encoder != nullptr);
      if (!encoder->open(cmd.output_file, cmd.camera.pixel_width, cmd.camera.pixel_height,
                         cmd.animate.fps)) {
        LOG(message_group::Error, "Can't open %1$s for writing.", cmd.output_file);
        return 1;
      }
    }

    for (unsigned frame = start_frame; frame < limit_frame; ++frame) {
      render_variables.time = frame * (1.0 / cmd.animate.frames);

      CommandLine frame_cmd = cmd;
      if (!encoder && !collectsGeometry) {
        std::ostringstream oss;
        oss << std::setw(5) << std::setfill('0') << frame;

        auto frame_file = fs::path(cmd.output_file);
        auto extension = frame_file.extension();
        frame_file.replace_extension();
        frame_file += oss.str();
        frame_file.replace_extension(extension);
        frame_cmd.output_file = frame_file.generic_string();
      }

      LOG("Exporting %1$s...", cmd.filename);

      int const r = do_export(frame_cmd, render_variables, export_format, root_file, encoder.get(),
                              collectsGeometry ? &usdFrames : nullptr);
      if (r != 0) {
        return r;
      }
    }

    if (collectsGeometry) {
      const std::string input_filename = cmd.is_stdin ? "<stdin>" : cmd.filename;
      const ExportInfo exportInfo = createExportInfo(export_format, fileformat::info(export_format),
                                                     input_filename, &cmd.camera, cmd.exportOptions);
      const bool wrote = with_output(
        false, fs::path(cmd.output_file).generic_string(),
        [&](std::ostream& stream) {
          if (export_format == FileFormat::BLEND) {
            export_blend_animation(
              usdFrames, cmd.animate.fps, stream,
              {.remeshSamples = static_cast<size_t>(cmd.animate.blend_remesh_samples),
               .defaultColor = exportInfo.defaultColor});
          } else if (export_format == FileFormat::USDZ) {
            export_usdz_animation(usdFrames, cmd.animate.fps, stream, exportInfo);
          } else {
            export_usda_animation(usdFrames, cmd.animate.fps, stream, exportInfo);
          }
        },
        std::ios::out | std::ios::binary);
      if (!wrote) {
        LOG(message_group::Error, "Can't open %1$s for writing.", cmd.output_file);
        return 1;
      }
    }

    if (encoder && !encoder->close()) {
      LOG(message_group::Error, "Failed to finalize %1$s.", cmd.output_file);
      return 1;
    }

    return 0;
  }
}

template <class Seq, typename ToString>
static std::string str_join(const Seq& seq, const std::string& sep, const ToString& toString)
{
  return boost::algorithm::join(boost::adaptors::transform(seq, toString), sep);
}

static bool flagConvert(const std::string& str)
{
  if (str == "1" || boost::iequals(str, "on") || boost::iequals(str, "true")) {
    return true;
  }
  if (str == "0" || boost::iequals(str, "off") || boost::iequals(str, "false")) {
    return false;
  }
  throw std::runtime_error("");
  return false;
}

static std::tuple<std::string, std::string> simple_split(const std::string& str, const char c)
{
  const auto idx = str.find_first_of(c);
  if (idx == std::string::npos) return {};
  const auto first = str.substr(0, idx);
  const auto second = str.substr(idx + 1);
  return {first, second};
}

static CmdLineExportOptions convert_export_options(const po::variables_map& vm)
{
  if (vm.count("O") == 0) {
    return {};
  }

  CmdLineExportOptions map;
  const auto& options = vm["O"].as<std::vector<std::string>>();
  for (const auto& option : options) {
    const auto [key, value] = simple_split(option, '=');
    const auto [section, name] = simple_split(key, '/');
    map[section][name] = value;
  }
  return map;
}

}  // namespace

void set_render_color_scheme(const std::string& color_scheme, const bool exit_if_not_found)
{
  if (color_scheme.empty()) {
    return;
  }

  if (ColorMap::instance().findColorScheme(color_scheme)) {
    RenderSettings::inst()->colorscheme = color_scheme;
    return;
  }

  if (exit_if_not_found) {
    throw CommandLineError("Unknown color scheme '" + arg_colorscheme + "'. Known schemes:\n" +
                           boost::algorithm::join(ColorMap::instance().colorSchemeNames(), "\n"));
  } else {
    LOG("Unknown color scheme '%1$s', using default '%2$s'.", arg_colorscheme,
        ColorMap::instance().defaultColorSchemeName());
  }
}

/**
 * Initialize gettext. This must be called after the application path was
 * determined so we can lookup the resource path for the language translation
 * files.
 */
void localization_init()
{
  fs::path const po_dir(PlatformUtils::resourcePath("locale"));
  const std::string& locale_path(po_dir.string());

  if (fs::is_directory(locale_path)) {
    setlocale(LC_ALL, "");
    bindtextdomain("openscad", locale_path.c_str());
    bind_textdomain_codeset("openscad", "UTF-8");
    textdomain("openscad");
  } else {
    LOG("Could not initialize localization (application path is '%1$s').",
        PlatformUtils::applicationPath());
  }
}

#ifdef Q_OS_MACOS
std::pair<std::string, std::string> customSyntax(const std::string& s)
{
  if (s.find("-psn_") == 0) return {"psn", s.substr(5)};
#else
std::pair<std::string, std::string> customSyntax(const std::string&)
{
#endif

  return {};
}
/*!
   This makes boost::program_option parse comma-separated values
 */
struct CommaSeparatedVector {
  std::vector<std::string> values;

  friend std::istream& operator>>(std::istream& in, CommaSeparatedVector& value)
  {
    std::string token;
    in >> token;
    // NOLINTNEXTLINE(*NewDeleteLeaks) LLVM bug https://github.com/llvm/llvm-project/issues/40486
    boost::split(value.values, token, boost::is_any_of(","));
    return in;
  }
};

// OpenSCAD
namespace {

/*!
   Serves a compute worker's channel until the parent finishes with it.

   Recognised before any option parsing, so it works in a HEADLESS build and never reaches the code
   that would put a window on screen. The channel arrives through the environment, not the command
   line: a descriptor number is not secret, but it has no business being visible in a process list.

   Returning when the channel ends is the whole contract. A worker whose window has gone must exit
   rather than linger, or a session leaks one process per window.
 */
int compute_worker_main()
{
  const char *argument = std::getenv(kIpcChannelEnvironmentVariable);
  if (argument == nullptr) {
    LOG(message_group::Error, "No compute worker channel in %1$s.", kIpcChannelEnvironmentVariable);
    return 1;
  }
  const auto channel = ipc_channel_from_argument(argument);
  if (!channel) {
    LOG(message_group::Error, "Compute worker channel '%1$s' is not usable.", argument);
    return 1;
  }

  IpcMessage message;
  while (channel->read(message)) {
    // Anything unrecognised is skipped rather than treated as an error: a newer parent must not be
    // able to wedge an older worker just by sending a message it has not heard of.
    if (message.name != "request") continue;

    nlohmann::json answer;
    try {
      const auto request = nlohmann::json::parse(message.payload);
      // Echoed back so a parent that has several requests in flight can match them up.
      if (request.contains("requestId")) answer["requestId"] = request["requestId"];
      const auto command = request.at("command").get<std::string>();
      const auto preview = command == "preview";
      if (!preview && command != "render") {
        throw std::runtime_error("unknown command '" + command + "'");
      }
      const auto input = request.at("input").get<std::string>();
      const auto output = request.at("output").get<std::string>();

      // do_export() chdir()s into the document's directory and leaves the process there. The
      // worker is persistent, so without restoring it here it would hold a handle on whichever
      // directory it last rendered from for the rest of its life -- on Windows that directory can
      // then not be renamed or removed by anyone. Restored on every exit from this request,
      // including the failing ones.
      const auto workerPath = fs::current_path();
      struct RestorePath {
        const fs::path& path;
        ~RestorePath()
        {
          std::error_code ignored;
          fs::current_path(path, ignored);
        }
      } const restorePath{workerPath};

      // Everything the export machinery writes goes to the channel instead of the filesystem for
      // the duration of this request.
      ipc_payload_sink::begin(*channel);
      const ViewOptions viewOptions{};
      const Camera camera;
      const CmdLineExportOptions exportOptions;
      // A window renders what is in its editor, which it hands over as a temporary file. Relative
      // include<> and use<> must still resolve against the document's own directory, or a model
      // that renders in the GUI fails in the worker.
      // The editor's text arrives as a temporary file, so where the document really lives has to
      // be said separately or its relative includes cannot be found.
      const auto workingDirectory = request.value("workingDirectory", std::string{});
      const auto sourcePath = request.value("sourcePath", std::string{});
      // The window owns this preference, so a preview normalized in the worker has to be told it
      // or the product list would differ from the one the same model produces in-process.
      if (request.contains("normalizationLimit")) {
        RenderSettings::inst()->openCSGTermLimit = request["normalizationLimit"].get<unsigned int>();
      }
      // Falls back to the document's own directory when one is named, since that is what relative
      // paths in the model are written against -- not wherever the text happens to have been read
      // from.
      const fs::path originalPath = !workingDirectory.empty() ? fs::path(workingDirectory)
                                    : !sourcePath.empty()     ? fs::path(sourcePath).parent_path()
                                                              : fs::path(input).parent_path();
      // The Customizer's values are not in the .scad file, so unless the request carries them the
      // worker renders the file's defaults -- a window that shows one thing and exports another.
      const std::string parameterFile = request.value("parameterFile", std::string{});
      const std::string setName = request.value("setName", std::string{});
      // The parent creates this file to withdraw the request. Polling a path is crude, but the
      // channel is not readable while this thread is inside the evaluation it would be cancelling.
      const std::string cancelFile = request.value("cancelFile", std::string{});
      const int result =
        cmdline(CommandLine{false,
                            input,
                            false,
                            output,
                            originalPath,
                            parameterFile,
                            setName,
                            viewOptions,
                            camera,
                            preview ? FileFormat::IPC_PRODUCTS : FileFormat::IPC_GEOMETRY,
                            exportOptions,
                            {},
                            {},
                            "",
                            sourcePath,
                            cancelFile});
      ipc_payload_sink::end();

      if (result != 0) throw std::runtime_error("evaluation of '" + input + "' failed");
      answer["ok"] = true;
    } catch (const ProgressCancelException&) {
      // Withdrawn by the parent. Answering rather than exiting is the entire point: the process
      // keeps the caches that make the next render cheap.
      ipc_payload_sink::end();
      answer["ok"] = false;
      answer["error"] = "cancelled";
    } catch (const std::exception& e) {
      ipc_payload_sink::end();
      // A request the worker cannot make sense of is reported and the worker stays up. Exiting
      // would cost the window its warm caches over a malformed message.
      answer["ok"] = false;
      answer["error"] = e.what();
    }
    channel->write("done", answer.dump());
  }
  return 0;
}

}  // namespace

/*!
   Builds the user-visible command line options.

   Extracted from openscad_main() so the same option set can be parsed from inside a running
   process -- see run_command_line() in command_line.h. Keeping a single definition is the
   point: a second, drifting copy of the option list would be worse than no in-process
   parsing at all.
 */
po::options_description build_options_description()
{
  // Only used to name the available --view options in the help text.
  ViewOptions viewOptions{};
  po::options_description desc("Allowed options");
  // clang-format off
  desc.add_options()
    ("export-format", po::value<std::string>(),
      "overrides format of exported scad file when using option '-o', arg can be any of its supported "
      "file extensions.  For ASCII stl export, specify 'asciistl', and for binary stl export, specify "
      "'binstl'.  ASCII export is the current stl default, but binary stl is planned as the future "
      "default so asciistl should be explicitly specified in scripts when needed.\n")
    ("multi-stl", "export each body to a separate STL file")
    ("overwrite", "allow multi-STL export to replace existing files")
    ("o,o", po::value<std::vector<std::string>>(),
      "output specified file instead of running the GUI. The file extension specifies the type: stl, "
      "off, wrl, 3mf, csg, dxf, svg, pdf, png, echo, ast, term, nef3, nefdbg, param, pov. May be "
      "used multiple times for different exports. Use '-' for stdout.\n")
    ("O,O", po::value<std::vector<std::string>>(),
      "pass settings value to the file export using the format section/key=value, e.g "
      "export-pdf/paper-size=a3. Use --help-export to list all available settings.")
    ("command", po::value<std::vector<std::string>>(),
      "run newline-delimited command options in this process. Value may be text, a file, or '-' for "
      "stdin; may be repeated to reuse warm caches.")
    ("D,D", po::value<std::vector<std::string>>(), "var=val -pre-define variables")
    ("p,p", po::value<std::string>(), "customizer parameter file")
    ("P,P", po::value<std::string>(), "customizer parameter set")
  #ifdef ENABLE_EXPERIMENTAL
    ("enable", po::value<std::vector<std::string>>(),
      ("enable experimental features (specify 'all' for enabling all available features): " +
      str_join(boost::make_iterator_range(Feature::begin(), Feature::end()), " | ",
               [](const Feature *feature) { return feature->get_name(); }) +
      "\n")
      .c_str())
  #endif
    ("help,h", "print this help message and exit")
    ("help-export", "print list of export parameters and values that can be set via -O")
    ("version,v", "print the version")
    ("info", "print information about the build process\n")
    ("camera", po::value<std::string>(),
      "camera parameters when exporting png: =translate_x,y,z,rot_x,y,z,dist or "
      "=eye_x,y,z,center_x,y,z")("autocenter", "adjust camera to look at object's center")
    ("viewall", "adjust camera to fit object")
    ("backend", po::value<std::string>(),
      "3D rendering backend to use: 'CGAL' (old/slow) or 'Manifold' (new/fast) [default]")
    ("imgsize", po::value<std::string>(), "=width,height of exported png")
    ("edge-width", po::value<double>()->default_value(1.0), "Feature edge width in pixels (finite, nonnegative)")
    ("render", po::value<std::string>()->implicit_value(""),
      "for full geometry evaluation when exporting png")
    ("preview", po::value<std::string>()->implicit_value(""),
      "[=throwntogether] -for ThrownTogether preview png")
    ("animate-processes", po::value<unsigned>(),
      "render the frames of --animate in N worker processes instead of one, then combine the "
      "results. Each worker renders its own share of the frames, so this uses N cores.")
    ("animate", po::value<unsigned>(), "export N animated frames")
    ("animate_fps", po::value<unsigned>(), "frame rate for formats that fold the frames into one file (gif, apng, avi, usda, usdz, blend); default 30")
    ("blend-remesh-samples", po::value<unsigned>(),
      "maximum evenly spaced geometry samples in .blend animations; range 1..256, default 256")
    ("animate_sharding", po::value<std::string>(),
      "Parameter <shard>/<num_shards> - Divide work into <num_shards> and only output frames for "
      "<shard>. E.g. 2/5 only outputs the second 1/5 of frames. Use to parallelize work on multiple "
      "cores or machines.")
    ("view", po::value<CommaSeparatedVector>(),
      ("=view options: " + boost::algorithm::join(viewOptions.names(), " | ")).c_str())
    ("projection", po::value<std::string>(), "=(o)rtho or (p)erspective when exporting png")
    ("csglimit", po::value<unsigned int>(), "=n -stop rendering at n CSG elements when exporting png")
    ("summary", po::value<std::vector<std::string>>(),
      "enable additional render summary and statistics: all | cache | time | camera | geometry | "
      "bounding-box | area")
    ("summary-file", po::value<std::string>(),
      "output summary information in JSON format to the given file, using '-' outputs to stdout")
    ("colorscheme", po::value<std::string>(),
          ("=colorscheme: " +
           str_join(ColorMap::instance().colorSchemeNames(), " | ",
                    [](const std::string& colorScheme) {
                      return (colorScheme == ColorMap::instance().defaultColorSchemeName() ? "*" : "") +
                             colorScheme;
                    }) +
           "\n")
            .c_str())
    ("d,d", po::value<std::string>(), "deps_file -generate a dependency file for make")
    ("m,m", po::value<std::string>(), "make_cmd -runs make_cmd file if file is missing")
    ("quiet,q", "quiet mode (don't print anything *except* errors)")
    ("reset-window-settings", "Reset GUI settings for window placement and fonts.")
    ("hardwarnings", "Stop on the first warning")
    ("trace-depth", po::value<unsigned int>(), "=n, maximum number of trace messages")
    ("trace-usermodule-parameters", po::value<std::string>(),
      "=true/false, configure the output of user module parameters in a trace")
    ("check-parameters", po::value<std::string>(),
      "=true/false, configure the parameter check for user modules and functions")
    ("check-parameter-ranges", po::value<std::string>(),
      "=true/false, configure the parameter range check for builtin modules")
    ("debug", po::value<std::string>(),
      "special debug info - specify 'all' or a set of source file names")
  #ifdef ENABLE_PYTHON
    ("trust-python", "Trust python")
    ("python-module", po::value<std::string>(), "=module Call pip python module")
  #endif
    ;
  // clang-format on

#ifdef ENABLE_GUI_TESTS
  // clang-format off
  desc.add_options()("run-all-gui-tests", "special gui testing mode - run all the tests");
// clang-format on
#endif
  return desc;
}

int openscad_main(int argc, char **argv)
{
  const bool compute_worker = argc == 2 && std::string(argv[1]) == "--compute-worker";

#if defined(ENABLE_CGAL) && defined(USE_MIMALLOC)
  // call init_mimalloc before any GMP variables are initialized. (defined in src/openscad_mimalloc.h)
  init_mimalloc();
#endif

  int rc = 0;
  StackCheck::inst();

  // Kept for --animate-processes, which starts worker copies of this process with
  // the same options. Captured before any parsing so it is exactly what we were given.
  original_args.assign(argv, argv + argc);

#ifdef Q_OS_MACOS
  bool isGuiLaunched = getenv("GUI_LAUNCHED") != nullptr;
  auto nslog = [](const Message& msg, void *userdata) { CocoaUtils::nslog(msg.msg, userdata); };
  if (isGuiLaunched) set_output_handler(nslog, nullptr, nullptr);
#else
  PlatformUtils::ensureStdIO();
#endif

#ifndef __EMSCRIPTEN__
  const auto applicationPath =
    weakly_canonical(boost::dll::program_location()).parent_path().generic_string();
#else
  const auto applicationPath = boost::dll::fs::current_path();
#endif
  PlatformUtils::registerApplicationPath(applicationPath);

#ifdef ENABLE_PYTHON
  // The original name as called, not resolving links and so on. This will
  // just forward everything to the python main.
  const auto applicationName = fs::path(argv[0]).filename().generic_string();
  if (applicationName == "python" || applicationName == "python3" ||
      applicationName.rfind("python3.", 0) == 0 || applicationName == "openscad-python") {
    return pythonRunArgs(argc, argv);
  }
#endif

#ifdef ENABLE_CGAL
  // Always throw exceptions from CGAL, so we can catch instead of crashing on bad geometry.
  CGAL::set_error_behaviour(CGAL::THROW_EXCEPTION);
  CGAL::set_warning_behaviour(CGAL::THROW_EXCEPTION);
#endif
  Builtins::initialize();

  // After the initialization a worker needs -- builtins, the application path, the allocator and
  // CGAL's error behaviour -- and before any argument parsing, so this mode never reaches the
  // options table or anything that would put a window on screen.
  if (compute_worker) return compute_worker_main();

  auto original_path = fs::current_path();

  std::vector<std::string> output_files;
  const char *deps_output_file = nullptr;
  boost::optional<FileFormat> export_format;

  ViewOptions viewOptions{};
  po::options_description desc = build_options_description();
  po::options_description hidden("Hidden options");
  // clang-format off
  hidden.add_options()
#ifdef Q_OS_MACOS
    ("psn", po::value<std::string>(), "process serial number")
#endif
    ("input-file", po::value<std::vector<std::string>>(), "input file");
  // clang-format on

  po::positional_options_description p;
  p.add("input-file", -1);

  po::options_description all_options;
  all_options.add(desc).add(hidden);

  po::variables_map vm;
  try {
    po::store(po::command_line_parser(argc, argv)
                .options(all_options)
                .positional(p)
                .extra_parser(customSyntax)
                .run(),
              vm);
  } catch (const std::exception& e) {  // Catches e.g. unknown options
    LOG("%1$s\n", e.what());
    help(argv[0], desc, true);
  }

  if (vm.count("command")) {
    if (vm.size() != 1) {
      LOG(
        "--command cannot be combined with top-level options. Put each option inside a command string.");
      return 1;
    }
    std::string error;
    const int command_rc = run_command_lines(vm["command"].as<std::vector<std::string>>(), error);
    if (!error.empty()) LOG("%1$s", error);
    return command_rc;
  }

  OpenSCAD::debug = "";
  if (vm.count("debug")) {
    OpenSCAD::debug = vm["debug"].as<std::string>();
    LOG("Debug on. --debug=%1$s", OpenSCAD::debug);
  }
#ifdef ENABLE_PYTHON
  if (vm.count("trust-python")) {
    LOG("Python Engine enabled", OpenSCAD::debug);
    python_trusted = true;
  }

  const auto pymod = "python-module";
  if (vm.count(pymod)) {
    PRINTDB("Running Python Module %s", pymod);
    std::vector<std::string> args;
    if (vm.count("input-file")) {
      args = vm["input-file"].as<std::vector<std::string>>();
    }
    return pythonRunModule(applicationPath, vm[pymod].as<std::string>(), args);
  }
#endif  // ifdef ENABLE_PYTHON
  if (vm.count("quiet")) {
    OpenSCAD::quiet = true;
  }

  if (vm.count("hardwarnings")) {
    OpenSCAD::hardwarnings = true;
  }

  if (vm.count("traceDepth")) {
    OpenSCAD::traceDepth = vm["traceDepth"].as<unsigned int>();
  }
  std::map<std::string, bool *> flags;
  flags.insert(std::make_pair("trace-usermodule-parameters", &OpenSCAD::traceUsermoduleParameters));
  flags.insert(std::make_pair("check-parameters", &OpenSCAD::parameterCheck));
  flags.insert(std::make_pair("check-parameter-ranges", &OpenSCAD::rangeCheck));
  for (const auto& flag : flags) {
    std::string name = flag.first;
    if (vm.count(name)) {
      std::string opt = vm[name].as<std::string>();
      try {
        (*(flag.second) = flagConvert(opt));
      } catch (const std::runtime_error& e) {
        LOG("Could not parse '--%1$s %2$s' as flag", name, opt);
      }
    }
  }

  if (vm.count("help")) help(argv[0], desc);
  if (vm.count("help-export")) help_export();
  if (vm.count("version")) version();
  if (vm.count("info")) arg_info = true;
  if (vm.count("backend")) {
    auto backend_string = vm["backend"].as<std::string>();
    auto backend = renderBackend3DFromString(backend_string);
    if (!backend) {
      LOG(message_group::Error, "Unknown rendering backend '%1$s'.", backend_string.c_str());
      return 1;
    }
    RenderSettings::inst()->backend3D = backend.value();
  }

  if (vm.count("preview")) {
    if (vm["preview"].as<std::string>() == "throwntogether")
      viewOptions.renderer = RenderType::THROWNTOGETHER;
  } else if (vm.count("render")) {
    // Note: "cgal" is here for backwards compatibility, can probably be removed soon
    if (vm["render"].as<std::string>() == "cgal" || vm["render"].as<std::string>() == "force") {
      viewOptions.renderer = RenderType::BACKEND_SPECIFIC;
    } else {
      viewOptions.renderer = RenderType::GEOMETRY;
    }
  }

  viewOptions.previewer = (viewOptions.renderer == RenderType::THROWNTOGETHER)
                            ? Previewer::THROWNTOGETHER
                            : Previewer::OPENCSG;
  if (vm.count("view")) {
    const auto& viewOptionValues = vm["view"].as<CommaSeparatedVector>();

    for (const auto& option : viewOptionValues.values) {
      try {
        viewOptions[option] = true;
      } catch (const std::out_of_range& e) {
        LOG("Unknown --view option '%1$s' ignored. Use -h to list available options.", option);
      }
    }
  }

  if (vm.count("csglimit")) {
    RenderSettings::inst()->openCSGTermLimit = vm["csglimit"].as<unsigned int>();
  }
  viewOptions.edgeWidth = vm["edge-width"].as<double>();
  if (!std::isfinite(viewOptions.edgeWidth) || viewOptions.edgeWidth < 0.0) {
    LOG(message_group::Error, "Edge width must be finite and nonnegative");
    return 1;
  }

  if (vm.count("o")) {
    output_files = vm["o"].as<std::vector<std::string>>();
  }
  if (vm.count("d")) {
    if (deps_output_file) help(argv[0], desc, true);
    deps_output_file = vm["d"].as<std::string>().c_str();
  }
  if (vm.count("m")) {
    if (make_command) help(argv[0], desc, true);
    make_command = vm["m"].as<std::string>().c_str();
  }

  if (vm.count("D")) {
    for (const auto& cmd : vm["D"].as<std::vector<std::string>>()) {
      commandline_commands += cmd;
      commandline_commands += ";\n";
    }
  }
  if (vm.count("enable")) {
    for (const auto& feature : vm["enable"].as<std::vector<std::string>>()) {
      if (feature == "all") {
        Feature::enable_all();
        break;
      }
      Feature::enable_feature(feature);
    }
  }

  std::string parameterFile;
  if (vm.count("p")) {
    if (!parameterFile.empty()) {
      help(argv[0], desc, true);
    }
    parameterFile = vm["p"].as<std::string>().c_str();
  }

  std::string parameterSet;
  if (vm.count("P")) {
    if (!parameterSet.empty()) {
      help(argv[0], desc, true);
    }
    parameterSet = vm["P"].as<std::string>().c_str();
  }

  std::vector<std::string> inputFiles;
  if (vm.count("input-file")) {
    inputFiles = vm["input-file"].as<std::vector<std::string>>();
  }

  if (vm.count("colorscheme")) {
    arg_colorscheme = vm["colorscheme"].as<std::string>();
  }

  if (vm.count("export-format")) {
    const auto format_str = vm["export-format"].as<std::string>();
    FileFormat format;
    if (fileformat::fromIdentifier(format_str, format)) {
      export_format.emplace(format);

    } else {
      LOG("Unknown --export-format option '%1$s'.  Use -h to list available options.", format_str);
      return 1;
    }
  }

  AnimateArgs const animate = get_animate(vm);
  const Camera camera = get_camera(vm);

  if (animate.frames) {
    for (const auto& filename : output_files) {
      if (filename == "-") {
        LOG("Option --animate is not supported when exporting to stdout.");
        return 1;
      }
    }
    if (output_files.empty()) {
      output_files.emplace_back("frame.png");
    }
  }

  PRINTDB("Application location detected as %s", applicationPath);

  auto cmdlinemode = false;
  if (!output_files.empty()) {  // cmd-line mode
    cmdlinemode = true;
    if (!inputFiles.size()) help(argv[0], desc, true);
  }

  if (arg_info || cmdlinemode) {
    if (inputFiles.size() > 1) help(argv[0], desc, true);
    try {
      parser_init();
      localization_init();
      if (arg_info) {
        rc = info();
      } else {
        for (const auto& filename : output_files) {
          const bool is_stdin = inputFiles[0] == "-";
          const std::string input_file = is_stdin ? "<stdin>" : inputFiles[0];
          const bool is_stdout = filename == "-";
          const std::string output_file = is_stdout ? "<stdout>" : filename;
          const auto export_options = convert_export_options(vm);
          const CommandLine cmd{is_stdin,
                                input_file,
                                is_stdout,
                                output_file,
                                original_path,
                                parameterFile,
                                parameterSet,
                                viewOptions,
                                camera,
                                export_format,
                                export_options,
                                animate,
                                vm.count("summary") ? vm["summary"].as<std::vector<std::string>>()
                                                    : std::vector<std::string>{},
                                vm.count("summary-file") ? vm["summary-file"].as<std::string>() : "",
                                "",  // sourcePath: only a compute worker reads from elsewhere
                                "",  // cancelFile: only a compute worker is cancellable
                                vm.count("multi-stl") > 0,
                                vm.count("overwrite") > 0};
          rc |= cmdline(cmd);
        }
      }
    } catch (const HardWarningException&) {
      rc = 1;
    }

    if (deps_output_file) {
      std::string const deps_out(deps_output_file);
      const std::vector<std::string>& geom_out(output_files);
      if (!write_deps(deps_out, geom_out)) {
        LOG("Error writing deps");
        return 1;
      }
    }
#ifndef OPENSCAD_NOGUI
  } else if (useGUI()) {
    if (vm.count("export-format")) {
      LOG("Ignoring --export-format option");
    }
    std::string gui_test = "none";
    if (vm.count("run-all-gui-tests")) {
      gui_test = "all";
    }
    auto reset_window_settings = vm.count("reset-window-settings") > 0;
    rc = gui(inputFiles, original_path, argc, argv, gui_test, reset_window_settings);
#endif
  } else {
    LOG("Requested GUI mode but can't open display!\n");
    return 1;
  }

  return rc;
}

/*
   In-process command line execution -- see command_line.h.

   ponytail: this parses with the same options_description as openscad_main() and then hands off
   to the same cmdline() runner, rather than reimplementing either. It deliberately supports only
   the export path (an -o output), which is all the Advanced Export spike needs; --help, --version
   and GUI mode are rejected rather than emulated, because those legitimately terminate or take
   over the process.
 */
int run_command_line(const std::vector<std::string>& args, std::string& error)
{
  error.clear();
  if (args.empty()) {
    error = "No arguments given.";
    return 1;
  }

  std::vector<const char *> argv;
  argv.reserve(args.size());
  for (const auto& arg : args) argv.push_back(arg.c_str());

  try {
    po::options_description desc = build_options_description();
    po::options_description hidden("Hidden options");
    hidden.add_options()("input-file", po::value<std::vector<std::string>>(), "input file");
    po::options_description all_options;
    all_options.add(desc).add(hidden);
    po::positional_options_description positional;
    positional.add("input-file", -1);

    po::variables_map vm;
    try {
      po::store(po::command_line_parser(static_cast<int>(argv.size()), argv.data())
                  .options(all_options)
                  .positional(positional)
                  .extra_parser(customSyntax)
                  .run(),
                vm);
    } catch (const std::exception& e) {
      throw CommandLineError(e.what());
    }

    for (const char *unsupported : {"help", "version", "info", "help-export", "command"}) {
      if (vm.count(unsupported)) {
        throw CommandLineError(std::string("--") + unsupported +
                               " is not supported when running in-process.");
      }
    }

    if (!vm.count("o")) {
      throw CommandLineError("An output file is required: pass -o <file>.");
    }
    const auto output_files = vm["o"].as<std::vector<std::string>>();
    if (!vm.count("input-file")) {
      throw CommandLineError("An input .scad file is required.");
    }
    const auto input_files = vm["input-file"].as<std::vector<std::string>>();
    if (input_files.size() != 1) {
      throw CommandLineError("Exactly one input file is supported.");
    }

    boost::optional<FileFormat> export_format;
    if (vm.count("export-format")) {
      const auto format_str = vm["export-format"].as<std::string>();
      FileFormat format;
      if (!fileformat::fromIdentifier(format_str, format)) {
        throw CommandLineError("Unknown --export-format '" + format_str + "'.");
      }
      export_format = format;
    }

    ViewOptions viewOptions{};
    if (vm.count("preview")) {
      viewOptions.renderer = vm["preview"].as<std::string>() == "throwntogether"
                               ? RenderType::THROWNTOGETHER
                               : RenderType::OPENCSG;
    } else if (vm.count("render")) {
      viewOptions.renderer =
        (vm["render"].as<std::string>() == "cgal" || vm["render"].as<std::string>() == "force")
          ? RenderType::BACKEND_SPECIFIC
          : RenderType::GEOMETRY;
    }
    viewOptions.previewer = (viewOptions.renderer == RenderType::THROWNTOGETHER)
                              ? Previewer::THROWNTOGETHER
                              : Previewer::OPENCSG;
    if (vm.count("view")) {
      for (const auto& option : vm["view"].as<CommaSeparatedVector>().values) {
        try {
          viewOptions[option] = true;
        } catch (const std::out_of_range&) {
          throw CommandLineError("Unknown --view option '" + option + "'.");
        }
      }
    }

    if (vm.count("D")) {
      for (const auto& assignment : vm["D"].as<std::vector<std::string>>()) {
        commandline_commands += assignment;
        commandline_commands += ";\n";
      }
    }
    if (vm.count("colorscheme")) {
      arg_colorscheme = vm["colorscheme"].as<std::string>();
      set_render_color_scheme(arg_colorscheme, true);
    }

    const Camera camera = get_camera(vm);
    const AnimateArgs animate = get_animate(vm);
    const CmdLineExportOptions export_options = convert_export_options(vm);
    const std::string parameterFile = vm.count("p") ? vm["p"].as<std::string>() : std::string();
    const std::string parameterSet = vm.count("P") ? vm["P"].as<std::string>() : std::string();
    const auto original_path = fs::current_path();

    parser_init();
    localization_init();

    int rc = 0;
    for (const auto& filename : output_files) {
      if (filename == "-") {
        throw CommandLineError("Writing to stdout is not supported when running in-process.");
      }
      const CommandLine cmd{false,
                            input_files[0],
                            false,
                            filename,
                            original_path,
                            parameterFile,
                            parameterSet,
                            viewOptions,
                            camera,
                            export_format,
                            export_options,
                            animate,
                            std::vector<std::string>{},
                            ""};
      rc |= cmdline(cmd);
    }
    return rc;
  } catch (const CommandLineError& e) {
    error = e.what();
    return 1;
  } catch (const std::exception& e) {
    error = e.what();
    return 1;
  }
}

int run_command_lines(const std::vector<std::string>& commands, std::string& error)
{
  error.clear();
  if (commands.empty()) {
    error = "No commands given.";
    return 1;
  }

  std::vector<std::string> commandLines;
  for (const auto& source : commands) {
    std::string contents = source;
    if (source == "-") {
      contents.assign(std::istreambuf_iterator<char>(std::cin), {});
    } else if (fs::exists(source)) {
      std::ifstream input(source);
      if (!input) {
        error = "Cannot read command file '" + source + "'.";
        return 1;
      }
      contents.assign(std::istreambuf_iterator<char>(input), {});
    }

    std::istringstream lines(contents);
    for (std::string line; std::getline(lines, line);) {
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (line.find_first_not_of(" \t") != std::string::npos) commandLines.push_back(std::move(line));
    }
  }
  if (commandLines.empty()) {
    error = "No commands given.";
    return 1;
  }

  for (size_t i = 0; i < commandLines.size(); ++i) {
    std::vector<std::string> args;
    try {
      args = po::split_unix(commandLines[i]);
    } catch (const std::exception& e) {
      error = "Command " + std::to_string(i + 1) + ": " + e.what();
      return 1;
    }
    args.insert(args.begin(), "openscad");
    if (run_command_line(args, error) != 0) {
      error = "Command " + std::to_string(i + 1) + ": " + error;
      return 1;
    }
  }
  return 0;
}
