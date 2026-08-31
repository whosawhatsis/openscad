#pragma once

#include <algorithm>
#include <map>
#include <string>

#include <cassert>
#include <cstddef>
#include <list>
#include <memory>
#include <string>
#include <utility>

#include "geometry/linalg.h"

class AbstractNode;
class CGALNefGeometry;
class GeometryList;
class GeometryVisitor;
class Polygon2d;
class PolySet;
#ifdef ENABLE_MANIFOLD
class ManifoldGeometry;
#endif

class Geometry
{
public:
  using GeometryItem = std::pair<std::shared_ptr<const AbstractNode>, std::shared_ptr<const Geometry>>;
  using Geometries = std::list<GeometryItem>;

  Geometry() = default;
  Geometry(const Geometry&) = default;
  Geometry& operator=(const Geometry&) = default;
  Geometry(Geometry&&) = default;
  Geometry& operator=(Geometry&&) = default;
  virtual ~Geometry() = default;

  [[nodiscard]] virtual size_t memsize() const = 0;
  [[nodiscard]] virtual BoundingBox getBoundingBox() const = 0;
  [[nodiscard]] virtual std::string dump() const = 0;
  [[nodiscard]] virtual unsigned int getDimension() const = 0;
  [[nodiscard]] virtual bool isEmpty() const = 0;
  [[nodiscard]] virtual std::unique_ptr<Geometry> copy() const = 0;
  [[nodiscard]] virtual size_t numFacets() const = 0;
  [[nodiscard]] unsigned int getConvexity() const { return convexity; }
  void setConvexity(int c) { this->convexity = c; }
  virtual void setColor(const Color4f& c) {}
  void setMaterialName(std::string name) { materialName_ = std::move(name); }
  // Roughness reaches the shader as the user wrote it: the microfacet BRDF is
  // parameterized on roughness directly, so there is nothing to convert and
  // exporters and renderer read the same number. Zero means "not set", which is
  // what the shader's default branch tests for.
  void setRoughness(float r)
  {
    roughness_ = r;
    hasRoughness_ = true;
  }
  [[nodiscard]] bool hasRoughness() const { return hasRoughness_; }
  [[nodiscard]] float roughness() const { return roughness_; }
  //! Roughness as the shader wants it: a negative value means the model set none, so
  //! that an explicit roughness = 0 (a mirror) stays distinguishable from "not set".
  //! Zero cannot be the sentinel, because zero is a meaningful roughness.
  [[nodiscard]] float shaderRoughness() const { return hasRoughness_ ? roughness_ : -1.0f; }
  // Extra POV-Ray finish parameters, carried as a map because only the POV
  // exporter reads them so far. TODO fold these into the metadata channel that
  // materialName uses on the process-isolation branch, so they survive the
  // compute worker transport too.
  void setFinishParams(std::map<std::string, double> params) { finishParams_ = std::move(params); }
  [[nodiscard]] const std::map<std::string, double>& finishParams() const { return finishParams_; }
  void setMetallic(float m) { metallic_ = m; }
  [[nodiscard]] float metallic() const { return metallic_; }
  [[nodiscard]] const std::string& materialName() const { return materialName_; }
  void setBodyBoundary(bool boundary = true) { bodyBoundary_ = boundary; }
  [[nodiscard]] bool isBodyBoundary() const { return bodyBoundary_; }
  void setBodyColor(const Color4f& color)
  {
    bodyColor_ = color;
    hasBodyColor_ = true;
  }
  [[nodiscard]] bool hasBodyColor() const { return hasBodyColor_; }
  [[nodiscard]] const Color4f& bodyColor() const { return bodyColor_; }
  // Body identity only. This deliberately does not repaint the geometry with
  // the body color: a geometry that came out of a boolean or a render() node
  // already carries the per-face colors of the operands it was built from, and
  // flooding it with one color is what color()/render() color preservation
  // exists to prevent. color() itself paints, in the ColorNode visitor.
  void copyBodyAttributes(const Geometry& other)
  {
    materialName_ = other.materialName_;
    roughness_ = other.roughness_;
    hasRoughness_ = other.hasRoughness_;
    metallic_ = other.metallic_;
    finishParams_ = other.finishParams_;
    bodyBoundary_ = other.bodyBoundary_;
    bodyColor_ = other.bodyColor_;
    hasBodyColor_ = other.hasBodyColor_;
  }
  // A body-combining operation consumes its operands and produces one body,
  // which takes the first operand's color as well as its name.
  void takeBodyAttributesFrom(const Geometry& other)
  {
    copyBodyAttributes(other);
    if (hasBodyColor_) setColor(bodyColor_);
  }
  // The smoothing tolerance this geometry was generated for, in degrees: facets meeting
  // at less than this are meant to read as one curved surface rather than as distinct
  // planes. Primitives record twice the $fa they were tessellated at. The default is
  // twice the default $fa of 12, so geometry that records nothing behaves as before.
  //
  // Read by the viewport's smooth shading and by the .blend exporter's sharp-edge
  // marking, which must use the same number or a model shades one way in OpenSCAD and
  // another in Blender.
  void setSmoothAngle(double degrees) { smooth_angle_ = degrees; }
  [[nodiscard]] double smoothAngle() const { return smooth_angle_; }

  virtual void transform(const Transform3d& /*mat*/) { assert(!"transform not implemented!"); }
  virtual void resize(const Vector3d& /*newsize*/, const Eigen::Matrix<bool, 3, 1>& /*autosize*/)
  {
    assert(!"resize not implemented!");
  }

  virtual void accept(GeometryVisitor& visitor) const = 0;

protected:
  double smooth_angle_{24.0};
  int convexity{1};
  std::string materialName_;
  float roughness_{0.0f};
  bool hasRoughness_{false};
  float metallic_{0.0f};
  std::map<std::string, double> finishParams_;
  bool bodyBoundary_{false};
  Color4f bodyColor_;
  bool hasBodyColor_{false};
};

/**
 * A Base class for simple visitors to process different Geometry subclasses uniformly
 */
class GeometryVisitor
{
public:
  virtual void visit(const GeometryList& node) = 0;
  virtual void visit(const PolySet& node) = 0;
  virtual void visit(const Polygon2d& node) = 0;
#ifdef ENABLE_CGAL
  virtual void visit(const CGALNefGeometry& node) = 0;
#endif
#ifdef ENABLE_MANIFOLD
  virtual void visit(const ManifoldGeometry& node) = 0;
#endif
  virtual ~GeometryVisitor() = default;
};

#define VISITABLE_GEOMETRY()                           \
  void accept(GeometryVisitor& visitor) const override \
  {                                                    \
    visitor.visit(*this);                              \
  }

class GeometryList : public Geometry
{
public:
  VISITABLE_GEOMETRY();
  Geometries children;

  GeometryList();
  GeometryList(Geometry::Geometries geometries);

  [[nodiscard]] size_t memsize() const override;
  [[nodiscard]] BoundingBox getBoundingBox() const override;
  [[nodiscard]] std::string dump() const override;
  [[nodiscard]] unsigned int getDimension() const override;
  [[nodiscard]] bool isEmpty() const override;
  [[nodiscard]] std::unique_ptr<Geometry> copy() const override;
  [[nodiscard]] size_t numFacets() const override
  {
    assert(false && "not implemented");
    return 0;
  }
  void transform(const Transform3d& mat) override;

  [[nodiscard]] const Geometries& getChildren() const { return this->children; }

  [[nodiscard]] Geometries flatten() const;
};
