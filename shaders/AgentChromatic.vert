#version 120

varying vec3 vNormal;

void main(void)
{
  // Negated for the same reason as AgentNormalMap.vert: VBOBuilder hands us
  // inward-facing normals. Shading with them would light every surface from
  // behind, and the gauge sphere - which is computed analytically from correct
  // outward normals - would not match the model beside it.
  vNormal = normalize(-(gl_NormalMatrix * gl_Normal));
  gl_Position = ftransform();
}
