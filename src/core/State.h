#pragma once

#include <string>

#include <cstring>
#include <memory>
#include <utility>

#include "core/node.h"
#include "geometry/linalg.h"

class State
{
public:
  State(std::shared_ptr<const AbstractNode> parent) : parentnode(std::move(parent))
  {
    this->matrix_ = Transform3d::Identity();
  }

  void setPrefix(bool on) { FLAG(this->flags, PREFIX, on); }
  void setPostfix(bool on) { FLAG(this->flags, POSTFIX, on); }
  void setHighlight(bool on) { FLAG(this->flags, HIGHLIGHT, on); }
  void setBackground(bool on) { FLAG(this->flags, BACKGROUND, on); }
  void setNumChildren(unsigned int numc) { this->numchildren = numc; }
  void setParent(const std::shared_ptr<const AbstractNode>& parent) { this->parentnode = parent; }
  void setMatrix(const Transform3d& m) { this->matrix_ = m; }
  void setColor(const Color4f& c) { this->color_ = c; }
  void setMaterialName(std::string name) { this->materialName_ = std::move(name); }
  void setRoughness(float r)
  {
    this->roughness_ = r;
    this->hasRoughness_ = true;
  }
  void setMetallic(float m) { this->metallic_ = m; }
  void setPreferNef(bool on) { FLAG(this->flags, PREFERNEF, on); }
  [[nodiscard]] bool preferNef() const { return this->flags & PREFERNEF; }

  [[nodiscard]] bool isPrefix() const { return this->flags & PREFIX; }
  [[nodiscard]] bool isPostfix() const { return this->flags & POSTFIX; }
  [[nodiscard]] bool isHighlight() const { return this->flags & HIGHLIGHT; }
  [[nodiscard]] bool isBackground() const { return this->flags & BACKGROUND; }
  [[nodiscard]] unsigned int numChildren() const { return this->numchildren; }
  [[nodiscard]] std::shared_ptr<const AbstractNode> parent() const { return this->parentnode; }
  [[nodiscard]] const Transform3d& matrix() const { return this->matrix_; }
  [[nodiscard]] const Color4f& color() const { return this->color_; }
  [[nodiscard]] const std::string& materialName() const { return this->materialName_; }
  [[nodiscard]] bool hasRoughness() const { return this->hasRoughness_; }
  [[nodiscard]] float roughness() const { return this->roughness_; }
  [[nodiscard]] float metallic() const { return this->metallic_; }

private:
  enum StateFlags : unsigned int {
    NONE = 0x00u,
    PREFIX = 0x01u,
    POSTFIX = 0x02u,
    PREFERNEF = 0x04u,
    HIGHLIGHT = 0x08u,
    BACKGROUND = 0x10u
  };

  constexpr void FLAG(unsigned int& var, StateFlags flag, bool on)
  {
    if (on) {
      var |= flag;
    } else {
      var &= ~flag;
    }
  }

  unsigned int flags{NONE};
  std::shared_ptr<const AbstractNode> parentnode;
  unsigned int numchildren{0};

  // Transformation matrix and color. FIXME: Generalize such state variables?
  Transform3d matrix_;
  Color4f color_;
  // Only ever read by the renderers, to look up a display-time default color.
  // It must not reach any exporter: a Preferences color is color scheme, not model.
  std::string materialName_;
  float roughness_{0.0f};
  bool hasRoughness_{false};
  float metallic_{0.0f};
};
