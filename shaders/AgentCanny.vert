#version 120
varying vec3 normal;
varying float depth;
varying vec4 rawColor;
varying float smoothAngle;
void main()
{
  normal = normalize(gl_NormalMatrix * gl_Normal);
  depth = -(gl_ModelViewMatrix * gl_Vertex).z;
  rawColor = gl_Color;
  smoothAngle = gl_MultiTexCoord1.x;
  gl_Position = ftransform();
}
