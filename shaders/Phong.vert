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
// Anisotropy: xyz is the surface direction the smear runs along - the layer
// direction of a print - and w is the signed strength in [-1,1]. Rotated into
// eye space here because that is where the lighting is evaluated.
attribute vec4 material_axis;
varying vec4 vMaterialAxis;

void main(void)
{
  // VBOBuilder supplies inward-facing normals; analysis shaders correct the
  // same winding convention before using them as outward surface normals.
  vNormal = normalize(-(gl_NormalMatrix * gl_Normal));
  vEyePosition = vec3(gl_ModelViewMatrix * gl_Vertex);
  vColor = gl_Color;
  vBC = barycentric;
  vMaterial = material;

  // As a tangent, not a normal: the axis lies in the surface, so the linear part
  // of the modelview is what moves it, without the inverse-transpose.
  //
  // The guard is not paranoia. When nothing binds this attribute, GL hands the
  // shader the default (0, 0, 0, 1) - a zero-length axis with FULL anisotropy,
  // which would normalize to NaN and paint the surface with whatever NaN does
  // on that driver. A degenerate axis has to read as isotropic.
  vec3 axis = mat3(gl_ModelViewMatrix) * material_axis.xyz;
  float axisLength = length(axis);
  vMaterialAxis = axisLength > 1e-6
                    ? vec4(axis / axisLength, material_axis.w)
                    : vec4(0.0, 0.0, 1.0, 0.0);

  gl_Position = ftransform();
}
