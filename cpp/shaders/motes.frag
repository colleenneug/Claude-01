#version 410 core
in float vAlpha;
in float vGlow;
out vec4 fragColor;

uniform vec3 uColour;
uniform float uOpacity;
uniform int uTerrainType;
uniform float uTime;

void main() {
  float d = length(gl_PointCoord - vec2(0.5));
  float a;
  if (uTerrainType == 4) {
    a = smoothstep(0.5, 0.02, d);
    float flicker = 0.7 + 0.3 * sin(uTime * 8.0 + vGlow * 20.0);
    a *= flicker;
  } else if (uTerrainType == 1) {
    a = smoothstep(0.5, 0.15, d);
  } else if (uTerrainType == 3) {
    a = smoothstep(0.5, 0.03, d);
  } else {
    a = smoothstep(0.5, 0.08, d);
  }
  if (a <= 0.001) discard;
  fragColor = vec4(uColour * vGlow, a * vAlpha * uOpacity);
}
