#version 120
varying vec3 normal;
varying vec4 rawColor;
varying float smoothAngle;
void main()
{
  normal = normalize(gl_NormalMatrix * gl_Normal);
  rawColor = gl_Color;
  smoothAngle = gl_MultiTexCoord1.x;
  gl_Position = ftransform();
}
