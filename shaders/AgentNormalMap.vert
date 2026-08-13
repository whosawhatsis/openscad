#version 120

varying vec3 vNormal;

void main(void)
{
  // Normal in camera space
  vNormal = normalize(gl_NormalMatrix * gl_Normal);
  
  // Basic transform
  gl_Position = ftransform();
}
