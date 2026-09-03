#version 410 core
// The one forward-shading material program every opaque object in the scene
// uses. Nothing in this project ships a texture asset, so every surface —
// armour, terrain, bare rock — is shaded procedurally from world position
// and world normal (triplanar-projected noise) rather than sampled from a
// canvas-baked map the way the browser build's materials.js does it. Same
// PBR rules, different mechanism for getting there.
//
// Implements, in order: (1) metallic/roughness PBR with anisotropic
// reflections and screen-space specular anti-aliasing; (2) two-layer
// procedural stone terrain with a fine detail bump; (3) three cascaded
// shadow maps, cross-faded by distance; (4) image-based lighting sampled
// from the prefiltered room cubemap.

in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vViewPos;
in float vBlend;

out vec4 fragColor;

uniform vec3 uCamPos;

uniform vec3 uSunDir;         // direction the light *travels*, world space
uniform vec3 uSunColour;
uniform float uSunIntensity;

struct Cascade { mat4 viewProj; vec2 range; };
uniform Cascade uCascade[3];
uniform float uCascadeFade;
uniform sampler2DShadow uCascadeMap0;
uniform sampler2DShadow uCascadeMap1;
uniform sampler2DShadow uCascadeMap2;

uniform samplerCube uIrradianceMap;
uniform float uIblMaxMip;

uniform int uMaterial;        // MaterialType: 0 armour, 1 terrain, 2 emissive, 3 rock
uniform vec3 uTint;
uniform float uMetallic;
uniform float uRoughness;
uniform float uWear;          // armour: how chipped the paint is, 0..~1.5
uniform vec3 uEmissive;
uniform float uEmissiveIntensity;
uniform float uAniso;         // 0 = isotropic reflections

const float PI = 3.14159265359;

// ---------------------------------------------------------------- noise
// Hash-based value noise and fbm, the same construction used to paint the
// terrain's per-vertex blend weight on the C++ side (Mesh::terrainPlane) —
// kept identical here so the vertex-painted patches and the fragment-shaded
// detail agree with each other.
float hash21(vec2 p) {
  p = fract(p * vec2(127.1, 311.7));
  p += dot(p, p + 34.23);
  return fract(p.x * p.y);
}
float valueNoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash21(i), b = hash21(i + vec2(1, 0));
  float c = hash21(i + vec2(0, 1)), d = hash21(i + vec2(1, 1));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
// Analytic noise has no mipmap chain to fall back on, so evaluating it at a
// fixed frequency all the way to the horizon aliases into exactly the
// salt-and-pepper static a texture would show with filtering turned off.
// Each octave is faded towards its own flat average — 0.5 — once the
// screen-space footprint of one fragment (fwidth) grows past that octave's
// cell size, the same role a mip chain plays for a sampled texture.
float fbm(vec2 p) {
  const float amps[3] = float[3](0.55, 0.28, 0.17);
  const float freqs[3] = float[3](1.0, 2.3, 5.1);
  float sum = 0.0;
  for (int i = 0; i < 3; i++) {
    vec2 pf = p * freqs[i];
    float footprint = fwidth(pf.x) + fwidth(pf.y);
    float fade = clamp(1.5 - footprint, 0.0, 1.0);
    sum += mix(0.5, valueNoise(pf), fade) * amps[i];
  }
  return sum;
}

// Triplanar projection weights: each of the three axis-aligned projections
// contributes in proportion to how much the normal faces that axis, sharpened
// so the blend between faces is narrow rather than a smear across a cube.
vec3 triplanarWeights(vec3 n) {
  vec3 w = pow(abs(n), vec3(5.0));
  return w / max(w.x + w.y + w.z, 1e-5);
}

float triplanarHeight(vec3 p, vec3 w, float scale) {
  float hx = fbm(p.yz * scale);
  float hy = fbm(p.xz * scale);
  float hz = fbm(p.xy * scale);
  return hx * w.x + hy * w.y + hz * w.z;
}

// Analytic bump: the world-space gradient of a triplanar height field,
// subtracted from the normal (Blinn's method) rather than sampled from a
// baked normal map. w is fixed across the six taps so the blend itself
// doesn't wobble as the sample position is perturbed.
vec3 bumpedNormal(vec3 n, vec3 p, float scale, float strength) {
  vec3 w = triplanarWeights(n);

  // The finite-difference step has to be small relative to fbm's *finest*
  // wavelength (its highest octave runs at 5.1x scale), or the two samples
  // straddle more than one noise period and the "derivative" is just the
  // difference between two uncorrelated points — numerical noise with no
  // relationship to the true slope, amplified further by strength below.
  // A fixed step this project first shipped with (4cm) was larger than that
  // finest wavelength for every detail-scale bump call, which is what
  // turned the ground and every plate edge into per-pixel static regardless
  // of distance — this was not aliasing, it was measuring the wrong thing.
  float eps = 0.12 / max(scale * 5.1, 1.0);

  float h0 = triplanarHeight(p, w, scale);
  float hx = triplanarHeight(p + vec3(eps, 0, 0), w, scale) - h0;
  float hy = triplanarHeight(p + vec3(0, eps, 0), w, scale) - h0;
  float hz = triplanarHeight(p + vec3(0, 0, eps), w, scale) - h0;
  vec3 grad = vec3(hx, hy, hz) / eps;
  // keep only the part of the gradient across the surface, not along it
  grad -= n * dot(grad, n);

  // With the step size corrected, this now does what mipmapping does for a
  // real normal map: once a fragment's own screen-space footprint grows
  // past the bump's period — far away, or at a grazing angle — fade the
  // perturbation out rather than let it alias.
  float footprint = fwidth(p.x) + fwidth(p.y) + fwidth(p.z);
  float fade = clamp(1.0 - footprint * scale, 0.0, 1.0);

  return normalize(n - grad * strength * fade);
}

// ---------------------------------------------------------------- armour
// A panel grid projected triplanar, with seams, edge wear (bare metal
// showing through the paint near a seam) and chip splotches — the
// procedural equivalent of materials.js's armourPlate() canvas texture.
struct ArmourSample { vec3 albedo; float metallic; float roughness; vec3 bumpN; };

float panelSeam(vec2 uv, float cell) {
  vec2 g = abs(fract(uv / cell) - 0.5) * 2.0;   // 0 at a seam, 1 mid-panel
  return 1.0 - smoothstep(0.0, 0.12, min(g.x, g.y));
}

ArmourSample shadeArmour(vec3 p, vec3 n, vec3 tint, float baseMetallic, float baseRoughness, float wear) {
  vec3 w = triplanarWeights(n);
  float cell = 0.55;

  vec2 uvX = p.yz, uvY = p.xz, uvZ = p.xy;
  float seam = panelSeam(uvX, cell) * w.x + panelSeam(uvY, cell) * w.y + panelSeam(uvZ, cell) * w.z;

  // per-panel colour drift, and chips that punch through to bare metal —
  // both keyed off the panel's own cell id so they read as belonging to
  // one plate rather than floating independently of the seams.
  vec2 cellIdX = floor(uvX / cell), cellIdY = floor(uvY / cell), cellIdZ = floor(uvZ / cell);
  float drift = (hash21(cellIdX) - 0.5) * w.x + (hash21(cellIdY) - 0.5) * w.y + (hash21(cellIdZ) - 0.5) * w.z;

  float fine = triplanarHeight(p, w, 6.0);
  float chip = smoothstep(0.55, 0.9, fine + seam * 0.4) * clamp(wear, 0.0, 1.6);

  vec3 bare = mix(tint, vec3(0.72, 0.74, 0.77), 0.7);
  vec3 albedo = mix(tint * (1.0 + drift * 0.12), bare, chip * 0.6);
  albedo *= (1.0 - seam * 0.35);  // grime collecting in the seam

  ArmourSample s;
  s.albedo = albedo;
  // Paint is a dielectric, the metal underneath it is not: chips push
  // metallic up towards the house value; seams are slightly rougher, where
  // grime sits.
  s.metallic = clamp(mix(baseMetallic * 0.55, baseMetallic, chip), 0.0, 1.0);
  s.roughness = clamp(baseRoughness + seam * 0.18 + fine * 0.06, 0.03, 1.0);
  s.bumpN = bumpedNormal(n, p, 1.4, 0.30 + seam * 0.35);
  return s;
}

// ---------------------------------------------------------------- terrain
// Two stone "layers" (a coarse base and a finer, differently tinted one),
// blended by the per-vertex weight painted on the C++ side, plus a tight
// detail layer folded in with a whiteout normal blend so the ground still
// shows grain up close whatever the two big layers are doing.
vec3 shadeTerrain(vec3 p, vec3 n, vec3 baseTint, float blend, out vec3 bumpN, out float roughOut) {
  vec3 w = triplanarWeights(n);
  float base = triplanarHeight(p, w, 0.35);
  float layer = triplanarHeight(p * 1.7, w, 0.9);

  vec3 tintA = baseTint;
  vec3 tintB = baseTint * vec3(1.12, 1.04, 0.92) + vec3(0.03);
  vec3 albedo = mix(tintA * (0.82 + base * 0.36), tintB * (0.82 + layer * 0.36), blend);

  // detail bump: much tighter frequency, always present regardless of blend
  vec3 nA = bumpedNormal(n, p, 0.35, 0.9);
  vec3 nB = bumpedNormal(n, p * 1.7, 0.9, 1.0);
  vec3 coarse = normalize(mix(nA, nB, blend));
  vec3 detail = bumpedNormal(n, p, 1.6, 0.45);
  // whiteout blend: add the two tangent-ish perturbations and renormalise,
  // which keeps both sets of bumps instead of the weaker one washing out.
  bumpN = normalize(coarse + detail - n);

  roughOut = clamp(0.90 + (fbm(p.xz * 0.4) - 0.5) * 0.08, 0.75, 1.0);
  return albedo;
}

// ---------------------------------------------------------- anisotropy
// Bend the reflection vector towards a grain axis before sampling the
// environment, so a highlight stretches along the grain instead of
// reflecting as an isotropic round blob — Kaplanyan's construction, same
// as the browser build's shading.js.
vec3 anisoBendReflection(vec3 n, vec3 v, vec3 grainAxisWorld, float strength) {
  vec3 t = grainAxisWorld - n * dot(grainAxisWorld, n);
  float tl = length(t);
  if (tl < 0.08) return reflect(-v, n);
  t /= tl;
  vec3 b = normalize(cross(n, t));
  vec3 bentN = normalize(mix(n, normalize(cross(b, cross(v, b))), strength));
  return reflect(-v, bentN);
}

// Karis's analytic environment-BRDF approximation — a cheap stand-in for
// sampling a precomputed split-sum LUT, accurate enough that the grazing-
// angle brightening it exists for is still visible.
vec2 envBRDFApprox(float NoV, float roughness) {
  const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
  const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
  vec4 r = roughness * c0 + c1;
  float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
  return vec2(-1.04, 1.04) * a004 + r.zw;
}

// ---------------------------------------------------------------- BRDF
float distributionGGX(float NoH, float roughness) {
  float a = roughness * roughness;
  float a2 = a * a;
  float d = NoH * NoH * (a2 - 1.0) + 1.0;
  return a2 / max(PI * d * d, 1e-6);
}
float geometrySmith(float NoV, float NoL, float roughness) {
  float k = (roughness + 1.0); k = (k * k) / 8.0;
  float gv = NoV / (NoV * (1.0 - k) + k);
  float gl = NoL / (NoL * (1.0 - k) + k);
  return gv * gl;
}
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// ------------------------------------------------------------- shadows
float sampleCascadePCF(sampler2DShadow map, vec4 lightClip, vec2 texel) {
  vec3 proj = lightClip.xyz / lightClip.w;
  proj = proj * 0.5 + 0.5;
  // A small constant bias so a surface doesn't shadow itself: the compare
  // value is nudged towards the light, which is far cheaper than a normal-
  // offset bias and adequate at these cascade resolutions.
  float ref = min(proj.z - 0.0006, 1.0);
  float sum = 0.0;
  for (int x = -1; x <= 1; x++) {
    for (int y = -1; y <= 1; y++) {
      sum += texture(map, vec3(proj.xy + vec2(x, y) * texel, ref));
    }
  }
  return sum / 9.0;
}

float shadowFactor(float viewDepth) {
  float wSum = 0.0, sSum = 0.0;
  vec2 texel0 = vec2(1.0 / 2048.0), texel1 = vec2(1.0 / 2048.0), texel2 = vec2(1.0 / 1024.0);

  float w0 = smoothstep(uCascade[0].range.x - uCascadeFade, uCascade[0].range.x + uCascadeFade, viewDepth) *
             (1.0 - smoothstep(uCascade[0].range.y - uCascadeFade, uCascade[0].range.y + uCascadeFade, viewDepth));
  float w1 = smoothstep(uCascade[1].range.x - uCascadeFade, uCascade[1].range.x + uCascadeFade, viewDepth) *
             (1.0 - smoothstep(uCascade[1].range.y - uCascadeFade, uCascade[1].range.y + uCascadeFade, viewDepth));
  float w2 = smoothstep(uCascade[2].range.x - uCascadeFade, uCascade[2].range.x + uCascadeFade, viewDepth) *
             (1.0 - smoothstep(uCascade[2].range.y - uCascadeFade, uCascade[2].range.y + uCascadeFade, viewDepth));

  if (w0 > 0.001) { sSum += w0 * sampleCascadePCF(uCascadeMap0, uCascade[0].viewProj * vec4(vWorldPos, 1.0), texel0); wSum += w0; }
  if (w1 > 0.001) { sSum += w1 * sampleCascadePCF(uCascadeMap1, uCascade[1].viewProj * vec4(vWorldPos, 1.0), texel1); wSum += w1; }
  if (w2 > 0.001) { sSum += w2 * sampleCascadePCF(uCascadeMap2, uCascade[2].viewProj * vec4(vWorldPos, 1.0), texel2); wSum += w2; }
  return wSum > 0.001 ? sSum / wSum : 1.0;
}

void main() {
  vec3 N = normalize(vNormal);
  vec3 V = normalize(uCamPos - vWorldPos);

  vec3 albedo = uTint;
  float metallic = uMetallic;
  float roughness = uRoughness;

  if (uMaterial == 2) {
    // Emissive: unlit, straight into the HDR buffer for bloom to pick up.
    fragColor = vec4(uEmissive * uEmissiveIntensity, 1.0);
    return;
  } else if (uMaterial == 0) {
    ArmourSample s = shadeArmour(vWorldPos, N, uTint, uMetallic, uRoughness, uWear);
    albedo = s.albedo; metallic = s.metallic; roughness = s.roughness; N = s.bumpN;
  } else if (uMaterial == 1) {
    vec3 bumpN; float roughOut;
    albedo = shadeTerrain(vWorldPos, N, uTint, vBlend, bumpN, roughOut);
    N = bumpN; roughness = roughOut; metallic = 0.0;
  } else {  // Rock
    vec3 w = triplanarWeights(N);
    float h = triplanarHeight(vWorldPos, w, 0.6);
    albedo = uTint * (0.8 + h * 0.5);
    N = bumpedNormal(N, vWorldPos, 2.4, 1.1);
    roughness = clamp(uRoughness + (fbm(vWorldPos.xz) - 0.5) * 0.06, 0.6, 1.0);
    metallic = 0.0;
  }

  // Specular anti-aliasing: a sharp reflection (low roughness) narrower
  // than a pixel's normal variance crawls with aliasing under a bump-mapped
  // surface; widen roughness by how fast the shading normal is changing
  // across the pixel's own footprint. Same construction as shading.js.
  vec3 dNx = dFdx(N), dNy = dFdy(N);
  float variance = 0.5 * (dot(dNx, dNx) + dot(dNy, dNy));
  float widen = min(2.0 * variance, 0.18);
  roughness = sqrt(clamp(roughness * roughness + widen, 0.0, 1.0));

  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  float NoV = max(dot(N, V), 1e-4);

  // ---------------- direct: the sun ----------------
  vec3 L = normalize(-uSunDir);
  vec3 H = normalize(V + L);
  float NoL = max(dot(N, L), 0.0);
  float NoH = max(dot(N, H), 0.0);
  float VoH = max(dot(V, H), 0.0);

  float shadow = NoL > 0.0 ? shadowFactor(-vViewPos.z) : 1.0;

  vec3 direct = vec3(0.0);
  if (NoL > 0.0) {
    float D = distributionGGX(NoH, roughness);
    float G = geometrySmith(NoV, NoL, roughness);
    vec3 F = fresnelSchlick(VoH, F0);
    vec3 spec = (D * G * F) / max(4.0 * NoV * NoL, 1e-4);
    vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
    vec3 sunRadiance = uSunColour * uSunIntensity;
    direct = (kd * albedo / PI + spec) * sunRadiance * NoL * shadow;
  }

  // ---------------- indirect: image-based lighting ----------------
  vec3 Fibl = fresnelSchlick(NoV, F0);
  vec3 kdIbl = (vec3(1.0) - Fibl) * (1.0 - metallic);
  vec3 irradiance = textureLod(uIrradianceMap, N, uIblMaxMip * 0.92).rgb;
  vec3 diffuseIbl = irradiance * albedo * kdIbl;

  vec3 R = uAniso > 0.001
    ? anisoBendReflection(N, V, vec3(0.0, 1.0, 0.0), uAniso)
    : reflect(-V, N);
  vec3 prefiltered = textureLod(uIrradianceMap, R, roughness * uIblMaxMip).rgb;
  vec2 ab = envBRDFApprox(NoV, roughness);
  vec3 specularIbl = prefiltered * (F0 * ab.x + ab.y);

  vec3 ambient = diffuseIbl + specularIbl;

  fragColor = vec4(direct + ambient, 1.0);
}
