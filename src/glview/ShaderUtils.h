#pragma once

#include <unordered_map>
#include <string>
#include <functional>
#include "glview/system-gl.h"

namespace ShaderUtils {

enum class ShaderType {
  NONE,
  EDGE_RENDERING,
  SELECT_RENDERING,
  AGENT_RENDERING,
};

struct ShaderResource {
  GLuint shader_program;
  GLuint vertex_shader;
  GLuint fragment_shader;
};

// Shader attribute identifiers
struct ShaderInfo {
  ShaderResource resource;
  ShaderType type;
  std::unordered_map<std::string, int> uniforms;
  std::unordered_map<std::string, int> attributes;
  bool captureSurface = false;
  // Surface-data consumers can composite each complete CSG product independently.
  std::function<void()> beginProduct;
  std::function<void()> endProduct;
};

std::string loadShaderSource(const std::string& name);
ShaderResource compileShaderProgram(const std::string& vs_str, const std::string& fs_str);

}  // namespace ShaderUtils
