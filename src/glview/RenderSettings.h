#pragma once

#include <optional>
#include <string>

enum class RenderBackend3D {
  UnknownBackend,
  CGALBackend,
  ManifoldBackend,
  OpenCASCADEBackend,
};

inline constexpr RenderBackend3D DEFAULT_RENDERING_BACKEND_3D = RenderBackend3D::ManifoldBackend;

std::string renderBackend3DToString(RenderBackend3D backend);
std::optional<RenderBackend3D> renderBackend3DFromString(std::string backend);
constexpr bool useBackendPreview(RenderBackend3D backend)
{
  return backend == RenderBackend3D::OpenCASCADEBackend;
}

class RenderSettings
{
public:
  static RenderSettings *inst(bool erase = false);

  RenderBackend3D backend3D;
  unsigned int openCSGTermLimit;
  double far_gl_clip_limit;
  std::string colorscheme;

private:
  RenderSettings();
};
