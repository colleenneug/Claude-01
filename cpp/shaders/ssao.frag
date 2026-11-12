#version 410 core
// Screen-space ambient occlusion, at half resolution, from the depth
// pre-pass alone.
//
// The probe lights every point as if nothing were near it. That is true of a
// point in the middle of a floor and false of the foot of a wall, the inside
// of a corner and the gap under a crate — and it is exactly those places
// that tell the eye a thing is standing *on* the ground rather than pasted
// over it. This estimates, per pixel, how much of the hemisphere above the
// surface is blocked by nearby geometry (McGuire et al., "Scalable Ambient
// Obscurance", 2012), and pbr.frag holds the fill light back by that much.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tDepth;
uniform vec2 uDepthTexel;     // one texel of the full-resolution depth buffer
uniform vec4 uProjInfo;       // 1/P00, 1/P11, P20, P21 — unprojects a pixel to view space
uniform float uNear;
uniform float uFar;
uniform float uProjScale;     // pixels (of this half-res target) per metre at one metre
uniform float uRadius;        // metres
uniform float uFrame;

const int SAMPLES = 11;
const float TURNS = 7.0;      // coprime with SAMPLES, so the spiral never lines up with itself

float linearDepth(float d) {
  float ndc = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - ndc * (uFar - uNear));
}

vec3 viewPos(vec2 uv) {
  float z = linearDepth(textureLod(tDepth, uv, 0.0).r);
  vec2 ndc = uv * 2.0 - 1.0;
  return vec3((ndc.x + uProjInfo.z) * uProjInfo.x * z, (ndc.y + uProjInfo.w) * uProjInfo.y * z, -z);
}

float ign(vec2 px) {
  return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
}

void main() {
  float rawDepth = textureLod(tDepth, vUv, 0.0).r;
  if (rawDepth >= 0.99999) { fragColor = vec4(1.0); return; }   // sky

  vec3 P = viewPos(vUv);

  // The surface normal, from the depth buffer itself. Across a silhouette
  // one of the two neighbours belongs to something else entirely, so on each
  // axis the difference is taken toward whichever neighbour is closer in
  // depth — the one that is on the same surface.
  vec3 Pr = viewPos(vUv + vec2(uDepthTexel.x, 0.0)), Pl = viewPos(vUv - vec2(uDepthTexel.x, 0.0));
  vec3 Pu = viewPos(vUv + vec2(0.0, uDepthTexel.y)), Pd = viewPos(vUv - vec2(0.0, uDepthTexel.y));
  vec3 dx = abs(Pr.z - P.z) < abs(P.z - Pl.z) ? Pr - P : P - Pl;
  vec3 dy = abs(Pu.z - P.z) < abs(P.z - Pd.z) ? Pu - P : P - Pd;
  vec3 n = normalize(cross(dx, dy));

  float depth = -P.z;
  float radiusPx = uRadius * uProjScale / depth;
  // Too far for the radius to cover even a couple of pixels: nothing to find.
  if (radiusPx < 1.5) { fragColor = vec4(1.0); return; }
  radiusPx = min(radiusPx, 72.0);
  // The world-space reach that capped pixel radius still corresponds to.
  float radius = radiusPx * depth / uProjScale;
  float r2 = radius * radius;

  float spin = ign(gl_FragCoord.xy + uFrame * 5.588238) * 6.2831853;
  vec2 texel = uDepthTexel * 2.0;   // one pixel of this target, in uv

  float sum = 0.0;
  for (int i = 0; i < SAMPLES; i++) {
    float a = (float(i) + 0.5) / float(SAMPLES);
    float angle = a * TURNS * 6.2831853 + spin;
    vec2 offset = vec2(cos(angle), sin(angle)) * a * radiusPx;
    vec3 Q = viewPos(vUv + offset * texel);
    vec3 v = Q - P;
    float vv = dot(v, v);
    float vn = dot(v, n);
    // A sample occludes by how far it rises above the tangent plane, and
    // stops counting smoothly as it leaves the radius — a hard cutoff rings.
    float f = max(r2 - vv, 0.0);
    sum += f * f * f * max((vn - 0.012 * depth - 0.004) / (vv + 0.0005), 0.0);
  }

  float ao = max(0.0, 1.0 - sum * 5.0 * 1.15 / (r2 * r2 * r2 * float(SAMPLES)) * radius);
  // Fade out with distance rather than stopping at the radiusPx cutoff above.
  ao = mix(1.0, ao, smoothstep(1.5, 4.0, uRadius * uProjScale / depth));
  fragColor = vec4(ao, ao, ao, 1.0);
}
