#version 120

varying vec3 vNormal;
varying vec3 vEyePosition;
varying vec4 vColor;
varying vec3 vBC;
varying vec2 vMaterial;
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
  // An unbound attribute reads 0, which is not a meaningful exponent. Falling
  // back to the constants this shader used before material attributes existed
  // keeps every render that supplies no material identical to before.
  float shininess = vMaterial.x > 0.0 ? vMaterial.x : 64.0;
  float metallic = clamp(vMaterial.y, 0.0, 1.0);
  float diffuse = 0.0;
  float specular = 0.0;

  for (int i = 0; i < 2; ++i) {
    vec3 lightDirection = normalize(gl_LightSource[i].position.xyz);
    float contribution = max(dot(normal, lightDirection), 0.0);
    diffuse += contribution;
    if (contribution > 0.0) {
      vec3 halfway = normalize(lightDirection + viewDirection);
      specular += pow(max(dot(normal, halfway), 0.0), shininess);
    }
  }

  // A metal has no diffuse response and tints its highlight with its own
  // colour; a dielectric keeps full diffuse and reflects the light's colour.
  vec3 base = vColor.rgb * (0.18 + 0.55 * min(diffuse, 1.0)) * (1.0 - metallic);
  vec3 highlightTint = mix(vec3(1.0), vColor.rgb, metallic);
  vec3 highlight = highlightTint * (0.35 * min(specular, 1.0));
  // Premultiply only the material contribution. The highlight represents
  // reflected light and remains visible as the material becomes transparent.
  vec4 surface = vec4(clamp(vColor.a * base + highlight, 0.0, 1.0), vColor.a);
  vec4 edge = vec4((vColor.rgb + vec3(1.0)) * 0.5, 1.0);
  gl_FragColor = showEdges ? mix(edge, surface, edgeFactor()) : surface;
}
