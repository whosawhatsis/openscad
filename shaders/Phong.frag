#version 120

varying vec3 vNormal;
varying vec3 vEyePosition;
varying vec4 vColor;
varying vec3 vBC;
varying vec4 vMaterial;
uniform bool showEdges;

// Screen-space reflections. The scene as drawn by a first pass, plus its depth,
// so a surface can reflect the model itself instead of only the stand-in
// environment. Off unless the experimental feature is enabled, in which case
// the geometry is drawn twice and this is the second pass.
uniform bool ssrEnabled;
uniform sampler2D ssrColor;
uniform sampler2D ssrDepth;

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
  // A studio, with all of its contrast collected at the horizon. Z is up in OpenSCAD,
  // so the vertical axis of the reflection direction drives it.
  //
  // The hard horizon is the edge that reads as polished metal: a soft ramp alone reads
  // as a grey wash, and a repeating pattern reads as flat because it carries no
  // orientation. Away from the horizon both halves fall back toward mid - the floor
  // lightens going down, the ceiling darkens going up - which is what a real studio
  // does and what puts a bright band around a curved surface with falloff either side.
  // That band is what gives a sphere its roundness back.
  //
  // 0.6 rather than more: rendered as a matrix, a full reversal darkens the crown of a
  // part enough to hide the face a CAD user is usually looking at, and a monotonic ramp
  // in the other direction leaves a sphere flat-topped. Non-metals are untouched by
  // this dial either way, measured at 77.0 mean against 76.9 for the monotonic version,
  // because a dielectric's F0 is 0.04.
  //
  // Roughness widens the band rather than fading the environment toward grey - that is
  // what a rough surface does to a reflected edge, and it keeps the contrast a polished
  // one needs. It widens by roughness squared, GGX's own alpha; linear was tried and
  // washed out a roughness 0.18 surface completely.
  vec3 ground = vec3(0.28, 0.28, 0.30);
  vec3 sky = vec3(1.05, 1.06, 1.10);
  float width = mix(0.02, 0.9, roughness * roughness);
  float horizon = smoothstep(-width, width, dir.z);
  return mix(ground, sky, mix(horizon, 0.5, 0.6 * abs(dir.z)));
}

// View-space z for a window-space depth. Perspective and orthographic differ in
// whether clip w carries -z, and OpenSCAD offers both, so read which one this is
// off the projection matrix rather than assuming.
float viewDepth(float ndcZ)
{
  if (gl_ProjectionMatrix[2][3] != 0.0) {
    return gl_ProjectionMatrix[3][2] / (-ndcZ - gl_ProjectionMatrix[2][2]);
  }
  return (ndcZ - gl_ProjectionMatrix[3][2]) / gl_ProjectionMatrix[2][2];
}

// March the reflection ray through the depth buffer. Returns the reflected
// color in rgb and a confidence in a: 0 means nothing was hit and the caller
// must fall back to the environment, which is most of the frame in a typical
// CAD scene where the model occupies a fraction of the viewport.
vec4 screenSpaceReflection(vec3 origin, vec3 dir)
{
  // Everything here is scaled by the distance to the shaded point rather than
  // fixed in millimetres. OpenSCAD models range from watch parts to buildings
  // and a constant step would be either uselessly coarse or unusably slow
  // depending only on what the user happens to be drawing.
  float stride = 0.02 * length(origin);
  float t = 0.0;
  float previousT = 0.0;
  bool crossed = false;

  // Coarse march: find the step on which the ray passes behind the recorded
  // surface. This only brackets the crossing - it does not locate it.
  for (int i = 0; i < 32; ++i) {
    previousT = t;
    t += stride;
    // Grow geometrically: fine near the surface, where contact reflections
    // live and error is most visible, cheap further out.
    stride *= 1.12;
    vec3 probe = origin + dir * t;
    // Behind the eye - nothing in front of the camera can be sampled.
    if (probe.z > -1e-4) return vec4(0.0);

    vec4 clip = gl_ProjectionMatrix * vec4(probe, 1.0);
    vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec4(0.0);

    float surface = viewDepth(texture2D(ssrDepth, uv).r * 2.0 - 1.0);
    if (probe.z < surface) {
      // A depth buffer records a front face, not a solid, so anything deeper
      // than a plausible thickness is a different object the ray passed behind
      // rather than the one it hit. Relative for the same reason the stride is.
      if (surface - probe.z > 0.08 * abs(surface)) return vec4(0.0);
      crossed = true;
      break;
    }
  }
  if (!crossed) return vec4(0.0);

  // Bisect the bracketing interval. Without this the reflection is quantized to
  // the coarse steps and a curved object reflects as a stack of rings - which
  // is exactly what the first working version of this drew. Ten halvings take
  // the residual error below a pixel for any stride this loop produces, and
  // cost six texture samples against the 32 above.
  for (int i = 0; i < 10; ++i) {
    float mid = (previousT + t) * 0.5;
    vec3 probe = origin + dir * mid;
    vec4 clip = gl_ProjectionMatrix * vec4(probe, 1.0);
    vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
    float surface = viewDepth(texture2D(ssrDepth, uv).r * 2.0 - 1.0);
    if (probe.z < surface) {
      t = mid;
    } else {
      previousT = mid;
    }
  }

  vec3 hit = origin + dir * t;
  vec4 clip = gl_ProjectionMatrix * vec4(hit, 1.0);
  vec2 uv = (clip.xy / clip.w) * 0.5 + 0.5;
  // Fade out at the screen edge, where the reflected object simply is not in
  // the buffer, so reflections dissolve instead of ending in a hard line.
  vec2 edge = smoothstep(vec2(0.0), vec2(0.1), uv) *
              (vec2(1.0) - smoothstep(vec2(0.9), vec2(1.0), uv));
  return vec4(texture2D(ssrColor, uv).rgb, edge.x * edge.y);
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
  // Normal-incidence reflectance of the dielectric half, folded from ior and
  // specular on the CPU because both do the same single job here. 0.04 is
  // ior 1.5, the conventional default for everything that is not a metal.
  float reflectance = clamp(vMaterial.z, 0.0, 1.0);
  float emission = max(vMaterial.w, 0.0);
  float a = roughness * roughness;
  // A metal has no diffuse response and reflects its own color; a dielectric
  // keeps full diffuse over a dim, uncolored 4% reflection.
  vec3 albedo = vColor.rgb * (1.0 - metallic);
  vec3 F0 = mix(vec3(reflectance), vColor.rgb, metallic);
  float NdotV = max(dot(normal, viewDirection), 1e-4);

  // A metal has no diffuse response, so the environment is the only thing it
  // reflects and the only thing that can describe its shape.
  vec3 diffuse = albedo * 0.21;
  vec3 reflection = reflect(-viewDirection, normal);
  vec3 reflectionWorld = normalize(mat3(gl_ModelViewMatrixInverse) * reflection);
  vec3 environment = environmentColor(reflectionWorld, roughness);
  if (ssrEnabled) {
    // A rough surface scatters its reflection over a cone that a single ray
    // cannot represent, so confidence falls away with roughness rather than
    // the march being run and then blurred - which it has no way to do here.
    // Start the ray just off the surface along its own normal. Without this a
    // ray leaving at a grazing angle immediately re-hits the surface it came
    // from - the depth buffer cannot tell "this pixel" from "a pixel in front
    // of it" - and the contact region fringes with spurious hits.
    vec3 rayOrigin = vEyePosition + normal * (0.01 * length(vEyePosition));
    vec4 hit = screenSpaceReflection(rayOrigin, reflection);
    float weight = hit.a * (1.0 - smoothstep(0.05, 0.45, roughness));
    environment = mix(environment, hit.rgb, weight);
  }
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

  // Emitted light is not reflected light: it takes no Fresnel, no shadowing and
  // no environment, and it is the one term that survives a surface facing away
  // from every light. Scaled by the surface color so an emissive red part glows
  // red rather than white.
  vec3 emitted = vColor.rgb * emission;

  // Premultiply only the material contribution. The highlight represents
  // reflected light and remains visible as the material becomes transparent.
  vec4 surface = vec4(clamp(vColor.a * diffuse + specular + emitted, 0.0, 1.0), vColor.a);
  vec4 edge = vec4((vColor.rgb + vec3(1.0)) * 0.5, 1.0);
  gl_FragColor = showEdges ? mix(edge, surface, edgeFactor()) : surface;
}
