#version 120

// Directions toward each light, eye space, one per color channel. Supplied
// from chromatic_lights() so the model and the analytic gauge sphere are lit by
// one table rather than two that can drift apart.
uniform vec3 lightRed;
uniform vec3 lightGreen;
uniform vec3 lightBlue;

varying vec3 vNormal;

void main(void)
{
  vec3 n = normalize(vNormal);
  // Lambert, clamped per light: a surface turned away from one light must
  // receive nothing from it rather than subtracting from the other two.
  gl_FragColor = vec4(max(0.0, dot(n, lightRed)), max(0.0, dot(n, lightGreen)),
                      max(0.0, dot(n, lightBlue)), 1.0);
}
