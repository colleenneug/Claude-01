#version 410 core
// A depth-aware blur and upsample in one. The occlusion and sun-shaft passes
// both run at half resolution with a per-pixel rotated pattern, so both come
// out as the right answer plus grain, and both need the grain averaged away
// *without* averaging across an edge: occlusion from the wall behind a crate
// does not belong on the crate. Each tap is weighted by how close its depth
// is to the depth of the pixel being written.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tSource;    // half-resolution, single channel
uniform sampler2D tDepth;     // full-resolution scene depth
uniform vec2 uSourceTexel;
uniform float uNear;
uniform float uFar;

float linearDepth(float d) {
  float ndc = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - ndc * (uFar - uNear));
}

void main() {
  float centre = linearDepth(textureLod(tDepth, vUv, 0.0).r);
  float sum = 0.0, wsum = 0.0;
  // A 4x4 footprint of source texels around this pixel — the same size as
  // the tile the rotation noise repeats over, so it cancels rather than
  // merely softens.
  for (int y = -1; y <= 2; y++) {
    for (int x = -1; x <= 2; x++) {
      vec2 uv = vUv + (vec2(x, y) - 0.5) * uSourceTexel;
      float d = linearDepth(textureLod(tDepth, uv, 0.0).r);
      float w = 1.0 / (0.0008 + abs(d - centre) / max(centre, 0.05) * 9.0);
      sum += textureLod(tSource, uv, 0.0).r * w;
      wsum += w;
    }
  }
  float v = sum / max(wsum, 1e-5);
  fragColor = vec4(v, v, v, 1.0);
}
