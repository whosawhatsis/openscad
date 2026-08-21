#version 120

varying vec3 vNormal;
varying vec3 vEyePosition;
varying vec4 vColor;

void main(void)
{
  vec3 normal = normalize(vNormal);
  vec3 viewDirection = normalize(-vEyePosition);
  float diffuse = 0.0;
  float specular = 0.0;

  for (int i = 0; i < 2; ++i) {
    vec3 lightDirection = normalize(gl_LightSource[i].position.xyz);
    float contribution = max(dot(normal, lightDirection), 0.0);
    diffuse += contribution;
    if (contribution > 0.0) {
      vec3 halfway = normalize(lightDirection + viewDirection);
      specular += pow(max(dot(normal, halfway), 0.0), 64.0);
    }
  }

  vec3 shaded = vColor.rgb * (0.18 + 0.55 * min(diffuse, 1.0)) + vec3(0.35 * min(specular, 1.0));
  gl_FragColor = vec4(clamp(shaded, 0.0, 1.0), vColor.a);
}
