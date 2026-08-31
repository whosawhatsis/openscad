#version 120

varying vec3 vNormal;
varying vec3 vEyePosition;
varying vec4 vColor;
attribute vec3 barycentric;
varying vec3 vBC;
// SurfaceFinish: x = roughness, y = metallic, z = dielectric reflectance (F0,
// folded from ior and specular on the CPU), w = emission. A negative roughness
// means the model set none, and the fragment shader substitutes the value that
// reproduces the look this shader had before material attributes existed.
attribute vec4 material;
varying vec4 vMaterial;

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
