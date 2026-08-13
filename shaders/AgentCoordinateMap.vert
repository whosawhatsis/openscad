#version 120

varying vec3 vCoord;

void main(void)
{
  vCoord = gl_Vertex.xyz;
  gl_Position = ftransform();
}
