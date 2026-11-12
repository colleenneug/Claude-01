#version 410 core
// Temporal anti-aliasing.
//
// Every frame the whole scene is drawn shifted by a different fraction of a
// pixel (Renderer::jitter), so over eight frames each pixel has been sampled
// in eight places inside itself. Blending those frames together is
// supersampling, paid for one frame at a time — and unlike multisampling it
// also averages what happens *inside* a triangle: a highlight on a bumped
// surface, noise in a shadow's penumbra, grain in the occlusion. That
// shimmer is most of what makes real-time rendering look like real-time
// rendering.
//
// The catch is that last frame's picture is of last frame's world. Each
// pixel is traced back to where it was (the camera moved; this reprojects
// through the depth buffer), and what is found there is only trusted as far
// as it looks like something that could be here now: it is clipped to the
// range of colours in this frame's own 3x3 neighbourhood (Karis 2014,
// Salvi 2016). A thing that was not there last frame is therefore simply
// not blended, rather than left as a ghost.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tCurrent;
uniform sampler2D tHistory;
uniform sampler2D tDepth;
uniform vec2 uTexel;
uniform mat4 uInvViewProjJittered;   // this frame, as drawn
uniform mat4 uViewProj;              // this frame, without the jitter
uniform mat4 uPrevViewProj;          // last frame, without the jitter
uniform float uNear;
uniform float uFar;
uniform float uHistoryValid;         // 0 on the first frame after a cut

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Blending is done on colours compressed by 1/(1+luma) and expanded again
// afterwards. In linear HDR one frame's glint from a lamp is worth a thousand
// ordinary frames, and it flickers; compressed, it is just a bright pixel.
vec3 squash(vec3 c) { return c / (1.0 + luma(c)); }
vec3 unsquash(vec3 c) { return c / max(1.0 - luma(c), 1e-4); }

vec3 toYCoCg(vec3 c) {
  return vec3(0.25 * c.r + 0.5 * c.g + 0.25 * c.b, 0.5 * c.r - 0.5 * c.b, -0.25 * c.r + 0.5 * c.g - 0.25 * c.b);
}
vec3 fromYCoCg(vec3 c) {
  return vec3(c.x + c.y - c.z, c.x + c.z, c.x - c.y - c.z);
}

float linearDepth(float d) {
  float ndc = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - ndc * (uFar - uNear));
}

// History is read with a Catmull-Rom filter rather than bilinear. Every
// frame's reprojection lands between texels, and bilinear there is a small
// blur; applied thirty times a second to its own output it is a large one.
// Five bilinear taps arranged to reproduce the bicubic (Jimenez 2016).
vec3 sampleHistory(vec2 uv) {
  vec2 pos = uv / uTexel;
  vec2 centre = floor(pos - 0.5) + 0.5;
  vec2 f = pos - centre;
  vec2 f2 = f * f, f3 = f2 * f;
  vec2 w0 = -0.5 * f3 + f2 - 0.5 * f;
  vec2 w1 = 1.5 * f3 - 2.5 * f2 + 1.0;
  vec2 w2 = -1.5 * f3 + 2.0 * f2 + 0.5 * f;
  vec2 w3 = 0.5 * f3 - 0.5 * f2;
  vec2 w12 = w1 + w2;
  vec2 t0 = (centre - 1.0) * uTexel;
  vec2 t3 = (centre + 2.0) * uTexel;
  vec2 t12 = (centre + w2 / w12) * uTexel;

  vec4 sum = vec4(0.0);
  sum += vec4(textureLod(tHistory, vec2(t12.x, t0.y), 0.0).rgb, 1.0) * (w12.x * w0.y);
  sum += vec4(textureLod(tHistory, vec2(t0.x, t12.y), 0.0).rgb, 1.0) * (w0.x * w12.y);
  sum += vec4(textureLod(tHistory, vec2(t12.x, t12.y), 0.0).rgb, 1.0) * (w12.x * w12.y);
  sum += vec4(textureLod(tHistory, vec2(t3.x, t12.y), 0.0).rgb, 1.0) * (w3.x * w12.y);
  sum += vec4(textureLod(tHistory, vec2(t12.x, t3.y), 0.0).rgb, 1.0) * (w12.x * w3.y);
  return max(sum.rgb / sum.a, vec3(0.0));
}

void main() {
  vec3 current = textureLod(tCurrent, vUv, 0.0).rgb;
  if (uHistoryValid < 0.5) { fragColor = vec4(current, 1.0); return; }

  // Where was this pixel last frame? Use the nearest depth in the 3x3
  // around it: an edge pixel is partly the thing in front, and it is the
  // thing in front whose motion the edge follows.
  float nearest = 1.0;
  vec2 nearestUv = vUv;
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      vec2 uv = vUv + vec2(x, y) * uTexel;
      float d = textureLod(tDepth, uv, 0.0).r;
      if (d < nearest) { nearest = d; nearestUv = uv; }
    }
  }
  vec2 historyUv = vUv;
  // Whatever is within arm's reach is the weapon, which moves with the
  // camera rather than with the world: it was in the same place on screen
  // last frame, not wherever a fixed point in the world would have been.
  if (linearDepth(nearest) > 1.1) {
    vec4 world = uInvViewProjJittered * vec4(nearestUv * 2.0 - 1.0, nearest * 2.0 - 1.0, 1.0);
    world /= world.w;
    vec4 now = uViewProj * world;
    vec4 then = uPrevViewProj * world;
    historyUv = vUv - (now.xy / now.w - then.xy / then.w) * 0.5;
  }
  if (any(lessThan(historyUv, vec2(0.0))) || any(greaterThan(historyUv, vec2(1.0)))) {
    fragColor = vec4(current, 1.0);
    return;
  }

  // The range of colours plausibly at this pixel now: mean and spread of the
  // 3x3 around it.
  vec3 m1 = vec3(0.0), m2 = vec3(0.0);
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      vec3 c = toYCoCg(squash(textureLod(tCurrent, vUv + vec2(x, y) * uTexel, 0.0).rgb));
      m1 += c;
      m2 += c * c;
    }
  }
  m1 /= 9.0;
  vec3 sigma = sqrt(max(m2 / 9.0 - m1 * m1, vec3(0.0)));
  vec3 lo = m1 - sigma * 1.25, hi = m1 + sigma * 1.25;

  vec3 cur = toYCoCg(squash(current));
  vec3 hist = toYCoCg(squash(sampleHistory(historyUv)));
  // Clip toward the neighbourhood's centre rather than clamping each channel
  // on its own, which would turn an out-of-range colour into a different hue.
  vec3 centre = 0.5 * (hi + lo), extent = 0.5 * (hi - lo) + 1e-5;
  vec3 offset = hist - centre;
  vec3 unit = abs(offset / extent);
  float worst = max(unit.x, max(unit.y, unit.z));
  if (worst > 1.0) hist = centre + offset / worst;

  // Ninety percent history when nothing is moving; less when the pixel has
  // moved far, because far-reprojected history is the blurriest kind.
  float moved = length((historyUv - vUv) / uTexel);
  float keep = mix(0.90, 0.68, clamp(moved / 14.0, 0.0, 1.0));
  vec3 result = fromYCoCg(mix(cur, hist, keep));
  fragColor = vec4(max(unsquash(result), vec3(0.0)), 1.0);
}
