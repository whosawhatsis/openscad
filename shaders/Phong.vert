#version 120

varying vec3 vNormal;
varying vec3 vEyePosition;
varying vec4 vColor;

void main(void)
{
  // VBOBuilder supplies inward-facing normals; analysis shaders correct the
  // same winding convention before using them as outward surface normals.
  vNormal = normalize(-(gl_NormalMatrix * gl_Normal));
  vEyePosition = vec3(gl_ModelViewMatrix * gl_Vertex);
  vColor = gl_Color;
  gl_Position = ftransform();
}
