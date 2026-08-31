#pragma once

#include "io/depthmap.h"

#include <boost/range/adaptor/map.hpp>
#include <boost/range/algorithm.hpp>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Settings.h"
#include "core/SourceFile.h"
#include "core/Tree.h"
#include "geometry/Geometry.h"
#include "geometry/linalg.h"
#include "glview/Camera.h"
#include "glview/GLView.h"
#include "glview/ColorMap.h"
#include "io/export_enums.h"

using SPDF = Settings::SettingsExportPdf;
using S3MF = Settings::SettingsExport3mf;

class PolySet;

struct UsdAnimationObject {
  std::shared_ptr<const PolySet> geometry;
  Transform3d transform;
  Color4f color;
  int nodeIndex;
};

struct UsdAnimationFrame {
  std::shared_ptr<const Geometry> geometry;
  std::vector<UsdAnimationObject> objects;
  std::optional<Camera> camera;
};

struct BlendExportOptions {
  size_t remeshSamples = 256;
  Color4f defaultColor{0.8f, 0.8f, 0.8f, 1.0f};
  // Unset means "ask the geometry", which records twice the $fa it was tessellated at.
  // Set only to override that for the whole export.
  std::optional<double> smoothAngle;
};

bool canExportObjectAnimation(const std::vector<UsdAnimationFrame>& frames);

enum class FileFormat {
  ASCII_STL,
  BINARY_STL,
  OBJ,
  OFF,
  WRL,
  AMF,
  _3MF,
  DXF,
  SVG,
  NEFDBG,
  NEF3,
  CSG,
  AST,
  TERM,
  ECHO,
  PNG,
  APNG,
  GIF,
  AVI,
  DEPTHMAP,
  PFM,
  NORMALMAP_PNG,
  CANNYMAP_PNG,
  COORDINATEMAP_PNG,
  FLATMAP_PNG,
  CHROMATIC_PNG,
  PDF,
  POV,
  PARAM,
  // Internal only: the binary payload a window and its private compute worker exchange.
  // Deliberately absent from the identifier table in export.cc, so it is not selectable
  // as an --export-format and cannot end up in a user's file.
  IPC_GEOMETRY,
  USDA,
  USDZ,
  BLEND
};

struct FileFormatInfo {
  FileFormat format;
  std::string identifier;
  std::string suffix;
  std::string description;
};

constexpr inline auto EXPORT_CREATOR = "OpenSCAD (https://www.openscad.org/)";

namespace fileformat {

std::vector<FileFormat> all();
std::vector<FileFormat> all2D();
std::vector<FileFormat> all3D();

const FileFormatInfo& info(FileFormat fileFormat);
bool fromIdentifier(const std::string& identifier, FileFormat& format);
const std::string& toSuffix(FileFormat format);
bool canPreview(FileFormat format);
/*!
   True for the animation containers, which hold a sequence of rendered frames and are
   only meaningful together with --animate.
 */
bool isAnimation(FileFormat format);
/*!
   True for the formats that fold every --animate frame into a single output file. A
   superset of isAnimation(): the video containers are meaningless without --animate,
   whereas USD is equally valid as a still, so it is animatable without requiring it.
 */
bool canAnimate(FileFormat format);
bool is3D(FileFormat format);
bool is2D(FileFormat format);

}  // namespace fileformat

using CmdLineExportOptions =
  std::unordered_map<std::string, std::unordered_map<std::string, std::string>>;

template <typename settings_entry_type>
auto set_cmd_line_option(const CmdLineExportOptions& cmdLineOptions, const std::string& section,
                         const settings_entry_type& se)
{
  if (cmdLineOptions.count(section) == 0) {
    return se.defaultValue();
  }

  const auto& o = cmdLineOptions.at(section);
  if (o.count(se.name()) == 0) {
    return se.defaultValue();
  }

  return se.decode(o.at(se.name()));
}

// include defaults to use without dialog or direction.
// Defaults match values used prior to incorporation of options.
struct ExportPdfOptions {
  bool showScale = true;
  bool showScaleMsg = true;
  bool showGrid = false;
  double gridSize = 10.0;
  bool showDesignFilename = false;
  ExportPdfPaperOrientation orientation = ExportPdfPaperOrientation::PORTRAIT;
  ExportPdfPaperSize paperSize = ExportPdfPaperSize::A4;
  bool addMetaData = SPDF::exportPdfAddMetaData.defaultValue();
  std::string metaDataTitle;
  std::string metaDataAuthor;
  std::string metaDataSubject;
  std::string metaDataKeywords;
  bool fill = false;
  std::string fillColor = "black";
  bool stroke = true;
  std::string strokeColor = "black";
  double strokeWidth = 1;

  static std::shared_ptr<const ExportPdfOptions> withOptions(const CmdLineExportOptions& cmdLineOptions)
  {
    return std::make_shared<const ExportPdfOptions>(ExportPdfOptions{
      .showScale = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                       Settings::SettingsExportPdf::exportPdfShowScale),
      .showScaleMsg = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                          Settings::SettingsExportPdf::exportPdfShowScaleMessage),
      .showGrid = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                      Settings::SettingsExportPdf::exportPdfShowGrid),
      .gridSize = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                      Settings::SettingsExportPdf::exportPdfGridSize),
      .showDesignFilename = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                                Settings::SettingsExportPdf::exportPdfShowFilename),
      .orientation = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                         Settings::SettingsExportPdf::exportPdfOrientation),
      .paperSize = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                       Settings::SettingsExportPdf::exportPdfPaperSize),
      .addMetaData = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                         Settings::SettingsExportPdf::exportPdfAddMetaData),
      .metaDataTitle = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                           Settings::SettingsExportPdf::exportPdfMetaDataTitle),
      .metaDataAuthor = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                            Settings::SettingsExportPdf::exportPdfMetaDataAuthor),
      .metaDataSubject = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                             Settings::SettingsExportPdf::exportPdfMetaDataSubject),
      .metaDataKeywords = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                              Settings::SettingsExportPdf::exportPdfMetaDataKeywords),
      .fill = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                  Settings::SettingsExportPdf::exportPdfFill),
      .fillColor = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                       Settings::SettingsExportPdf::exportPdfFillColor),
      .stroke = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                    Settings::SettingsExportPdf::exportPdfStroke),
      .strokeColor = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                         Settings::SettingsExportPdf::exportPdfStrokeColor),
      .strokeWidth = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_PDF,
                                         Settings::SettingsExportPdf::exportPdfStrokeWidth),
    });
  }

  static const std::shared_ptr<const ExportPdfOptions> fromSettings()
  {
    return std::make_shared<const ExportPdfOptions>(ExportPdfOptions{
      .showScale = SPDF::exportPdfShowScale.value(),
      .showScaleMsg = SPDF::exportPdfShowScaleMessage.value(),
      .showGrid = SPDF::exportPdfShowGrid.value(),
      .gridSize = SPDF::exportPdfGridSize.value(),
      .showDesignFilename = SPDF::exportPdfShowFilename.value(),
      .orientation = SPDF::exportPdfOrientation.value(),
      .paperSize = SPDF::exportPdfPaperSize.value(),
      .addMetaData = SPDF::exportPdfAddMetaData.value(),
      .metaDataTitle = SPDF::exportPdfMetaDataTitle.value(),
      .metaDataAuthor =
        SPDF::exportPdfAddMetaDataAuthor.value() ? SPDF::exportPdfMetaDataAuthor.value() : "",
      .metaDataSubject =
        SPDF::exportPdfAddMetaDataSubject.value() ? SPDF::exportPdfMetaDataSubject.value() : "",
      .metaDataKeywords =
        SPDF::exportPdfAddMetaDataKeywords.value() ? SPDF::exportPdfMetaDataKeywords.value() : "",
      .fill = SPDF::exportPdfFill.value(),
      .fillColor = SPDF::exportPdfFillColor.value(),
      .stroke = SPDF::exportPdfStroke.value(),
      .strokeColor = SPDF::exportPdfStrokeColor.value(),
      .strokeWidth = SPDF::exportPdfStrokeWidth.value(),
    });
  }
};

struct Export3mfOptions {
  Export3mfColorMode colorMode;
  Export3mfUnit unit;
  std::string color;
  Export3mfMaterialType materialType;
  int decimalPrecision;
  bool addMetaData;
  std::string metaDataTitle;
  std::string metaDataDesigner;
  std::string metaDataDescription;
  std::string metaDataCopyright;
  std::string metaDataLicenseTerms;
  std::string metaDataRating;

  static const std::shared_ptr<const Export3mfOptions> withOptions(
    const CmdLineExportOptions& cmdLineOptions)
  {
    return std::make_shared<const Export3mfOptions>(Export3mfOptions{
      .colorMode = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                       Settings::SettingsExport3mf::export3mfColorMode),
      .unit = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                  Settings::SettingsExport3mf::export3mfUnit),
      .color = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                   Settings::SettingsExport3mf::export3mfColor),
      .materialType = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                          Settings::SettingsExport3mf::export3mfMaterialType),
      .decimalPrecision = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                              Settings::SettingsExport3mf::export3mfDecimalPrecision),
      .addMetaData = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                         Settings::SettingsExport3mf::export3mfAddMetaData),
      .metaDataTitle = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                           Settings::SettingsExport3mf::export3mfMetaDataTitle),
      .metaDataDesigner = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                              Settings::SettingsExport3mf::export3mfMetaDataDesigner),
      .metaDataDescription =
        set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                            Settings::SettingsExport3mf::export3mfMetaDataDescription),
      .metaDataCopyright = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                               Settings::SettingsExport3mf::export3mfMetaDataCopyright),
      .metaDataLicenseTerms =
        set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                            Settings::SettingsExport3mf::export3mfMetaDataLicenseTerms),
      .metaDataRating = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_3MF,
                                            Settings::SettingsExport3mf::export3mfMetaDataRating),
    });
  }

  static const std::shared_ptr<const Export3mfOptions> fromSettings()
  {
    return std::make_shared<const Export3mfOptions>(Export3mfOptions{
      .colorMode = S3MF::export3mfColorMode.value(),
      .unit = S3MF::export3mfUnit.value(),
      .color = S3MF::export3mfColor.value(),
      .materialType = S3MF::export3mfMaterialType.value(),
      .decimalPrecision = S3MF::export3mfDecimalPrecision.value(),
      .addMetaData = S3MF::export3mfAddMetaData.value(),
      .metaDataTitle = S3MF::export3mfMetaDataTitle.value(),
      .metaDataDesigner =
        S3MF::export3mfAddMetaDataDesigner.value() ? S3MF::export3mfMetaDataDesigner.value() : "",
      .metaDataDescription =
        S3MF::export3mfAddMetaDataDescription.value() ? S3MF::export3mfMetaDataDescription.value() : "",
      .metaDataCopyright =
        S3MF::export3mfAddMetaDataCopyright.value() ? S3MF::export3mfMetaDataCopyright.value() : "",
      .metaDataLicenseTerms = S3MF::export3mfAddMetaDataLicenseTerms.value()
                                ? S3MF::export3mfMetaDataLicenseTerms.value()
                                : "",
      .metaDataRating =
        S3MF::export3mfAddMetaDataRating.value() ? S3MF::export3mfMetaDataRating.value() : "",
    });
  }
};

struct ExportSvgOptions {
  bool fill = false;
  std::string fillColor = "white";
  bool stroke = true;
  std::string strokeColor = "black";
  double strokeWidth = 0.35;

  static std::shared_ptr<const ExportSvgOptions> withOptions(const CmdLineExportOptions& cmdLineOptions)
  {
    return std::make_shared<const ExportSvgOptions>(ExportSvgOptions{
      .fill = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_SVG,
                                  Settings::SettingsExportSvg::exportSvgFill),
      .fillColor = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_SVG,
                                       Settings::SettingsExportSvg::exportSvgFillColor),
      .stroke = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_SVG,
                                    Settings::SettingsExportSvg::exportSvgStroke),
      .strokeColor = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_SVG,
                                         Settings::SettingsExportSvg::exportSvgStrokeColor),
      .strokeWidth = set_cmd_line_option(cmdLineOptions, Settings::SECTION_EXPORT_SVG,
                                         Settings::SettingsExportSvg::exportSvgStrokeWidth),
    });
  }

  static const std::shared_ptr<const ExportSvgOptions> fromSettings()
  {
    return std::make_shared<const ExportSvgOptions>(ExportSvgOptions{
      .fill = Settings::SettingsExportSvg::exportSvgFill.value(),
      .fillColor = Settings::SettingsExportSvg::exportSvgFillColor.value(),
      .stroke = Settings::SettingsExportSvg::exportSvgStroke.value(),
      .strokeColor = Settings::SettingsExportSvg::exportSvgStrokeColor.value(),
      .strokeWidth = Settings::SettingsExportSvg::exportSvgStrokeWidth.value(),
    });
  }
};

struct ExportInfo {
  FileFormat format;
  FileFormatInfo info;
  std::string title;
  std::string sourceFilePath;  // Full path to the OpenSCAD source file
  const Camera *camera;
  const Color4f defaultColor;
  const ColorScheme *colorScheme;
  int offPrecision = 0;

  std::shared_ptr<const ExportPdfOptions> optionsPdf;
  std::shared_ptr<const Export3mfOptions> options3mf;
  std::shared_ptr<const ExportSvgOptions> optionsSvg;
};

ExportInfo createExportInfo(const FileFormat& format, const FileFormatInfo& info,
                            const std::string& filepath, const Camera *camera,
                            const CmdLineExportOptions& cmdLineOptions);

bool exportFileByName(const std::shared_ptr<const class Geometry>& root_geom,
                      const std::string& filename, const ExportInfo& exportInfo);
bool exportFileStdOut(const std::shared_ptr<const class Geometry>& root_geom,
                      const ExportInfo& exportInfo);

void export_stl(const std::shared_ptr<const Geometry>& geom, std::ostream& output, bool binary = true);
bool export_stl_files(const std::shared_ptr<const Geometry>& geom, const std::string& filename,
                      const ExportInfo& exportInfo, bool overwrite);
// True when a model has more than one body, i.e. when splitting it across one
// STL per body is a meaningful choice to offer.
bool multi_stl_available(const std::shared_ptr<const Geometry>& geom);

std::vector<std::string> multi_stl_filenames(const std::shared_ptr<const Geometry>& geom,
                                             const std::string& filename);
// The bodies a geometry exports as, in source order, and the label each of them
// carries (see Material::bodyLabels).
Geometry::Geometries export_bodies(const std::shared_ptr<const Geometry>& geom);
// The name each body carries inside a container format that can name its
// objects: its material label, or the traditional "OpenSCAD Model" name
// (numbered when there is more than one) for bodies with no material name.
std::vector<std::string> export_body_names(const Geometry::Geometries& bodies);
void export_3mf(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                const ExportInfo& exportInfo);
void export_obj(const std::shared_ptr<const Geometry>& geom, std::ostream& output);
void export_off(const std::shared_ptr<const Geometry>& geom, std::ostream& output);
void export_wrl(const std::shared_ptr<const Geometry>& geom, std::ostream& output);
void export_amf(const std::shared_ptr<const Geometry>& geom, std::ostream& output);
void export_dxf(const std::shared_ptr<const Geometry>& geom, std::ostream& output);
void export_svg(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                const ExportInfo& exportInfo);
void export_pov(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                const ExportInfo& exportInfo);
void export_usda(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                 const ExportInfo& exportInfo);
void export_usdz(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                 const ExportInfo& exportInfo);
/*!
   Writes one USD stage covering every animation frame. OpenSCAD re-evaluates the script per
   frame, so topology may change between frames; USD represents that natively by
   time-sampling points/faceVertexCounts/faceVertexIndices.
 */
void export_usda_animation(const std::vector<std::shared_ptr<const Geometry>>& frames, unsigned fps,
                           std::ostream& output, const ExportInfo& exportInfo);
void export_usdz_animation(const std::vector<std::shared_ptr<const Geometry>>& frames, unsigned fps,
                           std::ostream& output, const ExportInfo& exportInfo);
void export_usda_animation(const std::vector<UsdAnimationFrame>& frames, unsigned fps,
                           std::ostream& output, const ExportInfo& exportInfo);
void export_usdz_animation(const std::vector<UsdAnimationFrame>& frames, unsigned fps,
                           std::ostream& output, const ExportInfo& exportInfo);
void export_blend_animation(const std::vector<UsdAnimationFrame>& frames, unsigned fps,
                            std::ostream& output, const BlendExportOptions& options = {});
void export_pdf(const std::shared_ptr<const Geometry>& geom, std::ostream& output,
                const ExportInfo& exportInfo);
void export_nefdbg(const std::shared_ptr<const Geometry>& geom, std::ostream& output);
void export_nef3(const std::shared_ptr<const Geometry>& geom, std::ostream& output);

enum class Previewer { OPENCSG, THROWNTOGETHER };
enum class RenderType { GEOMETRY, BACKEND_SPECIFIC, OPENCSG, THROWNTOGETHER };

struct ViewOption {
  const std::string name;
  bool& value;
};

struct ViewOptions {
  double edgeWidth = 1.0;
  bool canny = false;
  Previewer previewer{Previewer::OPENCSG};
  RenderType renderer{RenderType::OPENCSG};

  std::map<std::string, bool> flags{
    {"axes", false},
    {"scales", false},
    {"edges", false},
    {"crosshairs", false},
    {"transparent", false},
    {"shaded", false},
    // Shade the model by distance rather than by lighting. A render toggle, not
    // an output encoding, which is why it belongs here and the depthmap profile
    // does not.
    {"depth", false},
    // The two absolute-scale previews. Separate flags rather than a value on
    // "depth" because --view takes a list of names, and because they render
    // differently enough (near dark, background at the maximum) to be their own
    // thing rather than a variant of it.
    {"depth-metric", false},
    {"depth-metric10um", false},
  };

  const std::vector<std::string> names()
  {
    std::vector<std::string> names;
    boost::copy(flags | boost::adaptors::map_keys, std::back_inserter(names));
    return names;
  }

  bool& operator[](const std::string& name) { return flags.at(name); }

  bool operator[](const std::string& name) const { return flags.at(name); }
};

class OffscreenView;

std::string get_current_iso8601_date_time_utc();

/*!
   Build and paint an offscreen preview.

   Every setting here must be supplied to this call rather than set on the view it
   returns: this function paints, and export_png(const OffscreenView&) only saves
   the framebuffer that paint left behind. Anything applied afterwards never
   reaches a paint, and the export silently writes an ordinary shaded image.
 */
std::unique_ptr<OffscreenView> prepare_preview(Tree& tree, const ViewOptions& options, Camera& camera,
                                               const DepthmapOptions& depthOptions = {},
                                               AnalysisMode agentMode = AnalysisMode::Default,
                                               bool chromaticGauge = true);
bool export_png(const std::shared_ptr<const class Geometry>& root_geom, const ViewOptions& options,
                Camera& camera, std::ostream& output);
//! As above, but carrying the depth options so --view=depth shades with the same
//! range a depthmap export would encode with.
bool export_png(const std::shared_ptr<const class Geometry>& root_geom, const ViewOptions& options,
                Camera& camera, const DepthmapOptions& depthOptions, std::ostream& output);
bool export_png(const OffscreenView& glview, std::ostream& output);

bool export_depthmap(const std::shared_ptr<const class Geometry>& root_geom, const ViewOptions& options,
                     Camera& camera, DepthProfile profile, std::ostream& output);
bool export_depthmap(const OffscreenView& glview, DepthProfile profile, std::ostream& output);
bool export_depthmap(const std::shared_ptr<const class Geometry>& root_geom, const ViewOptions& options,
                     Camera& camera, const DepthmapOptions& depthOptions, std::ostream& output);
bool export_depthmap(const OffscreenView& glview, const DepthmapOptions& depthOptions,
                     std::ostream& output);
bool export_pfm(const std::shared_ptr<const class Geometry>& root_geom, const ViewOptions& options,
                Camera& camera, std::ostream& output);
bool export_pfm(const OffscreenView& glview, std::ostream& output);

/*!
   Renders one animation frame and hands its RGBA pixels to `encoder`, instead of
   writing a still image. The encoder must already be open at the camera's pixel size.
 */
bool export_video_frame(const OffscreenView& glview, class VideoEncoder& encoder);
bool export_video_frame(const std::shared_ptr<const class Geometry>& root_geom,
                        const ViewOptions& options, Camera& camera, class VideoEncoder& encoder);
bool export_param(SourceFile *root, const fs::path& path, std::ostream& output);

std::unique_ptr<PolySet> createSortedPolySet(const PolySet& ps);
