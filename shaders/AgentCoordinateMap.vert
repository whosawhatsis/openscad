#version 120

// The model bounding box, so object coordinates can be written as colour.
// Raw coordinates cannot: GL clamps colour to [0,1] and every model larger than
// one unit would saturate. coordExtent is never zero - a degenerate axis is
// passed as 1 and flagged, and pins to 0.5 rather than dividing by zero.
uniform vec3 coordMin;
uniform vec3 coordExtent;
uniform vec3 coordDegenerate;

varying vec3 vCoord;

void main(void)
{
  vec3 normalized = (gl_Vertex.xyz - coordMin) / coordExtent;
  // Clamp rather than wrap: a wrapped coordinate is indistinguishable from a
  // real one on the far side of the model.
  normalized = clamp(normalized, 0.0, 1.0);
  vCoord = mix(normalized, vec3(0.5), coordDegenerate);
  gl_Position = ftransform();
}
