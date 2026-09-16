#version 120
uniform sampler2D edges;
uniform vec2 size;
uniform float width;
uniform bool overlay;
uniform vec4 edgeColor;
void main()
{
  vec2 p = floor(gl_FragCoord.xy);
  float radius = width * 0.5;
  float reach = ceil(radius + 0.5);
  bool hit = false;
  if (width > 0.0) {
    for (float y = max(-reach, -p.y); y <= min(reach, size.y - p.y - 1.0); y += 1.0) {
      for (float x = max(-reach, -p.x); x <= min(reach, size.x - p.x - 1.0); x += 1.0) {
        vec2 value = texture2D(edges, (p + vec2(x,y) + 0.5) / size).xy;
        // Distance to half-pixel boundary segments. Half-open equality gives
        // width 1 a single-pixel centerline, rather than a two-sided response.
        float vertical = length(vec2(x + 0.5, max(abs(y) - 0.5, 0.0)));
        float horizontal = length(vec2(max(abs(x) - 0.5, 0.0), y + 0.5));
        hit = hit || (value.x > 0.5 && (vertical < radius || (vertical == radius && x >= 0.0)))
                  || (value.y > 0.5 && (horizontal < radius || (horizontal == radius && y >= 0.0)));
        if (hit) break;
      }
      if (hit) break;
    }
  }
  if (overlay && !hit) discard;
  gl_FragColor = overlay ? edgeColor : vec4(vec3(hit ? 1.0 : 0.0), 1);
}
