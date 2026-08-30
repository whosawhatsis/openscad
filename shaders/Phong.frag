#version 120

varying vec3 vNormal;
varying vec3 vEyePosition;
varying vec4 vColor;
varying vec3 vBC;
varying vec2 vMaterial;
uniform bool showEdges;

const float PI = 3.14159265;

float edgeFactor()
{
  const float thickness = 1.414;
  vec3 d = fwidth(vBC);
  vec3 antialias = smoothstep(vec3(0.0), thickness * d, vBC);
  return min(min(antialias.x, antialias.y), antialias.z);
}

// Trowbridge-Reitz / GGX normal distribution.
float distributionGGX(float NdotH, float a)
{
  float a2 = a * a;
  float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
  return a2 / (PI * d * d);
}

// Smith height-correlated visibility, which folds the 1/(4 NdotV NdotL)
// denominator of the microfacet BRDF into itself.
float visibilitySmith(float NdotV, float NdotL, float a)
{
  float a2 = a * a;
  float v = NdotL * sqrt(a2 + (1.0 - a2) * NdotV * NdotV);
  float l = NdotV * sqrt(a2 + (1.0 - a2) * NdotL * NdotL);
  return 0.5 / max(v + l, 1e-5);
}

vec3 fresnelSchlick(float VdotH, vec3 F0)
{
  return F0 + (1.0 - F0) * pow(clamp(1.0 - VdotH, 0.0, 1.0), 5.0);
}

void main(void)
{
  vec3 normal = normalize(vNormal);
  vec3 viewDirection = normalize(-vEyePosition);
  // An unbound attribute reads 0, which is not a meaningful roughness. The
  // fallback is the value whose old Blinn-Phong exponent was 64, so a render
  // that supplies no material keeps the look it had before.
  float roughness = clamp(vMaterial.x > 0.0 ? vMaterial.x : 0.417, 0.04, 1.0);
  float metallic = clamp(vMaterial.y, 0.0, 1.0);
  float a = roughness * roughness;
  // A metal has no diffuse response and reflects its own color; a dielectric
  // keeps full diffuse over a dim, uncolored 4% reflection.
  vec3 albedo = vColor.rgb * (1.0 - metallic);
  vec3 F0 = mix(vec3(0.04), vColor.rgb, metallic);
  float NdotV = max(dot(normal, viewDirection), 1e-4);

  // Ambient stands in for an environment this viewport does not have. Without
  // the specular half a metal has no diffuse response and nothing to reflect,
  // so it renders near black. A uniform environment of radiance L integrates to
  // roughly L * F0, which is the whole approximation here.
  //
  // The environment is deliberately brighter than the ambient diffuse. It is the
  // only thing a metal reflects, and because a dielectric's F0 is 0.04 raising it
  // brightens metals while leaving everything else alone: measured over a scene of
  // plain colored solids, taking it from 0.21 to 0.5 moved mean luminance by 2%.
  vec3 diffuse = albedo * 0.21;
  vec3 specular = F0 * 0.5;

  for (int i = 0; i < 2; ++i) {
    vec3 lightDirection = normalize(gl_LightSource[i].position.xyz);
    float NdotL = max(dot(normal, lightDirection), 0.0);
    if (NdotL > 0.0) {
      vec3 halfway = normalize(lightDirection + viewDirection);
      float NdotH = max(dot(normal, halfway), 0.0);
      float VdotH = max(dot(viewDirection, halfway), 0.0);
      vec3 F = fresnelSchlick(VdotH, F0);
      // The lights carry an intensity rather than a bare radiance of 1. A Lambertian
      // surface reflects albedo/pi, so switching from the empirical Blinn-Phong
      // weights to an energy-conserving BRDF made every shaded render about 25%
      // darker (measured: mean luminance 72.1 -> 53.9 over a fixed scene). 1.55
      // restores the previous brightness - measured back to 72.9 - without
      // abandoning energy conservation, which is what a bare 1.0 costs here.
      vec3 radiance = gl_LightSource[i].diffuse.rgb * NdotL * 1.55;

      specular += distributionGGX(NdotH, a) * visibilitySmith(NdotV, NdotL, a) * F * radiance;
      diffuse += (vec3(1.0) - F) * albedo * (1.0 / PI) * radiance;
    }
  }

  // Premultiply only the material contribution. The highlight represents
  // reflected light and remains visible as the material becomes transparent.
  vec4 surface = vec4(clamp(vColor.a * diffuse + specular, 0.0, 1.0), vColor.a);
  vec4 edge = vec4((vColor.rgb + vec3(1.0)) * 0.5, 1.0);
  gl_FragColor = showEdges ? mix(edge, surface, edgeFactor()) : surface;
}
