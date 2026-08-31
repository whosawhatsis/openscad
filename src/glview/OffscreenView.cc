#include "glview/OffscreenView.h"
#include "glview/system-gl.h"
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <fstream>
#include <vector>

#include "io/imageutils.h"
#include "lodepng/lodepng.h"
#include "Feature.h"
#include "utils/printutils.h"
#include "glview/OffscreenContextFactory.h"
#include "glview/fbo.h"
#if defined(USE_GLEW) || defined(OPENCSG_GLEW)
#include "glview/glew-utils.h"
#endif

namespace {

/*!
 Capture framebuffer from OpenGL and write it to the given ostream.
 Called by save_framebuffer() from platform-specific code.
*/
bool save_framebuffer(const OpenGLContext *ctx, std::ostream& output, bool with_alpha)
{
  if (!ctx) return false;

  const auto pixels = ctx->getFramebuffer();

  const size_t samplesPerPixel = 4;  // R, G, B and A
  // Flip it vertically - images read from OpenGL buffers are upside-down
  std::vector<uint8_t> flippedBuffer(samplesPerPixel * ctx->height() * ctx->width());
  flip_image(&pixels[0], flippedBuffer.data(), samplesPerPixel, ctx->width(), ctx->height());

  if (with_alpha) {
    // A transparent render always leaves the framebuffer premultiplied; PNG stores straight alpha,
    // so undo it. Fully opaque and fully transparent pixels need no correction -- the latter carry
    // no recoverable color at all.
    for (size_t i = 0; i < flippedBuffer.size(); i += samplesPerPixel) {
      const unsigned a = flippedBuffer[i + 3];
      if (a == 0 || a == 255) continue;
      for (size_t c = 0; c < 3; ++c) {
        flippedBuffer[i + c] =
          static_cast<uint8_t>(std::min(255u, (flippedBuffer[i + c] * 255u + a / 2) / a));
      }
    }
  }

  return write_png(output, flippedBuffer.data(), ctx->width(), ctx->height(), with_alpha);
}

}  // namespace

OffscreenView::OffscreenView(uint32_t width, uint32_t height)
{
  OffscreenContextFactory::ContextAttributes attrib = {
    .width = width,
    .height = height,
    .majorGLVersion = 2,
    .minorGLVersion = 0,
  };
  auto provider = OffscreenContextFactory::defaultProvider();
  // We cannot initialize GLX GLEW with an EGL context:
  // https://github.com/nigels-com/glew/issues/273
  // ..so if we're using GLEW, default to creating a GLX context.
  // FIXME: It's possible that GLEW was built using EGL, in which case this
  // logic isn't correct, but we don't have a good way of determining how GLEW was built.
#if defined(USE_GLEW) || defined(OPENCSG_GLEW)
  provider = !strcmp(provider, "egl") ? "glx" : provider;
#endif
  this->ctx = OffscreenContextFactory::create(provider, attrib);
  if (!this->ctx) {
    // If the provider defaulted to EGL, fall back to GLX if EGL failed
    if (!strcmp(provider, "egl")) {
      this->ctx = OffscreenContextFactory::create("glx", attrib);
    }
    if (!this->ctx) {
      throw OffscreenViewException("Unable to obtain GL Context");
    }
  }
  if (!this->ctx->makeCurrent()) throw OffscreenViewException("Unable to make GL context current");

#ifndef NULLGL
#if defined(USE_GLEW) || defined(OPENCSG_GLEW)
  if (!initializeGlew()) {
    throw OffscreenViewException("Unable to initialize Glew");
  }
#endif  // USE_GLEW
#ifdef USE_GLAD
  // We could ask for gladLoadGLES2UserPtr() here if we want to use GLES2+
  const auto version = gladLoaderLoadGL();
  if (version == 0) {
    throw OffscreenViewException("Unable to initialize GLAD");
  }
  PRINTDB("GLAD: Loaded OpenGL %d.%d", GLAD_VERSION_MAJOR(version) % GLAD_VERSION_MINOR(version));
#endif  // USE_GLAD

#endif  // NULLGL

  PRINTD(gl_dump());

  this->fbo = createFBO(width, height);
  if (!fbo) {
    throw OffscreenViewException("Unable to create FBO");
  }
  GLView::initializeGL();
  GLView::resizeGL(width, height);
}

OffscreenView::~OffscreenView()
{
#ifndef NULLGL
  if (ctx) ctx->makeCurrent();
  teardownShader();
#endif
  fbo.reset();
}

#ifdef ENABLE_OPENCSG
void OffscreenView::display_opencsg_warning()
{
  LOG("OpenSCAD recommended OpenGL version is 2.0.");
}
#endif

bool OffscreenView::save(const char *filename) const
{
  if (!feature_edge_error.empty()) return false;
  std::ofstream fstream(filename, std::ios::out | std::ios::binary);
  if (!fstream.is_open()) {
    std::cerr << "Can't open file " << filename << " for writing";
    return false;
  } else {
    return save(fstream);
  }
}

bool OffscreenView::saveDepth(std::ostream& output, DepthProfile profile) const
{
  DepthmapOptions opts;
  opts.profile = profile;
  return saveDepth(output, opts);
}

bool OffscreenView::saveDepth(std::ostream& output, const DepthmapOptions& options) const
{
  if (!this->ctx) return false;

  CameraParameters camParams;
  for (int i = 0; i < 16; ++i) {
    camParams.modelview[i] = static_cast<double>(this->modelview[i]);
    camParams.projection[i] = static_cast<double>(this->projection[i]);
  }
  camParams.clipNear = this->clipNear;
  camParams.clipFar = this->clipFar;
  camParams.fov = this->cam.fov;
  camParams.ortho = (this->cam.projection == Camera::ProjectionType::ORTHOGONAL);
  camParams.viewport[0] = static_cast<int>(this->ctx->width());
  camParams.viewport[1] = static_cast<int>(this->ctx->height());

  if (!options.camera_sidecar_path.empty()) {
    std::ofstream sidecar(options.camera_sidecar_path);
    if (sidecar.is_open()) {
      sidecar << serialize_camera_json(camParams);
      sidecar.close();
    }
    // Failing silently would hand back a depth map that cannot be unprojected,
    // with nothing to say why.
    if (!sidecar) {
      LOG(message_group::Error, "Can't write camera sidecar '%1$s'.", options.camera_sidecar_path);
      return false;
    }
  }

  const bool perspective = this->cam.projection == Camera::ProjectionType::PERSPECTIVE;
  const auto mm =
    linearize_depth(this->ctx->getDepthbuffer(), this->clipNear, this->clipFar, perspective);

  // Without an explicit range, normalize across the same capped bounding sphere
  // the viewport shades with, rather than across whatever happens to be visible.
  // Two reasons: preview and file agree, and the range no longer moves with the
  // camera - two renders of one model are comparable to each other. Geometry
  // outside it clamps (nearer than start is pure white, beyond end pure black)
  // instead of being re-normalized into a gradient that can run backwards.
  DepthmapOptions effective = options;
  if (!effective.has_explicit_range && this->renderer) {
    const BoundingBox bbox = this->renderer->getBoundingBox();
    if (!bbox.isEmpty()) {
      const double bmin[3] = {bbox.min().x(), bbox.min().y(), bbox.min().z()};
      const double bmax[3] = {bbox.max().x(), bbox.max().y(), bbox.max().z()};
      double mv[16];
      for (int i = 0; i < 16; ++i) mv[i] = static_cast<double>(this->modelview[i]);
      const Eigen::Vector3d vpt = this->cam.getVpt();
      const double center[3] = {vpt.x(), vpt.y(), vpt.z()};
      const DepthRange r = capped_sphere_range(bmin, bmax, center, mv);
      effective.has_explicit_range = true;
      effective.explicit_near = r.start;
      effective.explicit_far = r.end;
      effective.range_from_model = true;
    }
  }
  const auto image = encode_depthmap(mm, this->ctx->width(), this->ctx->height(), effective);

  // Same as the color path: buffers read from OpenGL are upside-down.
  std::vector<uint8_t> flipped(image.pixels.size());
  flip_image(image.pixels.data(), flipped.data(), image.bytesPerPixel, this->ctx->width(),
             this->ctx->height());

  if (image.clipped) {
    LOG(message_group::Warning,
        "Depthmap: geometry outside the %1$s range %2$.3f - %3$.3f mm was clamped.",
        effective.range_from_model ? "model's" : "requested", effective.explicit_near,
        effective.explicit_far);
  }
  if (const double units = depth_units_per_mm(options.profile); units > 0.0) {
    // The ceiling is worth stating outright: at 10um units it is 655mm, which a
    // scene framed from further away silently saturates against.
    LOG("Depthmap: %1$.3f - %2$.3f mm from the camera, %3$g units per mm (max %4$.2f mm).",
        image.minDepth, image.maxDepth, units, 65534.0 / units);
    if (image.maxDepth * units > 65534.0) {
      LOG(message_group::Warning,
          "Depthmap: geometry beyond %1$.2f mm saturated - the %2$g units/mm profile cannot "
          "represent it.",
          65534.0 / units, units);
    }
    // Embed what the file needs to be decodable, so a metric depth map is
    // self-describing even when it travels without its sidecar. The sidecar
    // stays available: PFM has nowhere to put this, and plenty of pipelines
    // re-encode PNGs and drop text chunks on the way.
    std::ostringstream scale;
    // Fixed, not full precision: these are a scale factor and a limit for a human
    // or a parser to read, and 655.34000000000003 helps neither.
    scale << std::fixed << std::setprecision(2);
    scale << "{\n  \"units_per_mm\": " << units << ",\n  \"max_mm\": " << (65534.0 / units)
          << ",\n  \"background\": 65535,\n"
          << "  \"encoding\": \"distance_mm = value / units_per_mm, from the camera\"\n}\n";
    const std::vector<std::pair<std::string, std::string>> metadata = {
      {"openscad.depthmap", scale.str()},
      {"openscad.camera", serialize_camera_json(camParams)},
    };
    return write_png_gray16(output, flipped.data(), this->ctx->width(), this->ctx->height(), metadata);
  }
  LOG("Depthmap: %1$.3f - %2$.3f mm from the camera, normalized across that range.", image.minDepth,
      image.maxDepth);
  return write_png(output, flipped.data(), this->ctx->width(), this->ctx->height());
}

bool OffscreenView::save(std::ostream& output) const
{
  if (!feature_edge_error.empty()) return false;
  if (analysisMode() == AnalysisMode::Canny) {
    const auto rgba = ctx->getFramebuffer();
    std::vector<unsigned char> gray(ctx->width() * ctx->height());
    for (unsigned y = 0; y < ctx->height(); ++y) {
      for (unsigned x = 0; x < ctx->width(); ++x) {
        gray[y * ctx->width() + x] = rgba[4 * ((ctx->height() - 1 - y) * ctx->width() + x)];
      }
    }
    lodepng::State state;
    state.encoder.auto_convert = false;
    state.info_raw.colortype = state.info_png.color.colortype = LCT_GREY;
    state.info_raw.bitdepth = state.info_png.color.bitdepth = 8;
    std::vector<unsigned char> encoded;
    if (lodepng::encode(encoded, gray.data(), ctx->width(), ctx->height(), state)) return false;
    output.write(reinterpret_cast<const char *>(encoded.data()), encoded.size());
    return output.good();
  }
  return save_framebuffer(this->ctx.get(), output, this->transparentBackground());
}

std::string OffscreenView::getRendererInfo() const
{
  std::ostringstream result;
  result << this->ctx->getInfo() << "\n" << gl_dump();
  return result.str();
}
