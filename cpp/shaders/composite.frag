#version 410 core
// Final composite: everything that happens in the camera rather than in the
// world. Depth-of-field blend, bloom, exposure, ACES filmic tone mapping, a
// light grade, sRGB encode, lens aberration, grain, vignette.
//
// The scene arrives as linear HDR radiance, already anti-aliased, with the
// sky and the fog in it (they are part of the world, and are drawn there —
// see sky.frag and applyFog in pbr.frag). Nothing before this pass tone-maps,
// which is what lets bloom see highlights a tone-mapped buffer would already
// have thrown away.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tScene;
uniform sampler2D tDepth;
uniform sampler2D tBloom;
uniform sampler2D tDof;
uniform sampler2D tAdapt;      // 1x1, adapted log-luminance of the scene

uniform vec2 uTexel;
uniform float uNear;
uniform float uFar;

uniform float uAim;
uniform float uDofFocus;
uniform float uDofRange;

uniform float uExposure;
// Eye adaptation: how bright a frame is "meant" to average, and how far the
// exposure is allowed to go to get it there. Strength 0 turns it off.
uniform float uAdaptKey;
uniform float uAdaptStrength;

uniform float uTime;
uniform float uGrain;
uniform float uVignette;
uniform float uAberration;
uniform float uBloom;
uniform float uSharpen;

float linearDepth(float d) {
  float ndc = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - ndc * (uFar - uNear));
}

float luma(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// three's ACES fit, so the grade matches whichever renderer the reader
// compares this against.
vec3 rrtOdtFit(vec3 v) {
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return a / b;
}
vec3 acesFilm(vec3 colour) {
  const mat3 IN = mat3(
    vec3(0.59719, 0.07600, 0.02840),
    vec3(0.35458, 0.90834, 0.13383),
    vec3(0.04823, 0.01566, 0.83777));
  const mat3 OUT = mat3(
    vec3( 1.60475, -0.10208, -0.00327),
    vec3(-0.53108,  1.10813, -0.07276),
    vec3(-0.07367, -0.00605,  1.07602));
  colour = IN * (colour / 0.6);
  colour = rrtOdtFit(colour);
  return clamp(OUT * colour, 0.0, 1.0);
}

vec3 linearToSRGB(vec3 c) {
  return mix(pow(c, vec3(0.41666)) * 1.055 - vec3(0.055),
             c * 12.92, vec3(lessThanEqual(c, vec3(0.0031308))));
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
  vec2 uv = vUv;
  vec2 fromCentre = uv - 0.5;
  float r2 = dot(fromCentre, fromCentre);

  // Lens aberration: the three channels focus at slightly different radii,
  // more so towards the edge of the frame.
  float ab = uAberration * 0.0016 * (0.35 + r2);
  vec3 col;
  col.r = texture(tScene, uv + fromCentre * ab).r;
  col.g = texture(tScene, uv).g;
  col.b = texture(tScene, uv - fromCentre * ab).b;

  // Temporal anti-aliasing trades shimmer for a little softness. Put some of
  // the edge back: push each pixel away from the average of its four
  // neighbours — in a compressed range, so the rim of a lamp is not turned
  // into a black ring — and never beyond what those neighbours themselves
  // span, which is what stops sharpening from inventing halos.
  if (uSharpen > 0.001) {
    vec3 n = texture(tScene, uv + vec2(0.0, uTexel.y)).rgb;
    vec3 s = texture(tScene, uv - vec2(0.0, uTexel.y)).rgb;
    vec3 e = texture(tScene, uv + vec2(uTexel.x, 0.0)).rgb;
    vec3 w = texture(tScene, uv - vec2(uTexel.x, 0.0)).rgb;
    vec3 c = texture(tScene, uv).rgb;
    float lc = luma(c) / (1.0 + luma(c));
    float ln = luma(n) / (1.0 + luma(n)), ls = luma(s) / (1.0 + luma(s));
    float le = luma(e) / (1.0 + luma(e)), lw = luma(w) / (1.0 + luma(w));
    float lo = min(lc, min(min(ln, ls), min(le, lw)));
    float hi = max(lc, max(max(ln, ls), max(le, lw)));
    float sharp = clamp(lc + (lc - 0.25 * (ln + ls + le + lw)) * uSharpen, lo, hi);
    col *= sharp / max(lc, 1e-4);
  }

  // Depth of field, blended in by the same circle-of-confusion the half-res
  // pass computed, scaled by how far down the sights the camera currently is.
  if (uAim > 0.001) {
    float dist = linearDepth(texture(tDepth, uv).r);
    float coc = smoothstep(uDofFocus, uDofFocus + uDofRange, dist) * uAim;
    col = mix(col, texture(tDof, uv).rgb, clamp(coc, 0.0, 1.0));
  }

  // Bloom, added while still linear HDR.
  col += texture(tBloom, uv).rgb * uBloom;

  // Exposure. The base value is the grade the scenes were lit for; the
  // adaptation nudges it toward a frame that averages mid-grey, the way an
  // eye opens up in a dark room and stops down facing the sun. Deliberately
  // partial and tightly clamped — a place that is meant to be dark should
  // stay dark, just not unreadable.
  float adapt = 1.0;
  if (uAdaptStrength > 0.0) {
    float adapted = exp(texelFetch(tAdapt, ivec2(0), 0).r);
    adapt = clamp(pow(uAdaptKey / max(adapted, 1e-4), uAdaptStrength), 0.70, 1.50);
  }

  // Everything above this line is radiance; everything below is a picture.
  col = acesFilm(col * uExposure * adapt);

  // The grade: a breath of cool in the shadows and warmth in the highlights,
  // which is what film stock does and what makes a frame read as shot rather
  // than computed. Small enough to miss, large enough to miss when it's gone.
  float y = luma(col);
  col *= mix(vec3(0.965, 1.0, 1.045), vec3(1.03, 1.0, 0.965), smoothstep(0.0, 0.75, y));
  col = mix(vec3(y), col, 1.06);
  col = clamp(col, 0.0, 1.0);

  col = linearToSRGB(col);

  col += (hash(uv * vec2(1024.0, 768.0) + uTime) - 0.5) * uGrain;
  col *= clamp(1.0 - uVignette * r2 * 1.9, 0.0, 1.0);

  fragColor = vec4(col, 1.0);
}
