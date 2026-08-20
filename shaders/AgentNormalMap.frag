#version 120

varying vec3 vNormal;

void main(void)
{
  // Map from [-1, 1] to [0, 1]
  vec3 color = (normalize(vNormal) * 0.5) + 0.5;
  gl_FragColor = vec4(color, 1.0);
}
