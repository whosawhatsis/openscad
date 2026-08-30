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


// A stand-in environment for the reflection term. There is no scene to reflect, so a
// mirror would otherwise show a flat wash: its lobe is narrow enough to miss both
// point lights entirely. Sampled along the reflection direction in world space, so
// the reflection slides across a surface as the model is orbited, which is the cue
// that reads as "reflective" rather than "pale".
//
// Roughness blurs it by fading toward the environment's mean, which is the cheapest
// stand-in for prefiltering: a rough metal must not mirror a crisp pattern.
vec3 environmentColor(vec3 dir, float roughness)
{
  // A studio: a solid floor and a solid ceiling meeting at a hard horizon. The sharp
  // edge is the point - a narrow transition reads as polished metal, where a wide one
  // reads as a soft grey wash and a repeating pattern reads as flat. Z is up in
  // OpenSCAD, so the vertical axis of the reflection direction drives it.
  //
  // Roughness widens the band rather than fading the whole environment toward grey:
  // that is what a rough surface does to a reflected edge, and it keeps the contrast a
  // polished one needs. It widens by roughness squared, which is GGX's own alpha - the
  // linear version was tried first and washed out a roughness 0.18 surface completely,
  // because the band had already opened to a fifth of the sphere by then.
  vec3 ground = vec3(0.28, 0.28, 0.30);
  vec3 sky = vec3(1.05, 1.06, 1.10);
  float width = mix(0.03, 0.9, roughness * roughness);
  return mix(ground, sky, smoothstep(-width, width, dir.z));
}

void main(void)
{
  vec3 normal = normalize(vNormal);
  vec3 viewDirection = normalize(-vEyePosition);
  // Negative means the model set no roughness - an unbound attribute reads 0, and 0
  // is a meaningful roughness (a mirror), so it cannot be the sentinel. The fallback
  // is the value whose old Blinn-Phong exponent was 64, so a render that supplies no
  // material keeps the look it had before.
  //
  // The floor is not 0: alpha = roughness^2 divides the GGX denominator, so a true
  // zero is a division by zero and a delta lobe no raster sampling can resolve. 0.02
  // is the shiniest value that stays numerically stable.
  float roughness = clamp(vMaterial.x >= 0.0 ? vMaterial.x : 0.417, 0.02, 1.0);
  float metallic = clamp(vMaterial.y, 0.0, 1.0);
  float a = roughness * roughness;
  // A metal has no diffuse response and reflects its own color; a dielectric
  // keeps full diffuse over a dim, uncolored 4% reflection.
  vec3 albedo = vColor.rgb * (1.0 - metallic);
  vec3 F0 = mix(vec3(0.04), vColor.rgb, metallic);
  float NdotV = max(dot(normal, viewDirection), 1e-4);

  // A metal has no diffuse response, so the environment is the only thing it
  // reflects and the only thing that can describe its shape.
  vec3 diffuse = albedo * 0.21;
  vec3 reflection = reflect(-viewDirection, normal);
  vec3 reflectionWorld = normalize(mat3(gl_ModelViewMatrixInverse) * reflection);
  vec3 environment = environmentColor(reflectionWorld, roughness);
  // F0 rather than a Fresnel term. Schlick against the view direction is the more
  // correct ambient reflectance, but it approaches 1 at grazing angles for *every*
  // material, so it lifts plain dielectrics along with the metals it is there for -
  // measured, it put a scene of plain solids 16% above the shader this replaced.
  // F0 keeps a dielectric's contribution at its 0.04 and lets the environment be
  // bright enough for metals to read.
  vec3 specular = F0 * environment;

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
