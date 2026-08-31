#include "geometry/smooth_normals.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
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

  // Faces incident to each vertex, as a flat CSR-style pair of arrays indexed by vertex
  // id. This was a std::map keyed by vertex, which cost 221ms on a $fn=200 sphere -
  // nearly all of it in the map, since ids are already a dense 0..n-1 range and the
  // lookup is the inner loop of the whole routine.
  std::vector<uint32_t> faceCountPerVertex(ps.vertices.size() + 1, 0);
  for (const auto& face : ps.indices) {
    for (const int v : face) {
      if (v >= 0 && size_t(v) < ps.vertices.size()) ++faceCountPerVertex[size_t(v) + 1];
    }
  }
  for (size_t v = 0; v < ps.vertices.size(); ++v) faceCountPerVertex[v + 1] += faceCountPerVertex[v];
  std::vector<uint32_t> vertexFaces(faceCountPerVertex.back());
  {
    std::vector<uint32_t> cursor(faceCountPerVertex.begin(), faceCountPerVertex.end() - 1);
    for (size_t f = 0; f < faceCount; ++f) {
      for (const int v : ps.indices[f]) {
        if (v >= 0 && size_t(v) < ps.vertices.size()) vertexFaces[cursor[size_t(v)]++] = uint32_t(f);
      }
    }
  }

  const double cosThreshold = std::cos(smoothAngleDegrees * (M_PI / 180.0));

  std::vector<Vector3d> cornerNormals;
  cornerNormals.reserve(vertexFaces.size());
  for (size_t f = 0; f < faceCount; ++f) {
    for (const int v : ps.indices[f]) {
      Vector3d sum = faceNormals[f];
      if (v >= 0 && size_t(v) < ps.vertices.size()) {
        for (uint32_t i = faceCountPerVertex[size_t(v)]; i < faceCountPerVertex[size_t(v) + 1]; ++i) {
          const uint32_t other = vertexFaces[i];
          if (other == f) continue;
          // The exporter's rule is per edge; this is per vertex fan, which is the
          // approximation a shading normal needs anyway - a corner normal is shared by
          // every face meeting there, not by an edge. The difference only shows for two
          // faces that touch at a single point without sharing an edge, where this
          // averages them and the edge rule would not. No boundary case in the tests
          // distinguishes the two, and the cube depends on the angle test, not on
          // connectivity.
          if (faceNormals[f].dot(faceNormals[other]) >= cosThreshold) sum += faceNormals[other];
        }
      }
      const double length = sum.norm();
      cornerNormals.push_back(length > 1e-12 ? Vector3d(sum / length) : faceNormals[f]);
    }
  }
  return cornerNormals;
}
