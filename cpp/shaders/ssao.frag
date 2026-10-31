#version 410 core
// Half-resolution screen-space ambient occlusion. Reconstructs view-space
// position from the depth buffer and samples a hemisphere of neighbours to
// estimate how enclosed each fragment is. The kernel is rotated per-pixel
// by a hash-based noise pattern so the result can be smoothed by a cheap
// bilateral blur rather than needing dozens of samples.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tDepth;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform float uNear;
uniform float uFar;
uniform vec2 uTexelSize;
uniform float uRadius;
uniform float uBias;
uniform float uIntensity;

float linearDepth(float d) {
  float ndc = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - ndc * (uFar - uNear));
}

vec3 viewPosFromDepth(vec2 uv) {
  float d = texture(tDepth, uv).r;
  vec4 ndc = vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
  vec4 vp = uInvProj * ndc;
  return vp.xyz / vp.w;
}

// Hash-based pseudo-random — no noise texture needed.
float hash12(vec2 p) {
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}
vec2 hash22(vec2 p) {
  return vec2(hash12(p), hash12(p + vec2(47.17, 113.43)));
}

// Fibonacci hemisphere: deterministic, well-distributed sample directions.
const int SAMPLES = 16;
vec3 fibSample(int i, float jitter) {
  float t = (float(i) + 0.5) / float(SAMPLES);
  float phi = (float(i) + jitter) * 2.399963;  // golden angle
  float cosTheta = 1.0 - t;
  float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
  return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

void main() {
  float depth01 = texture(tDepth, vUv).r;
  if (depth01 >= 0.99999) { fragColor = vec4(1.0); return; }

  vec3 fragPos = viewPosFromDepth(vUv);
  // Reconstruct the view-space normal from depth derivatives.
  vec3 dPdx = dFdx(fragPos);
  vec3 dPdy = dFdy(fragPos);
  vec3 normal = normalize(cross(dPdx, dPdy));

  // Per-pixel rotation from screen-space hash.
  vec2 noise = hash22(gl_FragCoord.xy) * 2.0 - 1.0;
  // Build a TBN that orients the hemisphere along the reconstructed normal,
  // then rotated by the noise angle so the pattern varies per pixel.
  vec3 tangent = normalize(noise.x * dPdx + noise.y * dPdy);
  tangent = normalize(tangent - normal * dot(tangent, normal));
  vec3 bitangent = cross(normal, tangent);
  mat3 tbn = mat3(tangent, bitangent, normal);

  float ao = 0.0;
  float jitter = hash12(gl_FragCoord.xy + vec2(0.37, 0.91));
  float rScale = uRadius / float(SAMPLES);

  for (int i = 0; i < SAMPLES; i++) {
    vec3 sampleDir = tbn * fibSample(i, jitter);
    // Place the sample along the hemisphere at a distance that grows with
    // index, so near-field occlusion is sampled more densely.
    float scale = float(i + 1) * rScale;
    vec3 samplePos = fragPos + sampleDir * scale;

    // Project to screen to read depth there.
    vec4 offset = uProj * vec4(samplePos, 1.0);
    offset.xy = (offset.xy / offset.w) * 0.5 + 0.5;

    if (offset.x < 0.0 || offset.x > 1.0 || offset.y < 0.0 || offset.y > 1.0)
      continue;

    float sampleDepth = viewPosFromDepth(offset.xy).z;

    // Range check: ignore samples that are too far behind the surface —
    // they belong to a different object and would darken edges wrongly.
    float rangeCheck = smoothstep(0.0, 1.0, uRadius / max(abs(fragPos.z - sampleDepth), 0.001));
    ao += (sampleDepth >= samplePos.z + uBias ? 1.0 : 0.0) * rangeCheck;
  }

  ao = 1.0 - (ao / float(SAMPLES)) * uIntensity;
  fragColor = vec4(vec3(clamp(ao, 0.0, 1.0)), 1.0);
}
