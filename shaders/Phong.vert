#version 120

varying vec3 vNormal;
varying vec3 vEyePosition;
varying vec4 vColor;
attribute vec3 barycentric;
varying vec3 vBC;
// x = Blinn-Phong specular exponent, y = metalness. Defaults (64, 0) reproduce
// the fixed values this shader used before material attributes existed.
attribute vec2 material;
varying vec2 vMaterial;

void main(void)
{
  // VBOBuilder supplies inward-facing normals; analysis shaders correct the
  // same winding convention before using them as outward surface normals.
  vNormal = normalize(-(gl_NormalMatrix * gl_Normal));
  vEyePosition = vec3(gl_ModelViewMatrix * gl_Vertex);
  vColor = gl_Color;
  vBC = barycentric;
  vMaterial = material;
  gl_Position = ftransform();
}
