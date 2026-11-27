#version 460
#extension GL_GOOGLE_include_directive : require
// =============================================================================
//  bloom.frag — one step of the bloom mip chain (render/src/PostProcess.cpp).
//
//  BLOOM: very bright light spills a soft glow round itself (in a camera
//  lens, and in the eye). It is built in two sweeps over a chain of ever
//  smaller images:
//
//    DOWN (push.b.x = 0): each level is a filtered half of the level above
//      (13 taps, Jimenez 2014, "Next Generation Post Processing in Call of
//      Duty"). The FIRST step reads the HDR scene and keeps only what is
//      above the threshold (soft knee), and weights its five 4-tap groups by
//      1 / (1 + luminance) (the KARIS AVERAGE): one tiny, extremely bright
//      pixel cannot dominate, so highlights do not flicker frame to frame.
//    UP (push.b.x = 1): each level gets the level below it added on,
//      upsampled with a 3x3 tent filter (the pipeline blends ONE + ONE).
//
//  The top level then holds glows of every size, from a few pixels to a
//  quarter of the screen.
//
//  push.a = (1 / source width, 1 / source height, threshold, knee)
//  push.b = (0 down / 1 up, 1 = first down step, target width, target height)
// =============================================================================
#include "post_common.glsl"

layout(location = 0) out vec4 outColour;

vec3 tap(vec2 uv, vec2 texelsAway) {
  return textureLod(source, uv + texelsAway * push.a.xy, 0.0).rgb;
}

// Soft-knee threshold: nothing below threshold - knee, all of what is above
// threshold + knee, a smooth curve in between.
vec3 threshold(vec3 c) {
  float bright = max(c.r, max(c.g, c.b));
  float knee = max(push.a.w, 1e-4);
  float soft = clamp(bright - push.a.z + knee, 0.0, 2.0 * knee);
  soft = soft * soft / (4.0 * knee);
  float keep = max(soft, bright - push.a.z) / max(bright, 1e-4);
  return c * keep;
}

vec3 downsample(vec2 uv, bool first) {
  vec3 a = tap(uv, vec2(-2.0, 2.0)), b = tap(uv, vec2(0.0, 2.0)), c = tap(uv, vec2(2.0, 2.0));
  vec3 d = tap(uv, vec2(-2.0, 0.0)), e = tap(uv, vec2(0.0, 0.0)), f = tap(uv, vec2(2.0, 0.0));
  vec3 g = tap(uv, vec2(-2.0, -2.0)), h = tap(uv, vec2(0.0, -2.0)), i = tap(uv, vec2(2.0, -2.0));
  vec3 j = tap(uv, vec2(-1.0, 1.0)), k = tap(uv, vec2(1.0, 1.0));
  vec3 l = tap(uv, vec2(-1.0, -1.0)), m = tap(uv, vec2(1.0, -1.0));
  // Five overlapping 2x2 groups: the centre one weighs 0.5, the corners 0.125 each.
  vec3 groups[5] = vec3[5]((j + k + l + m) * 0.25, (a + b + d + e) * 0.25, (b + c + e + f) * 0.25,
                           (d + e + g + h) * 0.25, (e + f + h + i) * 0.25);
  float weights[5] = float[5](0.5, 0.125, 0.125, 0.125, 0.125);
  vec3 sum = vec3(0.0);
  float total = 0.0;
  for (int n = 0; n < 5; ++n) {
    // Karis average (first step only): a bright group counts for less.
    float w = first ? weights[n] / (1.0 + luminance(groups[n])) : weights[n];
    sum += groups[n] * w;
    total += w;
  }
  vec3 colour = sum / total;
  return first ? threshold(colour) : colour;
}

vec3 upsample(vec2 uv) {
  // 3x3 tent: 1 2 1 / 2 4 2 / 1 2 1, over 16.
  vec3 s = tap(uv, vec2(0.0)) * 4.0;
  s += (tap(uv, vec2(-1.0, 0.0)) + tap(uv, vec2(1.0, 0.0)) + tap(uv, vec2(0.0, -1.0)) + tap(uv, vec2(0.0, 1.0))) * 2.0;
  s += tap(uv, vec2(-1.0, -1.0)) + tap(uv, vec2(1.0, -1.0)) + tap(uv, vec2(-1.0, 1.0)) + tap(uv, vec2(1.0, 1.0));
  return s / 16.0;
}

void main() {
  vec2 uv = gl_FragCoord.xy / push.b.zw;  // this pixel's centre, as a fraction of the target
  vec3 colour = push.b.x < 0.5 ? downsample(uv, push.b.y > 0.5) : upsample(uv);
  outColour = vec4(colour, 1.0);
}
