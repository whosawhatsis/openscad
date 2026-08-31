#version 120
varying vec3 normal;
varying vec4 rawColor;
varying float smoothAngle;
uniform int layer;
void main()
{
  if ((rawColor.a > 0.5) != (layer == 0)) discard;
  gl_FragData[0] = vec4(normalize(normal) * (1.0 + clamp(smoothAngle, 0.0, 180.0) / 180.0), gl_FragCoord.z);
  gl_FragData[1] = rawColor;
}
