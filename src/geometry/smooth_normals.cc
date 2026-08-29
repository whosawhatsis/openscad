#include "geometry/smooth_normals.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <utility>
#include <vector>

#include "geometry/PolySet.h"

Vector3d newellNormal(const PolySet& ps, const IndexedFace& face)
{
  // Newell's method rather than a cross product of the first three points: it is
  // stable for non-planar polygons, which tessellation routinely produces, and it
  // is what the .blend exporter uses. Two different normals for one face would
  // defeat the point of sharing the rule.
  Vector3d normal(0, 0, 0);
  const size_t n = face.size();
  for (size_t i = 0; i < n; ++i) {
    const int a = face[i];
    const int b = face[(i + 1) % n];
    if (a < 0 || size_t(a) >= ps.vertices.size() || b < 0 || size_t(b) >= ps.vertices.size()) continue;
    const Vector3d& p1 = ps.vertices[a];
    const Vector3d& p2 = ps.vertices[b];
    normal.x() += (p1.y() - p2.y()) * (p1.z() + p2.z());
    normal.y() += (p1.z() - p2.z()) * (p1.x() + p2.x());
    normal.z() += (p1.x() - p2.x()) * (p1.y() + p2.y());
  }
  const double length = normal.norm();
  return length > 1e-12 ? Vector3d(normal / length) : Vector3d(0, 0, 0);
}

std::vector<Vector3d> computeSmoothNormals(const PolySet& ps, double smoothAngleDegrees)
{
  const size_t faceCount = ps.indices.size();
  std::vector<Vector3d> faceNormals;
  faceNormals.reserve(faceCount);
  for (const auto& face : ps.indices) faceNormals.push_back(newellNormal(ps, face));

  // Which faces meet at each undirected edge. An edge without exactly two of them is
  // a boundary or non-manifold edge and stays sharp, matching the exporter.
  std::map<std::pair<int, int>, std::vector<size_t>> edgeFaces;
  for (size_t f = 0; f < faceCount; ++f) {
    const auto& face = ps.indices[f];
    for (size_t i = 0; i < face.size(); ++i) {
      const int a = face[i];
      const int b = face[(i + 1) % face.size()];
      edgeFaces[{std::min(a, b), std::max(a, b)}].push_back(f);
    }
  }

  // Faces sharing a vertex, so a corner can find its smoothing neighbours without
  // walking the whole mesh.
  std::map<int, std::vector<size_t>> vertexFaces;
  for (size_t f = 0; f < faceCount; ++f) {
    for (const int v : ps.indices[f]) vertexFaces[v].push_back(f);
  }

  const double cosThreshold = std::cos(smoothAngleDegrees * (M_PI / 180.0));

  std::vector<Vector3d> cornerNormals;
  for (size_t f = 0; f < faceCount; ++f) {
    const auto& face = ps.indices[f];
    for (const int v : face) {
      Vector3d sum = faceNormals[f];
      const auto neighbours = vertexFaces.find(v);
      if (neighbours != vertexFaces.end()) {
        for (const size_t other : neighbours->second) {
          if (other == f) continue;
          // Averaged only when this pair could shade smoothly: the same angle test the
          // exporter applies to the edge between them. Faces that meet only at a point
          // fail it too, which is what stops a cube's corner rounding.
          if (faceNormals[f].dot(faceNormals[other]) >= cosThreshold) sum += faceNormals[other];
        }
      }
      const double length = sum.norm();
      cornerNormals.push_back(length > 1e-12 ? Vector3d(sum / length) : faceNormals[f]);
    }
  }
  return cornerNormals;
}
