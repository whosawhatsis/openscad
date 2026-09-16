#include "glview/FeatureEdges.h"
#include "glview/GLView.h"
#include "glview/ColorMap.h"
#include "glview/Renderer.h"
#include "glview/ShaderUtils.h"
#include "glview/fbo.h"
#include "glview/preview/OpenCSGRenderer.h"

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

struct Program {
  ShaderUtils::ShaderResource resource;
  Program(const char *vertex, const char *fragment)
    : resource(ShaderUtils::compileShaderProgram(ShaderUtils::loadShaderSource(vertex),
                                                 ShaderUtils::loadShaderSource(fragment)))
  {
    GLint linked = GL_FALSE;
    glGetProgramiv(resource.shader_program, GL_LINK_STATUS, &linked);
    if (!linked) throw std::runtime_error("Feature-edge shader failed to link");
  }
  ~Program()
  {
    glDeleteProgram(resource.shader_program);
    glDeleteShader(resource.vertex_shader);
    glDeleteShader(resource.fragment_shader);
  }
  void use() const { glUseProgram(resource.shader_program); }
  GLint uniform(const char *name) const { return glGetUniformLocation(resource.shader_program, name); }
};

struct Target {
  std::unique_ptr<FBO> fbo;
  GLuint textures[2]{};
  Target(int width, int height) : fbo(createFBO(width, height))
  {
    if (!fbo || !fbo->isComplete()) throw std::runtime_error("Cannot allocate feature-edge framebuffer");
    glGenTextures(2, textures);
    for (int i = 0; i < 2; ++i) {
      glBindTexture(GL_TEXTURE_2D, textures[i]);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0 + i, GL_TEXTURE_2D, textures[i], 0);
    }
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      throw std::runtime_error("Feature-edge surface framebuffer is incomplete");
    }
  }
  ~Target() { glDeleteTextures(2, textures); }
  void bind(bool clear = false)
  {
    fbo->bind();
    const GLenum attachments[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, attachments);
    if (clear) {
      glDepthMask(GL_TRUE);
      glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      glClearColor(0, 0, 0, 1);  // alpha=1 in the surface texture is background depth
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
  }
  void sample(const Program& program, int unit, const char *normal, const char *color) const
  {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glUniform1i(program.uniform(normal), unit);
    glActiveTexture(GL_TEXTURE0 + unit + 1);
    glBindTexture(GL_TEXTURE_2D, textures[1]);
    glUniform1i(program.uniform(color), unit + 1);
    glActiveTexture(GL_TEXTURE0);
  }
};

void quad()
{
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_LIGHTING);
  glDisable(GL_BLEND);
  glBegin(GL_QUADS);
  glVertex2f(-1, -1);
  glVertex2f(1, -1);
  glVertex2f(1, 1);
  glVertex2f(-1, 1);
  glEnd();
  glUseProgram(0);
}

}  // namespace

class FeatureEdgeResources
{
public:
  int width, height;
  Program surface{"AgentCanny.vert", "AgentCanny.frag"};
  Program merge{"FeatureEdgeScreen.vert", "FeatureEdgeMerge.frag"};
  Program detect{"FeatureEdgeScreen.vert", "FeatureEdgeDetect.frag"};
  Program expand{"FeatureEdgeScreen.vert", "FeatureEdgeExpand.frag"};
  Target product;
  std::array<std::unique_ptr<Target>, 2> layers, scratch;

  FeatureEdgeResources(int width, int height) : width(width), height(height), product(width, height)
  {
    for (int i = 0; i < 2; ++i) {
      layers[i] = std::make_unique<Target>(width, height);
      scratch[i] = std::make_unique<Target>(width, height);
    }
  }
};

void drawFeatureEdges(GLView& view, bool colorEdges, bool overlay)
{
  GLint framebuffer, viewport[4], currentProgram, activeTexture;
  glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
  glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
  glPushAttrib(GL_ALL_ATTRIB_BITS);
  glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
  glActiveTexture(GL_TEXTURE0);
  glDisable(GL_BLEND);
  glDisable(GL_FOG);
  glDisable(GL_DITHER);
  glDisable(GL_MULTISAMPLE);
  glDisable(GL_ALPHA_TEST);

  // Restore the caller even if shader or framebuffer allocation fails.
  struct Restore {
    GLint framebuffer, program, texture;
    ~Restore()
    {
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
      glPopClientAttrib();
      glPopAttrib();
      glUseProgram(program);
      glActiveTexture(texture);
    }
  } restore{framebuffer, currentProgram, activeTexture};

  const int width = viewport[2], height = viewport[3];
  auto& resources = view.feature_edge_resources;
  if (!resources || resources->width != width || resources->height != height) {
    resources = std::make_shared<FeatureEdgeResources>(width, height);
  }
  auto& surface = resources->surface;
  auto& merge = resources->merge;
  auto& detect = resources->detect;
  auto& expand = resources->expand;
  auto& product = resources->product;
  auto& layers = resources->layers;
  for (int layer = 0; layer < 2; ++layer) {
    auto& accumulated = layers[layer];
    auto& scratch = resources->scratch[layer];
    accumulated->bind(true);
    ShaderUtils::ShaderInfo shader{.resource = surface.resource,
                                   .type = ShaderUtils::ShaderType::AGENT_RENDERING,
                                   .uniforms = {},
                                   .attributes = {},
                                   .captureSurface = true};
    auto begin = [&]() {
      product.bind(true);
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LEQUAL);
      glDisable(GL_BLEND);
      surface.use();
      glUniform1i(surface.uniform("layer"), layer);
      glUseProgram(0);
    };
    auto end = [&]() {
      scratch->bind();
      merge.use();
      product.sample(merge, 0, "surface", "color");
      accumulated->sample(merge, 2, "previousSurface", "previousColor");
      glUniform2f(merge.uniform("size"), width, height);
      quad();
      std::swap(accumulated, scratch);
    };
    if (dynamic_cast<OpenCSGRenderer *>(view.renderer.get())) {
      shader.beginProduct = begin;
      shader.endProduct = end;
      view.renderer->prepare(&shader);
      view.renderer->draw(false, &shader);
    } else {
      begin();
      view.renderer->prepare(&shader);
      view.renderer->draw(false, &shader);
      end();
    }
  }

  product.bind();
  detect.use();
  layers[0]->sample(detect, 0, "opaqueSurface", "opaqueColor");
  layers[1]->sample(detect, 2, "transparentSurface", "transparentColor");
  glUniform2f(detect.uniform("size"), width, height);
  glUniform1i(detect.uniform("colorEdges"), colorEdges);
  glUniform1i(detect.uniform("perspective"), view.cam.projection == Camera::ProjectionType::PERSPECTIVE);
  glUniform2f(detect.uniform("clip"), view.clipNear, view.clipFar);
  quad();

  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  // The original draw-buffer selection belongs to the caller's framebuffer.
  glDrawBuffer(framebuffer ? GL_COLOR_ATTACHMENT0 : GL_BACK);
  expand.use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, product.textures[0]);
  glUniform1i(expand.uniform("edges"), 0);
  glUniform2f(expand.uniform("size"), width, height);
  glUniform1f(expand.uniform("width"), std::min(view.edge_width, 2.0 * (width + height)));
  glUniform1i(expand.uniform("overlay"), overlay);
  const auto edgeColor = ColorMap::getColor(*view.colorscheme, RenderColor::CGAL_EDGE_FRONT_COLOR);
  glUniform4f(expand.uniform("edgeColor"), edgeColor.r(), edgeColor.g(), edgeColor.b(), 1.0f);
  quad();
}
