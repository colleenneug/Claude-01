#version 410 core
// Soft-knee bright pass: a hard cutoff makes bloom switch on and off as a
// surface crosses the threshold; the knee ramps the contribution in over a
// band instead, so a lamp brightens smoothly rather than snapping on.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tScene;
uniform float uThreshold;
uniform float uKnee;

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

void main() {
  vec3 c = texture(tScene, vUv).rgb;
  float l = luma(c);
  float knee = uThreshold * uKnee + 1e-5;
  float soft = clamp(l - uThreshold + knee, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee);
  float contribution = max(soft, l - uThreshold) / max(l, 1e-5);
  fragColor = vec4(c * contribution, 1.0);
}
