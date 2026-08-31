#version 120
uniform sampler2D opaqueSurface, opaqueColor, transparentSurface, transparentColor;
uniform vec2 size, clip;
uniform bool perspective, colorEdges;

float eyeDepth(float z)
{
  return perspective ? clip.x * clip.y / (clip.y - z * (clip.y - clip.x))
                     : clip.x + z * (clip.y - clip.x);
}

vec4 sampleSurface(sampler2D tex, vec2 pixel, bool transparent)
{
  if (any(lessThan(pixel, vec2(0.0))) || any(greaterThanEqual(pixel, size))) return vec4(0,0,0,1);
  vec4 value = texture2D(tex, (pixel + 0.5) / size);
  if (transparent && value.a >= texture2D(opaqueSurface, (pixel + 0.5) / size).a) return vec4(0,0,0,1);
  return value;
}

float boundary(sampler2D surface, sampler2D color, vec2 p, vec2 axis, bool transparent)
{
  vec4 a = sampleSurface(surface, p, transparent);
  vec4 b = sampleSurface(surface, p + axis, transparent);
  if ((a.a < 1.0) != (b.a < 1.0)) return 1.0;
  if (a.a >= 1.0) return 0.0;
  float tolerance = min(length(a.xyz), length(b.xyz)) - 1.0;
  float cosine = dot(normalize(a.xyz), normalize(b.xyz));
  if (cosine < min(0.99999, cos(tolerance * 3.141592654))) return 1.0;
  vec4 left = sampleSurface(surface, p - axis, transparent);
  vec4 right = sampleSurface(surface, p + 2.0 * axis, transparent);
  float delta = eyeDepth(b.a) - eyeDepth(a.a);
  float before = left.a < 1.0 ? eyeDepth(a.a) - eyeDepth(left.a) : delta;
  float after = right.a < 1.0 ? eyeDepth(right.a) - eyeDepth(b.a) : delta;
  float residual = min(abs(delta - before), abs(delta - after));
  float footprint = max(1e-5, abs(eyeDepth(a.a)) / max(size.x, size.y));
  if (residual > max(footprint * 0.1, 0.5 * max(abs(before), abs(after)))) return 1.0;
  if (colorEdges && any(notEqual(texture2D(color, (p + 0.5) / size),
                                texture2D(color, (p + axis + 0.5) / size)))) return 1.0;
  return 0.0;
}
void main()
{
  vec2 p = floor(gl_FragCoord.xy);
  vec2 result;
  result.x = max(boundary(opaqueSurface, opaqueColor, p, vec2(1,0), false),
                 boundary(transparentSurface, transparentColor, p, vec2(1,0), true));
  result.y = max(boundary(opaqueSurface, opaqueColor, p, vec2(0,1), false),
                 boundary(transparentSurface, transparentColor, p, vec2(0,1), true));
  gl_FragData[0] = vec4(result, 0, 1);
  gl_FragData[1] = vec4(0);
}
