#version 120

varying vec3 vCoord;

void main(void)
{
  gl_FragColor = vec4(vCoord, 1.0);
}
