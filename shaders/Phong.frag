#version 120

varying vec3 vNormal;
varying vec3 vEyePosition;
varying vec4 vColor;
varying vec3 vBC;
uniform bool showEdges;

float edgeFactor()
{
  const float thickness = 1.414;
  vec3 d = fwidth(vBC);
  vec3 antialias = smoothstep(vec3(0.0), thickness * d, vBC);
  return min(min(antialias.x, antialias.y), antialias.z);
}

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
  vec4 surface = vec4(clamp(shaded, 0.0, 1.0), vColor.a);
  vec4 edge = vec4((vColor.rgb + vec3(1.0)) * 0.5, 1.0);
  gl_FragColor = showEdges ? mix(edge, surface, edgeFactor()) : surface;
}
