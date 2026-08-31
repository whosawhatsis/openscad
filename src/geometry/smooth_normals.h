#pragma once

#include <vector>

#include "geometry/linalg.h"
#include "geometry/GeometryUtils.h"

class PolySet;

/*!
   Per-corner shading normals, smoothed across edges whose dihedral angle is small.

   This is the viewport half of the rule the .blend exporter already applies
   (src/io/export_blend.cc): face normals by Newell's method, an edge is sharp when
   dot(n1, n2) < cos(smoothAngle), and an edge without exactly two incident faces is
   always sharp. Keeping one rule means a model shades the same here and in Blender.

   Normals are per *corner*, not per vertex position: a cube's corner is shared by three
   mutually sharp faces and must keep three distinct normals, or smoothing rounds it.

   Returns one normal per corner, flattened in face order - face f's corner i is at
   offset (sum of earlier face sizes) + i. Faces with fewer than three usable corners
   still contribute their entry so the indexing stays aligned.
 */
std::vector<Vector3d> computeSmoothNormals(const PolySet& ps, double smoothAngleDegrees = 24.0);

//! Newell's method, exposed for tests and reuse. Zero for a degenerate face.
Vector3d newellNormal(const PolySet& ps, const IndexedFace& face);
