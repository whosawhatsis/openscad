#version 120
uniform sampler2D surface, color, previousSurface, previousColor;
uniform vec2 size;
void main()
{
  vec2 uv = gl_FragCoord.xy / size;
  vec4 candidate = texture2D(surface, uv);
  vec4 previous = texture2D(previousSurface, uv);
  bool nearer = candidate.a < 1.0 && candidate.a <= previous.a;
  gl_FragData[0] = nearer ? candidate : previous;
  gl_FragData[1] = nearer ? texture2D(color, uv) : texture2D(previousColor, uv);
}
