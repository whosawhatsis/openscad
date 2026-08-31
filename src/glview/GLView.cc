#include "glview/GLView.h"
#include "glview/FeatureEdges.h"
#include "Feature.h"
#include "geometry/linalg.h"
#include "glview/ShaderUtils.h"
#include "core/Selection.h"
#include "glview/system-gl.h"
#include "glview/preview/OpenCSGRenderer.h"
#include "glview/ColorMap.h"
#include "glview/RenderSettings.h"
#include "utils/printutils.h"
#include "glview/Renderer.h"
#include "utils/degree_trig.h"
#include "glview/hershey.h"
#include "io/depthmap.h"
#include "Feature.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <vector>
#include <memory>
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <string>

#ifdef ENABLE_OPENCSG
#include <opencsg.h>
#endif

GLView::GLView()
{
  aspectratio = 1;
  showedges = false;
  showaxes = false;
  showcrosshairs = false;
  showscale = false;
  analysis_mode = AnalysisMode::Default;
  colorscheme = &ColorMap::instance().defaultColorScheme();
  cam = Camera();
  far_far_away = RenderSettings::inst()->far_gl_clip_limit;
#ifdef ENABLE_OPENCSG
  is_opencsg_capable = false;
  has_shaders = false;
  static int sId = 0;
  this->opencsg_id = sId++;
#endif
}

GLView::~GLView()
{
  teardownShader();
}

void GLView::setupShader()
{
  if (edge_shader) return;

  auto resource = ShaderUtils::compileShaderProgram(ShaderUtils::loadShaderSource("ViewEdges.vert"),
                                                    ShaderUtils::loadShaderSource("ViewEdges.frag"));

  edge_shader = std::make_unique<ShaderUtils::ShaderInfo>(ShaderUtils::ShaderInfo{
    .resource = resource,
    .type = ShaderUtils::ShaderType::EDGE_RENDERING,
    .uniforms = {},
    .attributes =
      {
        {"barycentric", glGetAttribLocation(resource.shader_program, "barycentric")},
      },
  });

  auto phong_resource = ShaderUtils::compileShaderProgram(ShaderUtils::loadShaderSource("Phong.vert"),
                                                          ShaderUtils::loadShaderSource("Phong.frag"));
  phong_shader = std::make_unique<ShaderUtils::ShaderInfo>(ShaderUtils::ShaderInfo{
    .resource = phong_resource,
    .type = ShaderUtils::ShaderType::AGENT_RENDERING,
    .uniforms = {{"showEdges", glGetUniformLocation(phong_resource.shader_program, "showEdges")}},
    .attributes =
      {
        {"barycentric", glGetAttribLocation(phong_resource.shader_program, "barycentric")},
        {"material", glGetAttribLocation(phong_resource.shader_program, "material")},
      },
  });

  auto normal_resource =
    ShaderUtils::compileShaderProgram(ShaderUtils::loadShaderSource("AgentNormalMap.vert"),
                                      ShaderUtils::loadShaderSource("AgentNormalMap.frag"));
  agent_normal_shader = std::make_unique<ShaderUtils::ShaderInfo>(ShaderUtils::ShaderInfo{
    .resource = normal_resource,
    .type = ShaderUtils::ShaderType::AGENT_RENDERING,
    .uniforms = {},
    .attributes = {},
  });

  auto coord_resource =
    ShaderUtils::compileShaderProgram(ShaderUtils::loadShaderSource("AgentCoordinateMap.vert"),
                                      ShaderUtils::loadShaderSource("AgentCoordinateMap.frag"));
  agent_coord_shader = std::make_unique<ShaderUtils::ShaderInfo>(ShaderUtils::ShaderInfo{
    .resource = coord_resource,
    .type = ShaderUtils::ShaderType::AGENT_RENDERING,
    .uniforms =
      {
        {"coordMin", glGetUniformLocation(coord_resource.shader_program, "coordMin")},
        {"coordExtent", glGetUniformLocation(coord_resource.shader_program, "coordExtent")},
        {"coordDegenerate", glGetUniformLocation(coord_resource.shader_program, "coordDegenerate")},
      },
    .attributes = {},
  });

  auto chromatic_resource =
    ShaderUtils::compileShaderProgram(ShaderUtils::loadShaderSource("AgentChromatic.vert"),
                                      ShaderUtils::loadShaderSource("AgentChromatic.frag"));
  agent_chromatic_shader = std::make_unique<ShaderUtils::ShaderInfo>(ShaderUtils::ShaderInfo{
    .resource = chromatic_resource,
    .type = ShaderUtils::ShaderType::AGENT_RENDERING,
    .uniforms =
      {
        {"lightRed", glGetUniformLocation(chromatic_resource.shader_program, "lightRed")},
        {"lightGreen", glGetUniformLocation(chromatic_resource.shader_program, "lightGreen")},
        {"lightBlue", glGetUniformLocation(chromatic_resource.shader_program, "lightBlue")},
      },
    .attributes = {},
  });
}

CoordinateBounds GLView::coordinateBounds() const
{
  // Pinned to the model's own bounding box, not to what is currently on screen:
  // a box recomputed per frame would make the same point encode differently at
  // two camera angles, and the sidecar would describe only one of them.
  if (!this->renderer) {
    const double unit_min[3] = {0.0, 0.0, 0.0};
    const double unit_max[3] = {1.0, 1.0, 1.0};
    return coordinate_bounds(unit_min, unit_max);
  }
  const BoundingBox bbox = this->renderer->getBoundingBox();
  if (bbox.isEmpty()) {
    const double unit_min[3] = {0.0, 0.0, 0.0};
    const double unit_max[3] = {1.0, 1.0, 1.0};
    return coordinate_bounds(unit_min, unit_max);
  }
  const double bmin[3] = {bbox.min().x(), bbox.min().y(), bbox.min().z()};
  const double bmax[3] = {bbox.max().x(), bbox.max().y(), bbox.max().z()};
  return coordinate_bounds(bmin, bmax);
}

void GLView::applyCoordinateBounds(ShaderUtils::ShaderInfo *shader) const
{
  const CoordinateBounds bounds = coordinateBounds();
  glUseProgram(shader->resource.shader_program);
  glUniform3f(shader->uniforms.at("coordMin"), static_cast<GLfloat>(bounds.min[0]),
              static_cast<GLfloat>(bounds.min[1]), static_cast<GLfloat>(bounds.min[2]));
  glUniform3f(shader->uniforms.at("coordExtent"), static_cast<GLfloat>(bounds.extent[0]),
              static_cast<GLfloat>(bounds.extent[1]), static_cast<GLfloat>(bounds.extent[2]));
  glUniform3f(shader->uniforms.at("coordDegenerate"), bounds.degenerate[0] ? 1.0f : 0.0f,
              bounds.degenerate[1] ? 1.0f : 0.0f, bounds.degenerate[2] ? 1.0f : 0.0f);
  // Uniforms are per-program state and survive the unbind. Leaving the program
  // bound does not: a later paint in a mode that binds no program of its own
  // would keep drawing with this one.
  glUseProgram(0);
}

void GLView::applyChromaticLights(ShaderUtils::ShaderInfo *shader) const
{
  const auto lights = chromatic_lights();
  static const char *uniform_names[3] = {"lightRed", "lightGreen", "lightBlue"};
  glUseProgram(shader->resource.shader_program);
  for (const auto& light : lights) {
    glUniform3f(shader->uniforms.at(uniform_names[light.channel]), static_cast<GLfloat>(light.dir[0]),
                static_cast<GLfloat>(light.dir[1]), static_cast<GLfloat>(light.dir[2]));
  }
  glUseProgram(0);
}

void GLView::drawChromaticGauge()
{
  // Sized as a fraction of the shorter side so it stays legible at any export
  // resolution, and always in the same corner so a consumer knows where to crop
  // it out. Drawn last, straight into the color buffer: it is a reference
  // overlay, not part of the scene, and must not be lit, projected or depth
  // tested along with the model.
  // Framebuffer pixels, not cam.pixel_width: glWindowPos2i and glDrawPixels work
  // in the framebuffer's own coordinates, and on a HiDPI display that is twice
  // the camera's logical size. Using the logical width put the gauge at half the
  // intended x - the lower right of the lower-left quadrant.
  GLint viewport[4] = {0, 0, 0, 0};
  glGetIntegerv(GL_VIEWPORT, viewport);
  const int fb_width = viewport[2] > 0 ? viewport[2] : cam.pixel_width;
  const int fb_height = viewport[3] > 0 ? viewport[3] : cam.pixel_height;
  const int shorter = std::min(fb_width, fb_height);
  const auto size = static_cast<std::uint32_t>(std::max(32, shorter / 5));
  if (size == 0) return;
  const GaugeImage gauge = render_gauge_sphere(size);

  glUseProgram(0);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_LIGHTING);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // glDrawPixels reads bottom row first; render_gauge_sphere produces top row
  // first, so the rows are fed in reverse rather than flipping the buffer.
  std::vector<std::uint8_t> flipped(gauge.pixels.size());
  const size_t row_bytes = static_cast<size_t>(size) * 4;
  for (std::uint32_t y = 0; y < size; ++y) {
    std::copy_n(gauge.pixels.begin() + static_cast<long>((size - 1 - y) * row_bytes), row_bytes,
                flipped.begin() + static_cast<long>(y * row_bytes));
  }

  // Bottom-right, not bottom-left: the axes indicator and scale markers live in
  // the bottom-left of the viewport and the gauge would sit on top of them.
  const int margin = static_cast<int>(size) / 8;
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glWindowPos2i(fb_width - static_cast<int>(size) - margin, margin);
  glDrawPixels(static_cast<GLsizei>(size), static_cast<GLsizei>(size), GL_RGBA, GL_UNSIGNED_BYTE,
               flipped.data());

  glEnable(GL_DEPTH_TEST);
}

void GLView::teardownShader()
{
  feature_edge_resources.reset();
  if (edge_shader == nullptr) return;  // if OpenGL context was not initialized
  if (edge_shader->resource.shader_program) {
    glDeleteProgram(edge_shader->resource.shader_program);
  }
  if (edge_shader->resource.vertex_shader) {
    glDeleteShader(edge_shader->resource.vertex_shader);
  }
  if (edge_shader->resource.fragment_shader) {
    glDeleteShader(edge_shader->resource.fragment_shader);
  }

  if (phong_shader && phong_shader->resource.shader_program) {
    glDeleteProgram(phong_shader->resource.shader_program);
    glDeleteShader(phong_shader->resource.vertex_shader);
    glDeleteShader(phong_shader->resource.fragment_shader);
  }

  if (agent_normal_shader && agent_normal_shader->resource.shader_program) {
    glDeleteProgram(agent_normal_shader->resource.shader_program);
    glDeleteShader(agent_normal_shader->resource.vertex_shader);
    glDeleteShader(agent_normal_shader->resource.fragment_shader);
  }
  if (agent_coord_shader && agent_coord_shader->resource.shader_program) {
    glDeleteProgram(agent_coord_shader->resource.shader_program);
    glDeleteShader(agent_coord_shader->resource.vertex_shader);
    glDeleteShader(agent_coord_shader->resource.fragment_shader);
  }
  if (agent_chromatic_shader && agent_chromatic_shader->resource.shader_program) {
    glDeleteProgram(agent_chromatic_shader->resource.shader_program);
    glDeleteShader(agent_chromatic_shader->resource.vertex_shader);
    glDeleteShader(agent_chromatic_shader->resource.fragment_shader);
  }
  edge_shader.reset();
  phong_shader.reset();
  agent_normal_shader.reset();
  agent_coord_shader.reset();
  agent_chromatic_shader.reset();
}

void GLView::setRenderer(std::shared_ptr<Renderer> r)
{
  this->renderer = r;
}

/* update the color schemes of the Renderer attached to this GLView
   to match the colorscheme of this GLView.*/
void GLView::updateColorScheme()
{
  if (this->renderer) this->renderer->setColorScheme(*this->colorscheme);
}

/* change this GLView's colorscheme to the one given, and update the
   Renderer attached to this GLView as well. */
void GLView::setColorScheme(const ColorScheme& cs)
{
  this->colorscheme = &cs;
  this->updateColorScheme();
}

void GLView::setColorScheme(const std::string& cs)
{
  const auto colorscheme = ColorMap::instance().findColorScheme(cs);
  if (colorscheme) {
    setColorScheme(*colorscheme);
  } else {
    LOG(message_group::UI_Warning, "GLView: unknown colorscheme %1$s", cs);
  }
}

void GLView::resizeGL(int w, int h)
{
  cam.pixel_width = w;
  cam.pixel_height = h;
  glViewport(0, 0, w, h);
  aspectratio = 1.0 * w / h;

  // FIXME: Only run once, not every time the window is resized
  setupShader();
}

void GLView::setCamera(const Camera& cam)
{
  this->cam = cam;
}

void GLView::setupCamera()
{
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  auto dist = cam.zoomValue();
  switch (this->cam.projection) {
  case Camera::ProjectionType::PERSPECTIVE: {
    this->clipNear = 0.1 * dist;
    this->clipFar = 100 * dist;
    gluPerspective(cam.fov, aspectratio, this->clipNear, this->clipFar);
    break;
  }
  default:
  case Camera::ProjectionType::ORTHOGONAL: {
    auto height = dist * tan_degrees(cam.fov / 2);
    this->clipNear = -100 * dist;
    this->clipFar = +100 * dist;
    glOrtho(-height * aspectratio, height * aspectratio, -height, height, this->clipNear, this->clipFar);
    break;
  }
  }
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  gluLookAt(0.0, -dist, 0.0,  // eye
            0.0, 0.0, 0.0,    // center
            0.0, 0.0, 1.0);   // up

  glRotated(cam.object_rot.x(), 1.0, 0.0, 0.0);
  glRotated(cam.object_rot.y(), 0.0, 1.0, 0.0);
  glRotated(cam.object_rot.z(), 0.0, 0.0, 1.0);
  glTranslated(cam.object_trans[0], cam.object_trans[1],
               cam.object_trans[2]);  // translation be part of modelview matrix!
  glGetDoublev(GL_MODELVIEW_MATRIX, this->modelview);
  glTranslated(-cam.object_trans[0], -cam.object_trans[1], -cam.object_trans[2]);
  glGetDoublev(GL_PROJECTION_MATRIX, this->projection);
}

/*
   Shade the model by distance instead of by lighting, so the viewport shows what
   a depth map export will contain.

   This is GL_LINEAR fog with a black fog color over white geometry: fog blends
   f*white + (1-f)*black where f is linear in eye-space distance, so the fragment
   color *is* the depth. Nothing is read back and no shader is involved.

   Fog distance is eye-space and already linear, so unlike the export path this
   needs none of linearize_depth()'s unprojection - and none of its precision
   hazard either.

   The range is pinned to the model's bounding sphere rather than to what is
   currently on screen, so the shading does not swim while the model is rotated.
 */
void GLView::setupDepthShading()
{
  // Use the bounding box's actual eye-space depth extent rather than a bounding
  // sphere: a sphere overestimates badly for anything not cube-shaped, and the
  // wasted range shows up directly as washed-out contrast.
  // The model's bounding sphere, capped at the viewing distance - orientation
  // invariant, so turning the model no longer rebalances the shading. An
  // explicit -O depthmap/range= still wins over it.
  DepthRange range{0.0, 1.0};
  const BoundingBox bbox = this->renderer ? this->renderer->getBoundingBox() : BoundingBox();
  if (!bbox.isEmpty()) {
    const double bmin[3] = {bbox.min().x(), bbox.min().y(), bbox.min().z()};
    const double bmax[3] = {bbox.max().x(), bbox.max().y(), bbox.max().z()};
    const Eigen::Vector3d vpt = cam.getVpt();
    const double center[3] = {vpt.x(), vpt.y(), vpt.z()};
    range = capped_sphere_range(bmin, bmax, center, this->modelview);
  }
  if (this->depthoptions.has_explicit_range) {
    range = resolve_depth_range(this->depthoptions, range.start, range.end);
  }

  // A metric preview ignores the fitted range entirely: the file's mapping is
  // absolute, from zero to whatever the unit size allows, and the preview is only
  // honest if it uses the same one.
  const double units = analysisDepthUnits();
  const bool metric = units > 0.0;
  if (metric) {
    range.start = 0.0;
    range.end = 65534.0 / units;
  }

  const auto polarity = depth_preview_polarity(units);
  const GLfloat fogcolor[4] = {polarity.background, polarity.background, polarity.background, 1.0f};
  glFogi(GL_FOG_MODE, GL_LINEAR);
  glFogf(GL_FOG_START, static_cast<GLfloat>(range.start));
  glFogf(GL_FOG_END, static_cast<GLfloat>(range.end));
  glFogfv(GL_FOG_COLOR, fogcolor);
  glEnable(GL_FOG);

  // Fog blends f*C + (1-f)*fogcolour, so the result is only depth if C is
  // constant. glColor3f is not enough: the VBO renderers supply per-vertex
  // colors, which win with lighting off. Instead keep lighting on, take
  // GL_COLOR_MATERIAL out (so vertex colors stop feeding the material), and
  // make the material purely emissive - emission ignores normals, so every
  // fragment gets the profile's constant geometry value regardless of orientation.
  const GLfloat geometry[4] = {polarity.geometry, polarity.geometry, polarity.geometry, 1.0f};
  const GLfloat black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  glEnable(GL_LIGHTING);
  glDisable(GL_COLOR_MATERIAL);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, geometry);
  glMaterialfv(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE, black);
  glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, black);
}

//! Undo setupDepthShading() so the decorations drawn afterwards, and the next
//! frame, are not left emissive white inside a fog bank.
void GLView::teardownDepthShading()
{
  const GLfloat black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
  glDisable(GL_FOG);
  glMaterialfv(GL_FRONT_AND_BACK, GL_EMISSION, black);
  glEnable(GL_COLOR_MATERIAL);
}

void GLView::paintGL()
{
  const bool pureEdges =
    analysis_mode == AnalysisMode::Canny || analysis_mode == AnalysisMode::Wireframe;
  glDisable(GL_LIGHTING);
  auto bgcol = ColorMap::getColor(*this->colorscheme, RenderColor::BACKGROUND_COLOR);
  auto bgstopcol = ColorMap::getColor(*this->colorscheme, RenderColor::BACKGROUND_STOP_COLOR);
  if (analysis_mode != AnalysisMode::Default && analysis_mode != AnalysisMode::Shaded) {
    // An analysis image is data, so its background must not depend on which color
    // scheme the user happens to have selected, and must not be a gradient - both
    // would decode as varying "surface" values where there is no surface. Flat,
    // Black, flat, and documented as the no-geometry marker.
    const float bg = showDepth() ? depth_preview_polarity(analysisDepthUnits()).background : 0.0f;
    bgcol = Color4f(bg, bg, bg, 1.0f);
    bgstopcol = bgcol;
  }
  auto axescolor = ColorMap::getColor(*this->colorscheme, RenderColor::AXES_COLOR);
  auto crosshaircol = ColorMap::getColor(*this->colorscheme, RenderColor::CROSSHAIR_COLOR);

  // With transparent compositing the scene is always rendered onto a fully transparent buffer and
  // the background is composited underneath at the end of this function. Clearing to (0,0,0,0)
  // rather than to the background color is what makes partially transparent geometry come out
  // right: together with the separate alpha blend function below, the buffer ends up holding
  // premultiplied RGBA, which can be composited over any background without color error.
  const bool composite_background = Feature::ExperimentalTransparentCompositing.is_enabled();
  // A transparent export must be rendered this way whether or not the feature is on: exporting an
  // image with the background matted into it is a defect, not a default worth preserving. The
  // feature flag additionally applies it to the *live view*, which is the part that changes what
  // every user sees on every frame and therefore deserves gating.
  const bool premultiplied = composite_background || transparent_background;

  if (premultiplied) {
    glClearColor(0.0, 0.0, 0.0, 0.0);
  } else {
    glClearColor(bgcol.r(), bgcol.g(), bgcol.b(), 1.0);
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  if (premultiplied) {
    // Accumulate coverage in the alpha channel instead of blending it as if it were a color
    // channel. Source colors stay non-premultiplied, so no renderer or shader needs to change; only
    // the destination becomes premultiplied, which is what correct compositing requires.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  }

  // ponytail: a gradient background can't be transparent, so just skip it
  // Under compositing the background (gradient included) is drawn underneath at the end instead.
  if (bgcol != bgstopcol && !premultiplied) {
    glDisable(GL_DEPTH_TEST);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // draw screen aligned quad with color gradient
    glBegin(GL_QUADS);
    glColor3f(bgcol.r(), bgcol.g(), bgcol.b());
    glVertex2f(-1.0f, +1.0f);
    glVertex2f(+1.0f, +1.0f);

    glColor3f(bgstopcol.r(), bgstopcol.g(), bgstopcol.b());
    glVertex2f(+1.0f, -1.0f);
    glVertex2f(-1.0f, -1.0f);
    glEnd();
    glEnable(GL_DEPTH_TEST);
  }

  setupCamera();

  // The crosshair should be fixed at the center of the viewport...
  if (showcrosshairs && !pureEdges) GLView::showCrosshairs(crosshaircol);
  glTranslated(cam.object_trans.x(), cam.object_trans.y(), cam.object_trans.z());
  // ...the axis lines need to follow the object translation.
  if (showaxes && !pureEdges) GLView::showAxes(axescolor);
  // mark the scale along the axis lines
  if (showaxes && showscale && !pureEdges) GLView::showScalemarkers(axescolor);

  glEnable(GL_LIGHTING);
  glDepthFunc(GL_LESS);
  glCullFace(GL_BACK);
  glDisable(GL_CULL_FACE);
  glLineWidth(2);
  glColor3d(1.0, 0.0, 0.0);

  // Applies to the model only: the background gradient, axes and crosshairs are
  // drawn above, and the small axes below, all outside the fog.
  if (showDepth()) setupDepthShading();

  if (this->renderer) {
#if defined(ENABLE_OPENCSG)
    // FIXME: This belongs in the OpenCSG renderer, but it doesn't know about this ID yet
    OpenCSG::setContext(this->opencsg_id);
#endif
    ShaderUtils::ShaderInfo *active_shader = edge_shader.get();
    if (analysis_mode == AnalysisMode::Shaded && phong_shader) {
      active_shader = phong_shader.get();
      glUseProgram(active_shader->resource.shader_program);
      glUniform1i(active_shader->uniforms.at("showEdges"), showedges ? GL_TRUE : GL_FALSE);
      glUseProgram(0);
    } else if (analysis_mode == AnalysisMode::Normal && agent_normal_shader) {
      active_shader = agent_normal_shader.get();
    } else if (analysis_mode == AnalysisMode::Coordinate && agent_coord_shader) {
      active_shader = agent_coord_shader.get();
      applyCoordinateBounds(active_shader);
    } else if (analysis_mode == AnalysisMode::Chromatic && agent_chromatic_shader) {
      active_shader = agent_chromatic_shader.get();
      applyChromaticLights(active_shader);
    }

    // The shaded mode always needs the barycentric attribute bound; its uniform decides
    // whether those coordinates affect the final color.
    bool active_showedges = showedges || analysis_mode == AnalysisMode::Shaded;
    if (analysis_mode != AnalysisMode::Default && analysis_mode != AnalysisMode::Shaded) {
      active_showedges = false;
    }
    // Set both ways every paint rather than only disabling. A one-way disable
    // leaks into the next paint, so switching back to Default left the viewport
    // unlit - and the chromatic gauge, which also turns lighting off, leaked the
    // same way. Both are fixed by making this state a function of the current
    // mode instead of a side effect of having once been in another one.
    if (analysis_mode == AnalysisMode::Flat || analysis_mode == AnalysisMode::Chromatic) {
      glDisable(GL_LIGHTING);
    } else {
      glEnable(GL_LIGHTING);
    }

    // Smooth shading belongs to the Shaded mode only, so it is decided here where the
    // mode is known rather than in the renderer, which only sees a shader type shared
    // by every analysis mode.
    this->renderer->setSmoothShading(analysis_mode == AnalysisMode::Shaded);
    this->renderer->prepare(active_shader);
    // Phong emits premultiplied material RGB plus an unattenuated reflected
    // highlight, so its RGB must not be multiplied by alpha a second time.
    if (analysis_mode == AnalysisMode::Shaded) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    const bool featureEdges =
      (showedges || analysis_mode == AnalysisMode::Canny || analysis_mode == AnalysisMode::Wireframe);
    if (!featureEdges) feature_edge_error.clear();
    if (analysis_mode != AnalysisMode::Canny && analysis_mode != AnalysisMode::Wireframe) {
      if (featureEdges && analysis_mode == AnalysisMode::Shaded) {
        glUseProgram(phong_shader->resource.shader_program);
        glUniform1i(phong_shader->uniforms.at("showEdges"), GL_FALSE);
        glUseProgram(0);
      }
      this->renderer->draw(featureEdges ? false : active_showedges, active_shader);
    }
    if (featureEdges) {
      try {
        drawFeatureEdges(
          *this, analysis_mode == AnalysisMode::Canny,
          analysis_mode != AnalysisMode::Canny && analysis_mode != AnalysisMode::Wireframe);
        feature_edge_error.clear();
      } catch (const std::exception& error) {
        if (feature_edge_error != error.what()) LOG(message_group::Error, "%1$s", error.what());
        feature_edge_error = error.what();
      }
    }
    if (analysis_mode == AnalysisMode::Shaded) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (depth_preview_polarity(analysisDepthUnits()).invert) {
      // OpenCSG relies on black fog while constructing its internal CSG mask.
      // Invert only the finished image to get metric near-dark polarity without
      // exposing those internal passes to white fog.
      glPushAttrib(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_ENABLE_BIT | GL_CURRENT_BIT);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_LIGHTING);
      glDisable(GL_FOG);
      glEnable(GL_BLEND);
      glBlendFunc(GL_ONE_MINUS_DST_COLOR, GL_ZERO);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

      glMatrixMode(GL_PROJECTION);
      glPushMatrix();
      glLoadIdentity();
      glMatrixMode(GL_MODELVIEW);
      glPushMatrix();
      glLoadIdentity();
      glColor3f(1.0f, 1.0f, 1.0f);
      glBegin(GL_QUADS);
      glVertex2f(-1.0f, -1.0f);
      glVertex2f(1.0f, -1.0f);
      glVertex2f(1.0f, 1.0f);
      glVertex2f(-1.0f, 1.0f);
      glEnd();
      glPopMatrix();
      glMatrixMode(GL_PROJECTION);
      glPopMatrix();
      glMatrixMode(GL_MODELVIEW);
      glPopAttrib();
    }
    if (analysis_mode == AnalysisMode::Chromatic && chromatic_gauge) {
      drawChromaticGauge();
    }
  }
  Vector3d eyedir(this->modelview[2], this->modelview[6], this->modelview[10]);
  glColor3f(1, 0, 0);
  for (const SelectedObject& obj : this->selected_obj) {
    if (!pureEdges) showObject(obj, eyedir);
  }
  glColor3f(0, 1, 0);
  for (const SelectedObject& obj : this->shown_obj) {
    if (!pureEdges) showObject(obj, eyedir);
  }
  if (showDepth()) teardownDepthShading();
  glDisable(GL_LIGHTING);
  if (showaxes && !pureEdges) GLView::showSmallaxes(axescolor);

  // Workaround for inconsistent QT behavior related to handling custom OpenGL widgets that
  // leave non opaque alpha values in final output.
  // On wayland that can cause window to become transparent or blurry trail effect in the
  // parts that contain partially transparent objects.
  //
  // At the end of rendering clear alpha value, so that it doesn't matter how rest of the
  // compositing stack at QT and desktop level would interpret transparent pixels.
  //
  // Solves https://github.com/openscad/openscad/issues/3689.
  //
  // Originally developed by @karliss for FreeCAD (https://github.com/FreeCAD/FreeCAD/pull/19499).
  if (premultiplied) {
    if (!transparent_background) {
      // Composite the background *underneath* what has been drawn (destination-over). The color
      // blend adds bg*(1 - dstAlpha), which is exactly the missing contribution for a premultiplied
      // destination; the alpha blend drives the result to opaque, which also does the job of the
      // alpha-scrub workaround below.
      glDisable(GL_DEPTH_TEST);
      glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ONE, GL_ONE, GL_ONE);

      glMatrixMode(GL_PROJECTION);
      glLoadIdentity();
      glMatrixMode(GL_MODELVIEW);
      glLoadIdentity();

      if (bgcol.a() < 1.0f) {
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        const int vp_w = vp[2] > 0 ? vp[2] : 800;
        const int vp_h = vp[3] > 0 ? vp[3] : 600;
        const int size = 16;
        const int cols = (vp_w + size - 1) / size;
        const int rows = (vp_h + size - 1) / size;

        glBegin(GL_QUADS);
        for (int r = 0; r < rows; ++r) {
          float y1 = 1.0f - 2.0f * (r * size) / vp_h;
          float y2 = 1.0f - 2.0f * std::min((r + 1) * size, vp_h) / vp_h;
          for (int c = 0; c < cols; ++c) {
            float x1 = -1.0f + 2.0f * (c * size) / vp_w;
            float x2 = -1.0f + 2.0f * std::min((c + 1) * size, vp_w) / vp_w;
            if ((r + c) % 2 == 0) {
              glColor4f(0.85f, 0.85f, 0.85f, 1.0f);
            } else {
              glColor4f(0.70f, 0.70f, 0.70f, 1.0f);
            }
            glVertex2f(x1, y1);
            glVertex2f(x2, y1);
            glVertex2f(x2, y2);
            glVertex2f(x1, y2);
          }
        }
        glEnd();
      } else {
        glBegin(GL_QUADS);
        glColor4f(bgcol.r(), bgcol.g(), bgcol.b(), 1.0f);
        glVertex2f(-1.0f, +1.0f);
        glVertex2f(+1.0f, +1.0f);
        glColor4f(bgstopcol.r(), bgstopcol.g(), bgstopcol.b(), 1.0f);
        glVertex2f(+1.0f, -1.0f);
        glVertex2f(-1.0f, -1.0f);
        glEnd();
      }

      glEnable(GL_DEPTH_TEST);
    }
    // Leave the blend function as the rest of the code expects to find it.
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  } else if (!transparent_background) {
    GLboolean mask[4];
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    glColorMask(false, false, false, true);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);
  }
}

#ifdef ENABLE_OPENCSG

void glCompileCheck(GLuint shader)
{
  GLint status;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (status == GL_FALSE) {
    int loglen;
    char logbuffer[1000];
    glGetShaderInfoLog(shader, sizeof(logbuffer), &loglen, logbuffer);
    PRINTDB("OpenGL Shader Program Compile Error:\n%s", logbuffer);
  }
}

void GLView::enable_opencsg_shaders()
{
  // All OpenGL 2 contexts are OpenCSG capable
#ifdef USE_GLEW
  const bool hasOpenGL2_0 = GLEW_VERSION_2_0;
#endif
#ifdef USE_GLAD
  const bool hasOpenGL2_0 = GLAD_GL_VERSION_2_0;
#endif
  if (hasOpenGL2_0) {
    this->is_opencsg_capable = true;
    this->has_shaders = true;
  } else {
    display_opencsg_warning();
  }
}
#endif  // ifdef ENABLE_OPENCSG

#ifdef DEBUG
// Requires OpenGL 4.3+
/*
   void GLAPIENTRY MessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity,
                                  GLsizei length, const GLchar* message, const void* userParam)
   {
    fprintf(stderr, "GL CALLBACK: %s type = 0x%X, severity = 0x%X, message = %s\n",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
            type, severity, message);
   }
   //*/
#endif

void GLView::initializeGL()
{
  OpenCSGRenderer::clearCache();

#ifdef DEBUG
/*
   // Requires OpenGL 4.3+
   glEnable              ( GL_DEBUG_OUTPUT );
   glDebugMessageCallback( MessageCallback, 0 );
   //*/
#endif

  glEnable(GL_DEPTH_TEST);
  glDepthRange(-far_far_away, +far_far_away);

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  GLfloat light_diffuse[] = {1.0, 1.0, 1.0, 1.0};
  GLfloat light_position0[] = {-1.0, +1.0, +1.0, 0.0};
  GLfloat light_position1[] = {+1.0, -1.0, -1.0, 0.0};

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glLightfv(GL_LIGHT0, GL_DIFFUSE, light_diffuse);
  glLightfv(GL_LIGHT0, GL_POSITION, light_position0);
  glEnable(GL_LIGHT0);
  glLightfv(GL_LIGHT1, GL_DIFFUSE, light_diffuse);
  glLightfv(GL_LIGHT1, GL_POSITION, light_position1);
  glEnable(GL_LIGHT1);
  glEnable(GL_LIGHTING);
  glEnable(GL_NORMALIZE);

  glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
  // The following line is reported to fix issue #71
  glMateriali(GL_FRONT_AND_BACK, GL_SHININESS, 64);
  glEnable(GL_COLOR_MATERIAL);
#ifdef ENABLE_OPENCSG
  enable_opencsg_shaders();
#endif
}

void GLView::showSmallaxes(const Color4f& col)
{
  auto dpi = this->getDPI();
  // Small axis cross in the lower left corner
  glDepthFunc(GL_ALWAYS);

  // Set up an orthographic projection of the axis cross in the corner
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glTranslatef(-0.8f, -0.8f, 0.0f);
  auto scale = 90.0;
  glOrtho(-scale * dpi * aspectratio, scale * dpi * aspectratio, -scale * dpi, scale * dpi, -scale * dpi,
          scale * dpi);
  gluLookAt(0.0, -1.0, 0.0,  // eye
            0.0, 0.0, 0.0,   // center
            0.0, 0.0, 1.0);  // up

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glRotated(cam.object_rot.x(), 1.0, 0.0, 0.0);
  glRotated(cam.object_rot.y(), 0.0, 1.0, 0.0);
  glRotated(cam.object_rot.z(), 0.0, 0.0, 1.0);

  glLineWidth(dpi);
  glBegin(GL_LINES);
  glColor3d(1.0, 0.0, 0.0);
  glVertex3d(0, 0, 0);
  glVertex3d(10 * dpi, 0, 0);
  glColor3d(0.0, 1.0, 0.0);
  glVertex3d(0, 0, 0);
  glVertex3d(0, 10 * dpi, 0);
  glColor3d(0.0, 0.0, 1.0);
  glVertex3d(0, 0, 0);
  glVertex3d(0, 0, 10 * dpi);
  glEnd();

  GLdouble mat_model[16];
  glGetDoublev(GL_MODELVIEW_MATRIX, mat_model);

  GLdouble mat_proj[16];
  glGetDoublev(GL_PROJECTION_MATRIX, mat_proj);

  GLint viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);

  GLdouble xlabel_x, xlabel_y, xlabel_z;
  gluProject(12 * dpi, 0, 0, mat_model, mat_proj, viewport, &xlabel_x, &xlabel_y, &xlabel_z);
  xlabel_x = std::round(xlabel_x);
  xlabel_y = std::round(xlabel_y);

  GLdouble ylabel_x, ylabel_y, ylabel_z;
  gluProject(0, 12 * dpi, 0, mat_model, mat_proj, viewport, &ylabel_x, &ylabel_y, &ylabel_z);
  ylabel_x = std::round(ylabel_x);
  ylabel_y = std::round(ylabel_y);

  GLdouble zlabel_x, zlabel_y, zlabel_z;
  gluProject(0, 0, 12 * dpi, mat_model, mat_proj, viewport, &zlabel_x, &zlabel_y, &zlabel_z);
  zlabel_x = std::round(zlabel_x);
  zlabel_y = std::round(zlabel_y);

  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glTranslated(-1, -1, 0);
  glScaled(2.0 / viewport[2], 2.0 / viewport[3], 1);

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glColor3f(col.r(), col.g(), col.b());

  float d = 3 * dpi;
  glBegin(GL_LINES);
  // X Label
  glVertex3d(xlabel_x - d, xlabel_y - d, 0);
  glVertex3d(xlabel_x + d, xlabel_y + d, 0);
  glVertex3d(xlabel_x - d, xlabel_y + d, 0);
  glVertex3d(xlabel_x + d, xlabel_y - d, 0);
  // Y Label
  glVertex3d(ylabel_x - d, ylabel_y - d, 0);
  glVertex3d(ylabel_x + d, ylabel_y + d, 0);
  glVertex3d(ylabel_x - d, ylabel_y + d, 0);
  glVertex3d(ylabel_x, ylabel_y, 0);
  // Z Label
  glVertex3d(zlabel_x - d, zlabel_y - d, 0);
  glVertex3d(zlabel_x + d, zlabel_y - d, 0);
  glVertex3d(zlabel_x - d, zlabel_y + d, 0);
  glVertex3d(zlabel_x + d, zlabel_y + d, 0);
  glVertex3d(zlabel_x - d, zlabel_y - d, 0);
  glVertex3d(zlabel_x + d, zlabel_y + d, 0);
  glEnd();
}

void GLView::showAxes(const Color4f& col)
{
  // Large gray axis cross inline with the model
  glLineWidth(this->getDPI());
  glColor3f(col.r(), col.g(), col.b());

  glBegin(GL_LINES);
  glVertex4d(0, 0, 0, 1);
  glVertex4d(1, 0, 0, 0);  // w = 0 goes to infinity
  glVertex4d(0, 0, 0, 1);
  glVertex4d(0, 1, 0, 0);
  glVertex4d(0, 0, 0, 1);
  glVertex4d(0, 0, 1, 0);
  glEnd();

  glPushAttrib(GL_LINE_BIT);
  glEnable(GL_LINE_STIPPLE);
  glLineStipple(3, 0xAAAA);
  glBegin(GL_LINES);
  glVertex4d(0, 0, 0, 1);
  glVertex4d(-1, 0, 0, 0);
  glVertex4d(0, 0, 0, 1);
  glVertex4d(0, -1, 0, 0);
  glVertex4d(0, 0, 0, 1);
  glVertex4d(0, 0, -1, 0);
  glEnd();
  glPopAttrib();
}

void GLView::showCrosshairs(const Color4f& col)
{
  glLineWidth(this->getDPI());
  glColor3f(col.r(), col.g(), col.b());
  glBegin(GL_LINES);
  for (double xf : {-1.0, 1.0})
    for (double yf : {-1.0, 1.0}) {
      auto vd = cam.zoomValue() / 8;
      glVertex3d(-xf * vd, -yf * vd, -vd);
      glVertex3d(+xf * vd, +yf * vd, +vd);
    }
  glEnd();
}

void GLView::showObject(const SelectedObject& obj, const Vector3d& eyedir)
{
  auto vd = cam.zoomValue() / 200.0;
  switch (obj.type) {
  case SelectionType::SELECTION_POINT: {
    double n = 1 / sqrt(3);
    // create an octaeder
    // x- x+ y- y+ z- z+
    int sequence[] = {2, 0, 4, 1, 2, 4, 0, 3, 4, 3, 1, 4, 0, 2, 5, 2, 1, 5, 3, 0, 5, 1, 3, 5};
    glBegin(GL_TRIANGLES);
    for (int i = 0; i < 8; i++) {
      glNormal3f((i & 1) ? -n : n, (i & 2) ? -n : n, (i & 4) ? -n : n);
      for (int j = 0; j < 3; j++) {
        int code = sequence[i * 3 + j];
        switch (code) {
        case 0: glVertex3d(obj.p1[0] - vd, obj.p1[1], obj.p1[2]); break;
        case 1: glVertex3d(obj.p1[0] + vd, obj.p1[1], obj.p1[2]); break;
        case 2: glVertex3d(obj.p1[0], obj.p1[1] - vd, obj.p1[2]); break;
        case 3: glVertex3d(obj.p1[0], obj.p1[1] + vd, obj.p1[2]); break;
        case 4: glVertex3d(obj.p1[0], obj.p1[1], obj.p1[2] - vd); break;
        case 5: glVertex3d(obj.p1[0], obj.p1[1], obj.p1[2] + vd); break;
        }
      }
    }
    glEnd();
  } break;
  case SelectionType::SELECTION_LINE: {
    Vector3d diff = obj.p2 - obj.p1;
    Vector3d wdir = eyedir.cross(diff).normalized() * vd / 2.0;
    glBegin(GL_QUADS);
    glVertex3d(obj.p1[0] - wdir[0], obj.p1[1] - wdir[1], obj.p1[2] - wdir[2]);
    glVertex3d(obj.p2[0] - wdir[0], obj.p2[1] - wdir[1], obj.p2[2] - wdir[2]);
    glVertex3d(obj.p2[0] + wdir[0], obj.p2[1] + wdir[1], obj.p2[2] + wdir[2]);
    glVertex3d(obj.p1[0] + wdir[0], obj.p1[1] + wdir[1], obj.p1[2] + wdir[2]);
    glEnd();
  } break;
  }
}

void GLView::showScalemarkers(const Color4f& col)
{
  // Add scale ticks on large axes
  auto l = cam.zoomValue();
  glLineWidth(this->getDPI());
  glColor3f(col.r(), col.g(), col.b());

  // Take log of l, discretize, then exponentiate. This is done so that the tick
  // denominations change every time the viewport gets 10x bigger or smaller,
  // but stays constant in-between. l_adjusted is a step function of l.
  const int log_l = static_cast<int>(floor(log10(l)));
  const double l_adjusted = pow(10, log_l);

  // Calculate tick width.
  const double tick_width = l_adjusted / 10.0;

  const int size_div_sm = 60;  // divisor for l to determine minor tick size
  int line_cnt = 0;

  size_t divs = l / tick_width;
  for (size_t div = 0; div < divs; ++div) {
    double i = div * tick_width;  // i represents the position along the axis
    int size_div;
    if (line_cnt > 0 && line_cnt % 10 == 0) {        // major tick
      size_div = size_div_sm * .5;                   // resize to a major tick
      GLView::decodeMarkerValue(i, l, size_div_sm);  // print number
    } else {                                         // minor tick
      size_div = size_div_sm;                        // set the minor tick to the standard size

      // Draw additional labels if there are few major tick labels visible due to
      // zoom. Because the spacing/units of major tick marks only change when the
      // viewport changes size by a factor of 10, it can be hard to see the
      // major tick labels when when the viewport is slightly larger than size at
      // which the last tick spacing change occurred. When zoom level is such
      // that very few major tick marks are visible, additional labels are drawn
      // every 2 minor ticks. We can detect that very few major ticks are visible
      // by checking if the viewport size is larger than the adjusted scale by
      // only a small ratio.
      const double more_labels_threshold = 3;
      // draw additional labels every 2 minor ticks
      const int more_labels_freq = 2;
      if (line_cnt > 0 && line_cnt % more_labels_freq == 0 && l / l_adjusted < more_labels_threshold) {
        GLView::decodeMarkerValue(i, l, size_div_sm);  // print number
      }
    }
    line_cnt++;

    /*
     * The length of each tick is proportional to the length of the axis
     * (which changes with the zoom value.) l/size_div provides the
     * proportional length
     *
     * Commented glVertex3d lines provide additional 'arms' for the tick
     * the number of arms will (hopefully) eventually be driven via Preferences
     */

    // positive axes
    glBegin(GL_LINES);
    // x
    glVertex3d(i, 0, 0);
    glVertex3d(i, -l / size_div, 0);  // 1 arm
    // glVertex3d(i,-l/size_div,0); glVertex3d(i,l/size_div,0); // 2 arms
    // glVertex3d(i,0,-l/size_div); glVertex3d(i,0,l/size_div); // 4 arms (w/ 2 arms line)

    // y
    glVertex3d(0, i, 0);
    glVertex3d(-l / size_div, i, 0);  // 1 arm
    // glVertex3d(-l/size_div,i,0); glVertex3d(l/size_div,i,0); // 2 arms
    // glVertex3d(0,i,-l/size_div); glVertex3d(0,i,l/size_div); // 4 arms (w/ 2 arms line)

    // z
    glVertex3d(0, 0, i);
    glVertex3d(-l / size_div, 0, i);  // 1 arm
    // glVertex3d(-l/size_div,0,i); glVertex3d(l/size_div,0,i); // 2 arms
    // glVertex3d(0,-l/size_div,i); glVertex3d(0,l/size_div,i); // 4 arms (w/ 2 arms line)
    glEnd();

    // negative axes
    glPushAttrib(GL_LINE_BIT);
    glEnable(GL_LINE_STIPPLE);
    glLineStipple(3, 0xAAAA);
    glBegin(GL_LINES);
    // x
    glVertex3d(-i, 0, 0);
    glVertex3d(-i, -l / size_div, 0);  // 1 arm
    // glVertex3d(-i,-l/size_div,0); glVertex3d(-i,l/size_div,0); // 2 arms
    // glVertex3d(-i,0,-l/size_div); glVertex3d(-i,0,l/size_div); // 4 arms (w/ 2 arms line)

    // y
    glVertex3d(0, -i, 0);
    glVertex3d(-l / size_div, -i, 0);  // 1 arm
    // glVertex3d(-l/size_div,-i,0); glVertex3d(l/size_div,-i,0); // 2 arms
    // glVertex3d(0,-i,-l/size_div); glVertex3d(0,-i,l/size_div); // 4 arms (w/ 2 arms line)

    // z
    glVertex3d(0, 0, -i);
    glVertex3d(-l / size_div, 0, -i);  // 1 arm
    // glVertex3d(-l/size_div,0,-i); glVertex3d(l/size_div,0,-i); // 2 arms
    // glVertex3d(0,-l/size_div,-i); glVertex3d(0,l/size_div,-i); // 4 arms (w/ 2 arms line)
    glEnd();
    glPopAttrib();
  }
}

void GLView::decodeMarkerValue(double i, double l, int size_div_sm)
{
  // We draw both at once the positive and corresponding negative number.
  const std::string pos_number_str = STR(i);
  const std::string neg_number_str = "-" + pos_number_str;

  const float font_size = (l / size_div_sm);
  const float baseline_offset = font_size / 5;  // hovering a bit above axis

  // Length of the minus sign. We want the digits to be centered around
  // their ticks, but not have the minus prefix shift center of gravity.
  const float prefix_offset = hershey::TextWidth("-", font_size) / 2;

  // Draw functions that help map 2D axis label drawings into their plane.
  // Since we're just on axis, no need for fancy affine transformation,
  // just calling glVertex3d() with coordinates in the right plane.
  using PlaneVertexDraw =
    std::function<void(float x, float y, float font_height, float baseline_offset)>;

  const PlaneVertexDraw axis_draw_planes[3] = {
    [](float x, float y, float /*fh*/, float bl) {
      glVertex3d(x, y + bl, 0);  // x-label along x-axis; font drawn above line
    },
    [](float x, float y, float fh, float bl) {
      glVertex3d(-y + (fh + bl), x, 0);  // y-label along y-axis; font below
    },
    [](float x, float y, float fh, float bl) {
      glVertex3d(-y + (fh + bl), 0, x);  // z-label along z-axis; font below
    },
  };
  bool needs_glend = false;
  for (const PlaneVertexDraw& axis_draw : axis_draw_planes) {
    // We get 'plot instructions', a sequence of vertices. Translate into gl ops
    const auto plot_fun = [&](bool pen_down, float x, float y) {
      if (!pen_down) {  // Start a new line, coordinates just move not draw
        if (needs_glend) glEnd();
        glBegin(GL_LINE_STRIP);
        needs_glend = true;
      }
      axis_draw(x, y, font_size, baseline_offset);
    };

    hershey::DrawTextHershey(pos_number_str, i, 0, hershey::TextAlign::kCenter, font_size, plot_fun);
    if (needs_glend) glEnd();
    needs_glend = false;
    hershey::DrawTextHershey(neg_number_str, -i - prefix_offset, 0, hershey::TextAlign::kCenter,
                             font_size, plot_fun);
    if (needs_glend) glEnd();
    needs_glend = false;
  }
}
