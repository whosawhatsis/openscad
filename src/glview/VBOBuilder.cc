#include "glview/VBOBuilder.h"

#include "geometry/smooth_normals.h"

#include <algorithm>

#include <cmath>
#include <unordered_map>
#include <cstring>
#include <cassert>
#include <array>
#include <utility>
#include <vector>
#include <memory>
#include <cstdio>

#include "geometry/linalg.h"
#include "geometry/Polygon2d.h"
#include "utils/printutils.h"
#include "utils/hash.h"  // IWYU pragma: keep

namespace {

// Since we transform each verted on the CPU, we cache already transformed vertices in the same PolySet
// to avoid redundantly transforming the same vertex value twice, while we process non-indexed PolySets.
Vector3d uniqueMultiply(std::unordered_map<Vector3d, Vector3d>& vert_mult_map, const Vector3d& in_vert,
                        const Transform3d& m)
{
  auto entry = vert_mult_map.find(in_vert);
  if (entry == vert_mult_map.end()) {
    Vector3d out_vert = m * in_vert;
    vert_mult_map.emplace(in_vert, out_vert);
    return out_vert;
  }
  return entry->second;
}

}  // namespace

void addAttributeValues(IAttributeData&)
{
}

void VertexData::getLastVertex(std::vector<GLbyte>& interleaved_buffer) const
{
  GLbyte *dst_start = interleaved_buffer.data();
  for (const auto& data : attributes_) {
    size_t size = data->sizeofAttribute();
    GLbyte *dst = dst_start;
    const GLbyte *src = data->toBytes() + data->sizeInBytes() - data->sizeofAttribute();
    std::memcpy((void *)dst, (void *)src, size);
    dst_start += size;
  }
}

void VertexData::remove(size_t count)
{
  for (const auto& data : attributes_) {
    data->remove(count);
  }
}

// Adds attributes needed for regular 3D polygon rendering:
// position, normal, color
void VBOBuilder::addSurfaceData()
{
  auto vertex_data = std::make_shared<VertexData>();
  vertex_data->addPositionData(std::make_shared<AttributeData<GLfloat, 3, GL_FLOAT>>());
  vertex_data->addNormalData(std::make_shared<AttributeData<GLfloat, 3, GL_FLOAT>>());
  vertex_data->addColorData(std::make_shared<AttributeData<GLfloat, 4, GL_FLOAT>>());
  surface_index_ = vertices_.size();
  vertices_.emplace_back(std::move(vertex_data));
}

void VBOBuilder::addEdgeData()
{
  auto vertex_data = std::make_shared<VertexData>();
  vertex_data->addPositionData(std::make_shared<AttributeData<GLfloat, 3, GL_FLOAT>>());
  vertex_data->addColorData(std::make_shared<AttributeData<GLfloat, 4, GL_FLOAT>>());
  edge_index_ = vertices_.size();
  vertices_.emplace_back(std::move(vertex_data));
}

void VBOBuilder::createVertex(const std::array<Vector3d, 3>& points,
                              const std::array<Vector3d, 3>& normals, const Color4f& color,
                              size_t active_point_index, size_t primitive_index, size_t shape_size,
                              bool outlines, bool /*mirror*/)
{
  addAttributeValues(*(data()->positionData()), points[active_point_index][0],
                     points[active_point_index][1], points[active_point_index][2]);
  if (data()->hasNormalData()) {
    addAttributeValues(*(data()->normalData()), normals[active_point_index][0],
                       normals[active_point_index][1], normals[active_point_index][2]);
  }
  if (data()->hasColorData()) {
    addAttributeValues(*(data()->colorData()), color.r(), color.g(), color.b(), color.a());
  }

  if (useElements()) {
    std::vector<GLbyte> interleaved_vertex;
    interleaved_vertex.resize(data()->stride());
    data()->getLastVertex(interleaved_vertex);
    std::pair<ElementsMap::iterator, bool> entry;
    entry.first = elements_map_.find(interleaved_vertex);
    if (entry.first == elements_map_.end()) {
      // append vertex data if this is a new element
      if (!interleaved_buffer_.empty()) {
        memcpy(interleaved_buffer_.data() + vertices_offset_, interleaved_vertex.data(),
               interleaved_vertex.size());
        data()->clear();
      }
      vertices_offset_ += interleaved_vertex.size();
      entry = elements_map_.emplace(interleaved_vertex, elements_map_.size());
    } else {
      data()->remove();
#if 0
      if (OpenSCAD::debug != "") {
        // in debug, check for bad hash matches
        size_t i = 0;
        if (interleaved_vertex.size() != entry.first->first.size()) {
          PRINTDB("vertex index = %d", entry.first->second);
          assert(false && "VBORenderer invalid vertex match size!!!");
        }
        for (const auto& b : interleaved_vertex) {
          if (b != entry.first->first[i]) {
            PRINTDB("vertex index = %d", entry.first->second);
            assert(false && "VBORenderer invalid vertex value hash match!!!");
          }
          i++;
        }
      }
#endif  // 0
    }

    // append element data
    addAttributeValues(*elementsData(), entry.first->second);
    elements_offset_ += elementsData()->sizeofAttribute();
  } else {  // !useElements()
    if (interleaved_buffer_.empty()) {
      vertices_offset_ = sizeInBytes();
    } else {
      std::vector<GLbyte> interleaved_vertex;
      interleaved_vertex.resize(data()->stride());
      data()->getLastVertex(interleaved_vertex);
      memcpy(interleaved_buffer_.data() + vertices_offset_, interleaved_vertex.data(),
             interleaved_vertex.size());
      vertices_offset_ += interleaved_vertex.size();
      data()->clear();
    }
  }
}

void VBOBuilder::createInterleavedVBOs()
{
  for (const auto& state : vertex_state_container_.states()) {
    state->setDrawOffset(this->indexOffset(state->drawOffset()));
  }

  // If the upfront size was not known, the the buffer has to be built
  size_t total_size = this->sizeInBytes();
  // If VertexArray is not empty, and initial size is zero
  if (interleaved_buffer_.empty() && total_size) {
    GL_TRACE("glBindBuffer(GL_ARRAY_BUFFER, %d)", vertex_state_container_.verticesVBO());
    GL_CHECKD(glBindBuffer(GL_ARRAY_BUFFER, vertex_state_container_.verticesVBO()));
    GL_TRACE("glBufferData(GL_ARRAY_BUFFER, %d, %p, GL_STATIC_DRAW)", total_size % (void *)nullptr);
    GL_CHECKD(glBufferData(GL_ARRAY_BUFFER, total_size, nullptr, GL_STATIC_DRAW));

    size_t dst_start = 0;
    for (const auto& vertex_data : vertices_) {
      // All attribute vectors need to be the same size to interleave
      size_t idx = 0, last_size = 0, stride = vertex_data->stride();
      for (const auto& data : vertex_data->attributes()) {
        size_t size = data->sizeofAttribute();
        const GLbyte *src = data->toBytes();
        size_t dst = dst_start;

        if (src) {
          if (idx != 0) {
            if (last_size != data->size() / data->count()) {
              PRINTDB("attribute data for vertex incorrect size at index %d = %d",
                      idx % (data->size() / data->count()));
              PRINTDB("last_size = %d", last_size);
              assert(false);
            }
          }
          last_size = data->size() / data->count();
          for (size_t i = 0; i < last_size; ++i) {
            // This path is chosen in vertex-object-renderers non-direct mode
            GL_TRACE("A glBufferSubData(GL_ARRAY_BUFFER, %p, %d, %p)", (void *)dst % size % (void *)src);
            GL_CHECKD(glBufferSubData(GL_ARRAY_BUFFER, dst, size, src));
            src += size;
            dst += stride;
          }
          dst_start += size;
        }
        idx++;
      }
      dst_start = vertex_data->sizeInBytes();
    }

    GL_TRACE0("glBindBuffer(GL_ARRAY_BUFFER, 0)");
    GL_CHECKD(glBindBuffer(GL_ARRAY_BUFFER, 0));
  } else if (!interleaved_buffer_.empty()) {
    GL_TRACE("glBindBuffer(GL_ARRAY_BUFFER, %d)", vertex_state_container_.verticesVBO());
    GL_CHECKD(glBindBuffer(GL_ARRAY_BUFFER, vertex_state_container_.verticesVBO()));
    GL_TRACE("glBufferData(GL_ARRAY_BUFFER, %d, %p, GL_STATIC_DRAW)",
             interleaved_buffer_.size() % (void *)interleaved_buffer_.data());
    GL_CHECKD(glBufferData(GL_ARRAY_BUFFER, interleaved_buffer_.size(), interleaved_buffer_.data(),
                           GL_STATIC_DRAW));
    GL_TRACE0("glBindBuffer(GL_ARRAY_BUFFER, 0)");
    GL_CHECKD(glBindBuffer(GL_ARRAY_BUFFER, 0));
  }

  PRINTDB("useElements() = %d, elements_size_ = %d", useElements() % elements_size_);
  if (useElements()) {
    GL_TRACE("glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, %d)", vertex_state_container_.elementsVBO());
    GL_CHECKD(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertex_state_container_.elementsVBO()));
    if (elements_size_ == 0) {
      GL_TRACE("glBufferData(GL_ELEMENT_ARRAY_BUFFER, %d, %p, GL_STATIC_DRAW)",
               elements_.sizeInBytes() % (void *)nullptr);
      GL_CHECKD(glBufferData(GL_ELEMENT_ARRAY_BUFFER, elements_.sizeInBytes(), nullptr, GL_STATIC_DRAW));
    }
    size_t last_size = 0;
    for (const auto& e : elements_.attributes()) {
      GL_TRACE("glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, %d, %d, %p)",
               last_size % e->sizeInBytes() % (void *)e->toBytes());
      GL_CHECKD(glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, last_size, e->sizeInBytes(), e->toBytes()));
      last_size += e->sizeInBytes();
    }
    GL_TRACE0("glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0)");
    GL_CHECKD(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0));
  }
}

void VBOBuilder::addAttributePointers(size_t start_offset)
{
  if (!this->data()) return;

  std::shared_ptr<VertexData> vertex_data = this->data();
  std::shared_ptr<VertexState> vertex_state = vertex_state_container_.states().back();

  GLsizei count = vertex_data->positionData()->count();
  GLenum type = vertex_data->positionData()->glType();
  GLsizei stride = vertex_data->stride();
  size_t offset = start_offset + vertex_data->interleavedOffset(vertex_data->positionIndex());
  // Note: Some code, like OpenCSGRenderer::createVBOPrimitive() relies on this order of
  // glBegin/glEnd functions for unlit/uncolored vertex rendering.
  vertex_state->glBegin().emplace_back([]() {
    GL_TRACE0("glEnableClientState(GL_VERTEX_ARRAY)");
    GL_CHECKD(glEnableClientState(GL_VERTEX_ARRAY));
  });
  vertex_state->glBegin().emplace_back(
    [count, type, stride, offset, vs_ptr = std::weak_ptr<VertexState>(vertex_state)]() {
      auto vs = vs_ptr.lock();
      if (vs) {
        // NOLINTBEGIN(performance-no-int-to-ptr)
        GL_TRACE("glVertexPointer(%d, %d, %d, %p)",
                 count % type % stride % (GLvoid *)(vs->drawOffset() + offset));
        GL_CHECKD(glVertexPointer(count, type, stride, (GLvoid *)(vs->drawOffset() + offset)));
        // NOLINTEND(performance-no-int-to-ptr)
      }
    });
  vertex_state->glEnd().emplace_back([]() {
    GL_TRACE0("glDisableClientState(GL_VERTEX_ARRAY)");
    GL_CHECKD(glDisableClientState(GL_VERTEX_ARRAY));
  });

  if (vertex_data->hasNormalData()) {
    type = vertex_data->normalData()->glType();
    size_t offset = start_offset + vertex_data->interleavedOffset(vertex_data->normalIndex());
    vertex_state->glBegin().emplace_back([]() {
      GL_TRACE0("glEnableClientState(GL_NORMAL_ARRAY)");
      GL_CHECKD(glEnableClientState(GL_NORMAL_ARRAY));
    });
    vertex_state->glBegin().emplace_back(
      [type, stride, offset, vs_ptr = std::weak_ptr<VertexState>(vertex_state)]() {
        auto vs = vs_ptr.lock();
        if (vs) {
          // NOLINTBEGIN(performance-no-int-to-ptr)
          GL_TRACE("glNormalPointer(%d, %d, %p)", type % stride % (GLvoid *)(vs->drawOffset() + offset));
          GL_CHECKD(glNormalPointer(type, stride, (GLvoid *)(vs->drawOffset() + offset)));
          // NOLINTEND(performance-no-int-to-ptr)
        }
      });
    vertex_state->glEnd().emplace_back([]() {
      GL_TRACE0("glDisableClientState(GL_NORMAL_ARRAY)");
      GL_CHECKD(glDisableClientState(GL_NORMAL_ARRAY));
    });
  }
  if (vertex_data->hasColorData()) {
    count = vertex_data->colorData()->count();
    type = vertex_data->colorData()->glType();
    size_t offset = start_offset + vertex_data->interleavedOffset(vertex_data->colorIndex());
    vertex_state->glBegin().emplace_back([]() {
      GL_TRACE0("glEnableClientState(GL_COLOR_ARRAY)");
      GL_CHECKD(glEnableClientState(GL_COLOR_ARRAY));
    });
    vertex_state->glBegin().emplace_back(
      [count, type, stride, offset, vs_ptr = std::weak_ptr<VertexState>(vertex_state)]() {
        auto vs = vs_ptr.lock();
        if (vs) {
          // NOLINTBEGIN(performance-no-int-to-ptr)
          GL_TRACE("glColorPointer(%d, %d, %d, %p)",
                   count % type % stride % (GLvoid *)(vs->drawOffset() + offset));
          GL_CHECKD(glColorPointer(count, type, stride, (GLvoid *)(vs->drawOffset() + offset)));
          // NOLINTEND(performance-no-int-to-ptr)
        }
      });
    vertex_state->glEnd().emplace_back([]() {
      GL_TRACE0("glDisableClientState(GL_COLOR_ARRAY)");
      GL_CHECKD(glDisableClientState(GL_COLOR_ARRAY));
    });
  }
}

// Allocates GPU memory for vertices (and elements if enabled)
// for holding the given number of vertices.
void VBOBuilder::allocateBuffers(size_t num_vertices)
{
  size_t vbo_buffer_size = num_vertices * stride();
  interleaved_buffer_.resize(vbo_buffer_size);
  GL_TRACE("glBindBuffer(GL_ARRAY_BUFFER, %d)", vertex_state_container_.verticesVBO());
  GL_CHECKD(glBindBuffer(GL_ARRAY_BUFFER, vertex_state_container_.verticesVBO()));
  GL_TRACE("glBufferData(GL_ARRAY_BUFFER, %d, %p, GL_STATIC_DRAW)", vbo_buffer_size % (void *)nullptr);
  GL_CHECKD(glBufferData(GL_ARRAY_BUFFER, vbo_buffer_size, nullptr, GL_STATIC_DRAW));
  if (Feature::ExperimentalVxORenderersIndexing.is_enabled()) {
    // Use smallest possible index data type
    if (num_vertices <= 0xff) {
      addElementsData(std::make_shared<AttributeData<GLubyte, 1, GL_UNSIGNED_BYTE>>());
    } else if (num_vertices <= 0xffff) {
      addElementsData(std::make_shared<AttributeData<GLushort, 1, GL_UNSIGNED_SHORT>>());
    } else {
      addElementsData(std::make_shared<AttributeData<GLuint, 1, GL_UNSIGNED_INT>>());
    }
    // FIXME: How do we know how much to allocate?
    // FIXME: Should we preallocate so we don't have to make a bunch of glBufferSubData() calls?
    size_t elements_size = num_vertices * elements_.stride();
    setElementsSize(elements_size);
    GL_TRACE("glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, %d)", vertex_state_container_.elementsVBO());
    GL_CHECKD(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, vertex_state_container_.elementsVBO()));
    GL_TRACE("glBufferData(GL_ELEMENT_ARRAY_BUFFER, %d, %p, GL_STATIC_DRAW)",
             elements_size % (void *)nullptr);
    GL_CHECKD(glBufferData(GL_ELEMENT_ARRAY_BUFFER, elements_size, nullptr, GL_STATIC_DRAW));
  }
}

// FIXME: This specifically adds barycentric vertex attributes, so document/rename accordingly
void VBOBuilder::addShaderData()
{
  const std::shared_ptr<VertexData> vertex_data = data();
  shader_attributes_index_ = vertex_data->attributes().size();
  vertex_data->addAttributeData(
    std::make_shared<AttributeData<GLubyte, 4, GL_UNSIGNED_BYTE>>());  // barycentric
  vertex_data->addAttributeData(
    std::make_shared<AttributeData<GLfloat, 4, GL_FLOAT>>());  // SurfaceFinish
  // (axis.xyz, anisotropy). The fifth shading value would not fit the vec4
  // above, and having paid for a second vec4 the axis rides along in the three
  // floats that would otherwise be padding.
  vertex_data->addAttributeData(
    std::make_shared<AttributeData<GLfloat, 4, GL_FLOAT>>());  // anisotropy axis
}

void VBOBuilder::add_barycentric_attribute(size_t active_point_index, size_t primitive_index,
                                           size_t shape_size, bool outlines)
{
  const std::shared_ptr<VertexData> vertex_data = data();

  // Get edge states
  std::array<GLubyte, 3> barycentric_flags;

  if (!outlines) {
    // top / bottom or 3d object
    if (shape_size == 3) {
      // true, true, true
      barycentric_flags = {0, 0, 0};
    } else if (shape_size == 4) {
      // false, true, true
      barycentric_flags = {1, 0, 0};
    } else {
      // true, false, false
      barycentric_flags = {0, 1, 1};
    }
  } else {
    // sides
    if (primitive_index == 0) {
      // true, false, true
      barycentric_flags = {0, 1, 0};
    } else {
      // true, true, false
      barycentric_flags = {0, 0, 1};
    }
  }

  barycentric_flags[active_point_index] = 1;

  addAttributeValues(*(vertex_data->attributes()[shader_attributes_index_ + BARYCENTRIC_ATTRIB]),
                     barycentric_flags[0], barycentric_flags[1], barycentric_flags[2], 0);

  // Written here, alongside barycentric, so the two shader attribute arrays get
  // exactly one entry per vertex and stay aligned.
  addAttributeValues(*(vertex_data->attributes()[shader_attributes_index_ + MATERIAL_ATTRIB]),
                     material_finish_.roughness, material_finish_.metallic, material_finish_.reflectance,
                     material_finish_.emission);

  addAttributeValues(*(vertex_data->attributes()[shader_attributes_index_ + MATERIAL_AXIS_ATTRIB]),
                     static_cast<GLfloat>(material_finish_.axis.x()),
                     static_cast<GLfloat>(material_finish_.axis.y()),
                     static_cast<GLfloat>(material_finish_.axis.z()), material_finish_.anisotropy);
}

void VBOBuilder::create_triangle(const Color4f& color, const Vector3d& p0, const Vector3d& p1,
                                 const Vector3d& p2, size_t primitive_index, size_t shape_size,
                                 bool outlines, bool enable_barycentric, bool mirror,
                                 const Vector3d *smooth_normals)
{
  const double ax = p1[0] - p0[0], bx = p1[0] - p2[0];
  const double ay = p1[1] - p0[1], by = p1[1] - p2[1];
  const double az = p1[2] - p0[2], bz = p1[2] - p2[2];
  const double nx = ay * bz - az * by;
  const double ny = az * bx - ax * bz;
  const double nz = ax * by - ay * bx;
  const double nl = sqrt(nx * nx + ny * ny + nz * nz);
  const Vector3d n = Vector3d(nx / nl, ny / nl, nz / nl);

  // Reconcile the supplied normals against the flat one rather than trusting their
  // orientation. VBOBuilder's normals face inward (Phong.vert negates them) and a
  // mirroring transform flips the winding, so a smoothed normal computed in object
  // space can arrive pointing the wrong way. Smoothing only ever averages faces
  // within the angle threshold, so a corner normal is never more than 90 degrees
  // from its own face and this cannot flip a legitimately smoothed normal.
  Vector3d n0 = n, n1 = n, n2 = n;
  if (smooth_normals) {
    const auto orient = [&n](const Vector3d& s) {
      if (s.squaredNorm() < 1e-24) return n;
      const Vector3d u = s.normalized();
      return u.dot(n) < 0.0 ? Vector3d(-u) : u;
    };
    n0 = orient(smooth_normals[0]);
    n1 = orient(smooth_normals[1]);
    n2 = orient(smooth_normals[2]);
  }

  if (!data()) return;

  if (enable_barycentric) {
    add_barycentric_attribute(0, primitive_index, shape_size, outlines);
  }
  createVertex({p0, p1, p2}, {n0, n1, n2}, color, 0, primitive_index, shape_size, outlines, mirror);

  if (!mirror) {
    if (enable_barycentric) {
      add_barycentric_attribute(1, primitive_index, shape_size, outlines);
    }
    createVertex({p0, p1, p2}, {n0, n1, n2}, color, 1, primitive_index, shape_size, outlines, mirror);
  }
  if (enable_barycentric) {
    add_barycentric_attribute(2, primitive_index, shape_size, outlines);
  }
  createVertex({p0, p1, p2}, {n0, n1, n2}, color, 2, primitive_index, shape_size, outlines, mirror);
  if (mirror) {
    if (enable_barycentric) {
      add_barycentric_attribute(1, primitive_index, shape_size, outlines);
    }
    createVertex({p0, p1, p2}, {n0, n1, n2}, color, 1, primitive_index, shape_size, outlines, mirror);
  }
}

// Creates a VBO "surface" from the PolySet.
// This will usually create a new VertexState and append it to our
// vertex states
void VBOBuilder::create_surface(const PolySet& ps, const Transform3d& m, const Color4f& default_color,
                                bool enable_barycentric, bool force_default_color, double smooth_angle)
{
  const std::shared_ptr<VertexData> vertex_data = data();

  if (!vertex_data) {
    return;
  }

  const bool mirrored = m.matrix().determinant() < 0;
  size_t triangle_count = 0;

  std::unordered_map<Vector3d, Vector3d> vert_mult_map;
  const auto last_size = verticesOffset();

  size_t elements_offset = 0;
  if (useElements()) {
    elements_offset = elementsOffset();
    elementsMap().clear();
  }

  auto has_colors = !ps.color_indices.empty();
  // The finish set by the caller is the fallback for a PolySet that carries no
  // finish channel of its own - which is every PolySet that never went through
  // a material(), and the whole F5 path, where the finish comes off the CSG leaf.
  const SurfaceFinish default_finish = material_finish_;

  // One pass over the whole PolySet, not per facet: the corner normals depend on
  // neighbouring faces, so they cannot be derived inside the loop below.
  std::vector<Vector3d> corner_normals;
  if (smooth_angle > 0.0) {
    corner_normals = computeSmoothNormals(ps, smooth_angle);
    // Normals transform by the inverse transpose, not by the matrix; under a
    // non-uniform scale the two differ and shading goes visibly wrong.
    const Matrix3d normal_matrix = m.linear().inverse().transpose();
    for (auto& normal : corner_normals) normal = normal_matrix * normal;
  }
  size_t corner_base = 0;
  const auto corner_at = [&](size_t offset) -> const Vector3d * {
    return corner_normals.empty() ? nullptr : &corner_normals[corner_base + offset];
  };

  for (size_t i = 0, n = ps.indices.size(); i < n; i++) {
    const auto& poly = ps.indices[i];
    const size_t color_index = has_colors && i < ps.color_indices.size() ? ps.color_indices[i] : -1;
    const auto& color = !force_default_color && color_index >= 0 && color_index < ps.colors.size() &&
                            ps.colors[color_index].isValid()
                          ? ps.colors[color_index]
                          : default_color;
    material_finish_ = color_index < ps.finishes.size() ? ps.finishes[color_index] : default_finish;
    if (poly.size() == 3) {
      const Vector3d p0 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(0)], m);
      const Vector3d p1 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(1)], m);
      const Vector3d p2 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(2)], m);

      const Vector3d tri_normals[3] = {corner_at(0) ? *corner_at(0) : Vector3d::Zero(),
                                       corner_at(1) ? *corner_at(1) : Vector3d::Zero(),
                                       corner_at(2) ? *corner_at(2) : Vector3d::Zero()};
      create_triangle(color, p0, p1, p2, 0, poly.size(), false, enable_barycentric, mirrored,
                      corner_normals.empty() ? nullptr : tri_normals);
      triangle_count++;
    } else if (poly.size() == 4) {
      const Vector3d p0 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(0)], m);
      const Vector3d p1 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(1)], m);
      const Vector3d p2 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(2)], m);
      const Vector3d p3 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(3)], m);

      const Vector3d *c0 = corner_at(0), *c1 = corner_at(1), *c2 = corner_at(2), *c3 = corner_at(3);
      const bool smooth = !corner_normals.empty();
      const Vector3d quad_a[3] = {smooth ? *c0 : Vector3d::Zero(), smooth ? *c1 : Vector3d::Zero(),
                                  smooth ? *c3 : Vector3d::Zero()};
      const Vector3d quad_b[3] = {smooth ? *c2 : Vector3d::Zero(), smooth ? *c3 : Vector3d::Zero(),
                                  smooth ? *c1 : Vector3d::Zero()};
      create_triangle(color, p0, p1, p3, 0, poly.size(), false, enable_barycentric, mirrored,
                      smooth ? quad_a : nullptr);
      create_triangle(color, p2, p3, p1, 1, poly.size(), false, enable_barycentric, mirrored,
                      smooth ? quad_b : nullptr);
      triangle_count += 2;
    } else {
      Vector3d center = Vector3d::Zero();
      for (const auto& idx : poly) {
        center += ps.vertices[idx];
      }
      center /= poly.size();
      for (size_t i = 1; i <= poly.size(); i++) {
        const Vector3d p0 = uniqueMultiply(vert_mult_map, center, m);
        const Vector3d p1 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(i % poly.size())], m);
        const Vector3d p2 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(i - 1)], m);

        // The centre vertex is synthesized and has no corner of its own, so it takes
        // the mean of the face's corners - which is the face normal for a planar face.
        const bool smooth = !corner_normals.empty();
        Vector3d centre_normal = Vector3d::Zero();
        if (smooth) {
          for (size_t c = 0; c < poly.size(); ++c) centre_normal += *corner_at(c);
          if (centre_normal.squaredNorm() > 1e-24) centre_normal.normalize();
        }
        const Vector3d fan_normals[3] = {centre_normal, smooth ? *corner_at(i - 1) : Vector3d::Zero(),
                                         smooth ? *corner_at(i % poly.size()) : Vector3d::Zero()};
        create_triangle(color, p0, p2, p1, i - 1, poly.size(), false, enable_barycentric, mirrored,
                        smooth ? fan_normals : nullptr);
        triangle_count++;
      }
    }
    corner_base += poly.size();
  }

  GLenum elements_type = 0;
  if (useElements()) elements_type = elementsData()->glType();
  std::shared_ptr<VertexState> vertex_state =
    createVertexState(GL_TRIANGLES, triangle_count * 3, elements_type, writeIndex(), elements_offset);
  vertex_state_container_.states().emplace_back(std::move(vertex_state));
  addAttributePointers(last_size);
  // OpenCSG copies the first two callbacks as the position-only primitive draw.
  // Append metadata after those callbacks so its stencil/depth pass stays intact.
  vertex_state_container_.states().back()->glBegin().emplace_back(
    [angle = ps.smoothAngle()]() { glMultiTexCoord1f(GL_TEXTURE1, static_cast<float>(angle)); });
}

void VBOBuilder::create_edges(const Polygon2d& polygon, const Transform3d& m, const Color4f& color)
{
  const std::shared_ptr<VertexData> vertex_data = data();

  if (!vertex_data) return;

  auto& vertex_states = states();
  std::unordered_map<Vector3d, Vector3d> vert_mult_map;

  // Render only outlines
  for (const Outline2d& o : polygon.outlines()) {
    const auto last_size = verticesOffset();
    size_t elements_offset = 0;
    if (useElements()) {
      elements_offset = elementsOffset();
      elementsMap().clear();
    }
    for (const Vector2d& v : o.vertices) {
      const Vector3d p0 = uniqueMultiply(vert_mult_map, Vector3d(v[0], v[1], 0.0), m);
      createVertex({p0}, {}, color, 0, 0, o.vertices.size(), true, false);
    }

    GLenum elements_type = 0;
    if (useElements()) elements_type = elementsData()->glType();
    std::shared_ptr<VertexState> line_loop =
      createVertexState(GL_LINE_LOOP, o.vertices.size(), elements_type, writeIndex(), elements_offset);
    vertex_states.emplace_back(std::move(line_loop));
    addAttributePointers(last_size);
  }
}

void VBOBuilder::create_polygons(const PolySet& ps, const Transform3d& m, const Color4f& color)
{
  assert(ps.getDimension() == 2);
  const std::shared_ptr<VertexData> vertex_data = data();

  if (!vertex_data) return;

  auto& vertex_states = states();
  std::unordered_map<Vector3d, Vector3d> vert_mult_map;

  PRINTD("create_polygons 2D");
  const bool mirrored = m.matrix().determinant() < 0;
  size_t triangle_count = 0;
  const auto last_size = verticesOffset();
  size_t elements_offset = 0;
  if (useElements()) {
    elements_offset = elementsOffset();
    elementsMap().clear();
  }

  for (const auto& poly : ps.indices) {
    if (poly.size() == 3) {
      const Vector3d p0 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(0)], m);
      const Vector3d p1 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(1)], m);
      const Vector3d p2 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(2)], m);

      create_triangle(color, p0, p1, p2, 0, poly.size(), false, false, mirrored);
      triangle_count++;
    } else if (poly.size() == 4) {
      const Vector3d p0 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(0)], m);
      const Vector3d p1 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(1)], m);
      const Vector3d p2 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(2)], m);
      const Vector3d p3 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(3)], m);

      create_triangle(color, p0, p1, p3, 0, poly.size(), false, false, mirrored);
      create_triangle(color, p2, p3, p1, 1, poly.size(), false, false, mirrored);
      triangle_count += 2;
    } else {
      Vector3d center = Vector3d::Zero();
      for (const auto& point : poly) {
        center[0] += ps.vertices[point][0];
        center[1] += ps.vertices[point][1];
      }
      center[0] /= poly.size();
      center[1] /= poly.size();

      for (size_t i = 1; i <= poly.size(); i++) {
        const Vector3d p0 = uniqueMultiply(vert_mult_map, center, m);
        const Vector3d p1 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(i % poly.size())], m);
        const Vector3d p2 = uniqueMultiply(vert_mult_map, ps.vertices[poly.at(i - 1)], m);

        create_triangle(color, p0, p2, p1, i - 1, poly.size(), false, false, mirrored);
        triangle_count++;
      }
    }
  }

  GLenum elements_type = 0;
  if (useElements()) elements_type = elementsData()->glType();
  std::shared_ptr<VertexState> vs =
    createVertexState(GL_TRIANGLES, triangle_count * 3, elements_type, writeIndex(), elements_offset);
  vertex_states.emplace_back(std::move(vs));
  addAttributePointers(last_size);
}
