#version 410 core
in float vAlpha;
in float vGlow;
out vec4 fragColor;

uniform vec3 uColour;
uniform float uOpacity;

void main() {
  float d = length(gl_PointCoord - vec2(0.5));
  float a = smoothstep(0.5, 0.08, d);
  if (a <= 0.001) discard;
  fragColor = vec4(uColour * vGlow, a * vAlpha * uOpacity);
}
