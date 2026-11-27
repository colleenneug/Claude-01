#version 460
#extension GL_GOOGLE_include_directive : require
// =============================================================================
//  tonemap.frag — the last step: the HDR scene (+ bloom) onto the screen.
//
//    1. the scene's pixel, plus bloom: scene + glow x strength. Added, not
//       mixed: the glow holds only what passed the threshold, so mixing it
//       in would darken everything below the threshold.
//    2. exposure;
//    3. tone mapping: none, ACES filmic (Stephen Hill's fit of the Academy
//       Color Encoding System's reference transform: the film-like look,
//       rich contrast, highlights roll off to white), or Khronos PBR Neutral
//       (base colours stay true);
//    4. sRGB ENCODING, ONCE: an _SRGB swapchain encodes on write, so this
//       shader writes linear values; a _UNORM swapchain stores what it is
//       given, so this shader encodes (kEncodeSrgb, a specialization constant
//       set per swapchain format). Never both: that is the classic washed-out
//       look of gamma applied twice.
//
//  push.a = (exposure, tone map: 0 none / 1 ACES / 2 PBR Neutral, bloom strength, bloom on)
//  push.b = (1 / bloom levels, 0, 0, 0)
// =============================================================================
#include "post_common.glsl"

layout(constant_id = 0) const bool kEncodeSrgb = false;

layout(location = 0) out vec4 outColour;

// Stephen Hill's ACES fit (sRGB/Rec.709 primaries in, out).
vec3 toneMapAces(vec3 colour) {
  const mat3 inputMatrix = mat3(0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777);
  const mat3 outputMatrix = mat3(1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602);
  vec3 v = inputMatrix * colour;
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return clamp(outputMatrix * (a / b), 0.0, 1.0);
}

vec3 toneMapPbrNeutral(vec3 colour) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;
  float x = min(colour.r, min(colour.g, colour.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  colour -= offset;
  float peak = max(colour.r, max(colour.g, colour.b));
  if (peak < startCompression) return colour;
  const float d = 1.0 - startCompression;
  float newPeak = 1.0 - d * d / (peak + d - startCompression);
  colour *= newPeak / peak;
  float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
  return mix(colour, vec3(newPeak), g);
}

vec3 encodeSrgb(vec3 linear) {
  linear = clamp(linear, 0.0, 1.0);
  vec3 low = linear * 12.92;
  vec3 high = 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055;
  return mix(high, low, lessThanEqual(linear, vec3(0.0031308)));
}

void main() {
  vec3 colour = texelFetch(source, ivec2(gl_FragCoord.xy), 0).rgb;
  if (push.a.w > 0.5) {
    vec2 uv = gl_FragCoord.xy / vec2(textureSize(source, 0));
    vec3 glow = textureLod(bloom, uv, 0.0).rgb * push.b.x;  // the top level sums every level: average them
    colour += glow * push.a.z;
  }
  colour *= push.a.x;
  if (push.a.y > 1.5) {
    colour = toneMapPbrNeutral(colour);
  } else if (push.a.y > 0.5) {
    colour = toneMapAces(colour);
  }
  outColour = vec4(kEncodeSrgb ? encodeSrgb(colour) : colour, 1.0);
}
