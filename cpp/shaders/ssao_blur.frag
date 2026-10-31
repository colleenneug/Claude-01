#version 410 core
// Bilateral blur for the raw SSAO output. A 4x4 box weighted by depth
// similarity so AO bleeds within surfaces but not across depth edges —
// a chair leg's AO does not smear onto the wall behind it.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tAO;
uniform sampler2D tDepth;
uniform vec2 uTexelSize;
uniform float uNear;
uniform float uFar;

float linearDepth(float d) {
  float ndc = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - ndc * (uFar - uNear));
}

void main() {
  float rawDepth = texture(tDepth, vUv).r;
  if (rawDepth >= 0.99999) { fragColor = vec4(1.0); return; }
  float centreDepth = linearDepth(rawDepth);
  float sum = 0.0;
  float wSum = 0.0;

  for (int x = -2; x <= 2; x++) {
    for (int y = -2; y <= 2; y++) {
      vec2 offset = vec2(float(x), float(y)) * uTexelSize;
      float aoVal = texture(tAO, vUv + offset).r;
      float d = linearDepth(texture(tDepth, vUv + offset).r);
      float w = 1.0 / (1.0 + abs(d - centreDepth) * 4.0);
      sum += aoVal * w;
      wSum += w;
    }
  }

  fragColor = vec4(vec3(sum / max(wSum, 0.001)), 1.0);
}
