#pragma once

/* GLView: A basic OpenGL rectangle for rendering images.

   This class is inherited by:

 * QGLview - for Qt GUI
 * OffscreenView - for offscreen rendering, in tests and from command-line
   (This class is also overridden by NULLGL.cc for special experiments)

   The view assumes either a Gimbal Camera (rotation,translation,distance)
   or Vector Camera (eye,center/target) is being used. See Camera.h. The
   cameras are not kept in sync.

   QGLView only uses GimbalCamera while OffscreenView can use either one.
   Some actions (showCrossHairs) only work properly on Gimbal Camera.

 */

#include <memory>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <string>
#include <vector>
#include "glview/Camera.h"
#include "io/depthmap.h"
#include "glview/ShaderUtils.h"
#include "geometry/linalg.h"
#include "glview/ColorMap.h"
#include "glview/system-gl.h"
#include "core/Selection.h"
#include "glview/Renderer.h"
#include "io/chromatic.h"
#include "io/coordinatemap.h"

/*!
   How the viewport (and the matching image exports) draw the model: ordinary
   shading, or one of the analysis views that encode data instead of appearance.

   One enum rather than a set of toggles because these are alternatives, not
   layers. Depth shading used to be a separate `bool showdepth`, which let the
   GUI show "Shade by Depth" checked while an agent lighting mode was active and
   the renderer silently ignored the fog - a state the UI could express and the
   renderer could not.
 */
enum class AnalysisMode {
  Default,
  Phong,
  Depth,
  DepthMetric,
  DepthMetricFine,
  Normal,
  Coordinate,
  Flat,
  Chromatic
};

class GLView
{
public:
  GLView();
  virtual ~GLView();

  void setupShader();
  void teardownShader();

  void setRenderer(std::shared_ptr<Renderer> r);
  [[nodiscard]] Renderer *getRenderer() const { return this->renderer.get(); }

  void initializeGL();
  void resizeGL(int w, int h);
  virtual void paintGL();

  void setCamera(const Camera& cam);
  void setupCamera();
  void setupDepthShading();
  void teardownDepthShading();

  void setColorScheme(const ColorScheme& cs);
  void setColorScheme(const std::string& cs);
  void updateColorScheme();

  [[nodiscard]] bool showAxes() const { return this->showaxes; }
  void setShowAxes(bool enabled) { this->showaxes = enabled; }
  [[nodiscard]] bool showScaleProportional() const { return this->showscale; }
  void setShowScaleProportional(bool enabled) { this->showscale = enabled; }
  [[nodiscard]] bool showEdges() const { return this->showedges; }
  void setShowEdges(bool enabled) { this->showedges = enabled; }
  [[nodiscard]] bool showCrosshairs() const { return this->showcrosshairs; }
  void setShowCrosshairs(bool enabled) { this->showcrosshairs = enabled; }
  //! True for any of the depth modes; the draw path asks in three places.
  [[nodiscard]] bool showDepth() const
  {
    return this->analysis_mode == AnalysisMode::Depth ||
           this->analysis_mode == AnalysisMode::DepthMetric ||
           this->analysis_mode == AnalysisMode::DepthMetricFine;
  }
  /*!
     The absolute scale the viewport is previewing, or 0 for the normalized
     depth view. A metric preview shows what the exported file contains rather
     than what is easiest to look at: near dark, far bright, background at the
     maximum, and the same fixed millimetre mapping the file uses. At 1mm units
     that is a nearly black screen for any desktop-scale model - which is the
     honest picture of what 8 bits of a 65.5m range looks like.
   */
  [[nodiscard]] double analysisDepthUnits() const
  {
    switch (this->analysis_mode) {
    case AnalysisMode::DepthMetric:     return DEPTHMAP_METRIC_SCALE;
    case AnalysisMode::DepthMetricFine: return DEPTHMAP_FINE_SCALE;
    default:                            return 0.0;
    }
  }
  //! Pin the depth shading to an explicit range instead of the bounding box, so
  //! the viewport matches an export made with the same range.
  void setDepthOptions(const DepthmapOptions& options) { this->depthoptions = options; }

  [[nodiscard]] AnalysisMode analysisMode() const { return this->analysis_mode; }
  void setAnalysisMode(AnalysisMode mode) { this->analysis_mode = mode; }
  //! The box the coordinate map normalizes against, for the sidecar.
  [[nodiscard]] CoordinateBounds coordinateBounds() const;

  void applyCoordinateBounds(ShaderUtils::ShaderInfo *shader) const;
  void applyChromaticLights(ShaderUtils::ShaderInfo *shader) const;
  //! Blit the analytic calibration sphere into the corner, after the model is drawn.
  void drawChromaticGauge();

public:
  //! Whether the chromatic mode draws its calibration sphere. On by default: the
  //! gauge is what makes the image interpretable, so suppressing it is the
  //! deliberate choice, taken when the overlay would occlude the geometry.
  void setChromaticGauge(bool enabled) { this->chromatic_gauge = enabled; }
  [[nodiscard]] bool chromaticGauge() const { return this->chromatic_gauge; }

  virtual bool save(const char *filename) const = 0;
  [[nodiscard]] virtual std::string getRendererInfo() const = 0;
  virtual float getDPI() { return 1.0f; }

  std::unique_ptr<ShaderUtils::ShaderInfo> edge_shader;
  std::unique_ptr<ShaderUtils::ShaderInfo> phong_shader;
  std::unique_ptr<ShaderUtils::ShaderInfo> agent_normal_shader;
  std::unique_ptr<ShaderUtils::ShaderInfo> agent_coord_shader;
  std::unique_ptr<ShaderUtils::ShaderInfo> agent_chromatic_shader;
  std::shared_ptr<Renderer> renderer;
  const ColorScheme *colorscheme;
  Camera cam;
  double far_far_away;
  double aspectratio;
  //! The clip planes setupCamera() last handed to the projection matrix. Kept
  //! so depth-buffer readback can linearize without re-deriving the formula -
  //! two copies of it would drift apart the first time the projection changes.
  double clipNear{0.0};
  double clipFar{0.0};
  bool showaxes;
  bool showedges;
  bool showcrosshairs;
  bool showscale;
  DepthmapOptions depthoptions{};
  AnalysisMode analysis_mode;
  bool chromatic_gauge = true;
  GLdouble modelview[16];
  GLdouble projection[16];
  std::vector<SelectedObject> selected_obj;
  std::vector<SelectedObject> shown_obj;

#ifdef ENABLE_OPENCSG
  bool is_opencsg_capable;
  bool has_shaders;
  void enable_opencsg_shaders();
  virtual void display_opencsg_warning() = 0;
  int opencsg_id;
#endif
  void showObject(const SelectedObject& pt, const Vector3d& eyedir);

private:
  void showCrosshairs(const Color4f& col);
  void showAxes(const Color4f& col);
  void showSmallaxes(const Color4f& col);
  void showScalemarkers(const Color4f& col);
  void decodeMarkerValue(double i, double l, int size_div_sm);
};
