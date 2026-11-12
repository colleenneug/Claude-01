#version 410 core
// Sun shafts: how much of the haze between the eye and each pixel is
// actually in sunlight.
//
// The fog is an analytic integral (see applyFog in pbr.frag), which is exact
// and nearly free but assumes the sun reaches every bit of air along the
// ray. Where a wall or a pillar stands between the sun and the air, the air
// behind it is dark — and the edge between lit air and unlit air is what a
// shaft of light *is*. So this walks each view ray through the shadow
// cascades and reports the lit fraction, weighted by where along the ray the
// fog that you actually see is. The fog passes multiply their sun term by
// it. Nothing here is drawn on top of the picture; it only corrects the
// fog's one wrong assumption.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tDepth;
uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform vec3 uCamForward;

struct Cascade { mat4 viewProj; vec2 range; };
uniform Cascade uCascade[3];
uniform sampler2DShadow uCascadeMap0;
uniform sampler2DShadow uCascadeMap1;
uniform sampler2DShadow uCascadeMap2;

uniform float uFogDensity;
uniform float uFogFalloff;
uniform float uFogBase;
uniform float uMaxDistance;
uniform float uFrame;

const int STEPS = 14;

float ign(vec2 px) {
  return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
}

float sunAt(vec3 p) {
  float viewDepth = dot(p - uCamPos, uCamForward);
  if (viewDepth < uCascade[0].range.y) {
    vec4 lc = uCascade[0].viewProj * vec4(p, 1.0);
    return texture(uCascadeMap0, vec3(lc.xy * 0.5 + 0.5, lc.z * 0.5 + 0.5 - 0.0012));
  }
  if (viewDepth < uCascade[1].range.y) {
    vec4 lc = uCascade[1].viewProj * vec4(p, 1.0);
    return texture(uCascadeMap1, vec3(lc.xy * 0.5 + 0.5, lc.z * 0.5 + 0.5 - 0.0012));
  }
  vec4 lc = uCascade[2].viewProj * vec4(p, 1.0);
  return texture(uCascadeMap2, vec3(lc.xy * 0.5 + 0.5, lc.z * 0.5 + 0.5 - 0.0012));
}

void main() {
  float depth01 = textureLod(tDepth, vUv, 0.0).r;
  vec4 wp = uInvViewProj * vec4(vUv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
  vec3 ray = wp.xyz / wp.w - uCamPos;
  float rayLen = length(ray);
  vec3 dir = ray / max(rayLen, 1e-4);
  float len = min(rayLen, uMaxDistance);

  // Start each ray a different fraction of a step in. Fourteen steps in the
  // same places on every ray is fourteen visible slices through the fog;
  // offset, it is grain, and the blur that follows takes grain out.
  float jitter = ign(gl_FragCoord.xy + uFrame * 5.588238);
  float stepLen = len / float(STEPS);
  float densityAtEye = uFogDensity * exp(-uFogFalloff * (uCamPos.y - uFogBase));

  float lit = 0.0, weight = 0.0, optical = 0.0;
  for (int i = 0; i < STEPS; i++) {
    float t = (float(i) + jitter) * stepLen;
    vec3 p = uCamPos + dir * t;
    float density = densityAtEye * exp(-uFogFalloff * (p.y - uCamPos.y));
    optical += density * stepLen;
    // Fog nearer the eye hides fog further away, so nearer samples count
    // for more of what is seen.
    float w = density * exp(-optical);
    lit += sunAt(p) * w;
    weight += w;
  }
  float v = weight > 1e-6 ? lit / weight : 1.0;
  fragColor = vec4(v, v, v, 1.0);
}
