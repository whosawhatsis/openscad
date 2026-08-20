#version 120

varying vec3 vNormal;

void main(void)
{
  // Camera-space normal, negated.
  //
  // VBOBuilder::create_triangle computes n = (p1-p0) x (p1-p2), which is the
  // negation of the standard CCW normal (p1-p0) x (p2-p0) - on the unit
  // triangle (0,0,0),(1,0,0),(0,1,0) it yields (0,0,-1) where the outward
  // normal is (0,0,+1). So the vertex normals arriving here point *into* the
  // solid. Nothing else notices, because the preview lights the scene with two
  // opposing lights and GL_COLOR_MATERIAL on GL_FRONT_AND_BACK, which shades an
  // inverted normal just as well.
  //
  // A normal map is data, so it must emit outward normals: without this negation
  // a surface facing the camera encodes as (0,0,-1) instead of (0,0,+1).
  // Negating here rather than in VBOBuilder keeps the fix to this feature -
  // changing the winding convention would touch every renderer and both
  // geometry backends.
  vNormal = normalize(-(gl_NormalMatrix * gl_Normal));

  gl_Position = ftransform();
}
