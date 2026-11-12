#version 410 core
// The one forward-shading material program every opaque object in the scene
// uses. Nothing in this project ships a texture asset, so every surface is
// shaded procedurally from position and normal rather than sampled from a
// map.
//
// What it does, in the order main() does it:
//
//   1. the surface   — what the thing is made of at this point: colour,
//                      metalness, roughness and the small-scale normal. Boxes
//                      get their edges rounded and worn; walls get seams,
//                      streaks, dirt at the foot and dust on top; the ground
//                      gets lumps, grit and standing water.
//   2. the sun       — GGX specular and Lambert diffuse, under three cascaded
//                      shadow maps with penumbrae that widen with distance
//                      from whatever cast them.
//   3. local lights  — everything in the scene that glows also lights what
//                      is near it (see Renderer::gatherLights).
//   4. the fill      — image-based lighting from the probe, held back in
//                      creases by screen-space ambient occlusion.
//   5. the air       — height fog, lit where the sun reaches it and dark
//                      where something stands in the way.

in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vViewPos;
in float vBlend;
in vec3 vLocalPos;
in vec3 vLocalNormal;

out vec4 fragColor;

uniform vec3 uCamPos;

uniform vec3 uSunDir;         // direction the light *travels*, world space
uniform vec3 uSunColour;
uniform float uSunIntensity;

struct Cascade { mat4 viewProj; vec2 range; };
uniform Cascade uCascade[3];
// Per cascade: x = one texel in uv, y = the width of the map in metres,
// z = the depth it spans in metres. A penumbra is a size in the world, and
// these are what turn it into a size in the map.
uniform vec3 uCascadeInfo[3];
uniform float uCascadeFade;
uniform sampler2DShadow uCascadeMap0;
uniform sampler2DShadow uCascadeMap1;
uniform sampler2DShadow uCascadeMap2;
// The same three depth textures again, bound through a sampler that returns
// the stored depth instead of comparing against it. Finding how far away a
// shadow's caster is needs the number; filtering the shadow needs the
// comparison; one texture can do both if it is bound twice.
uniform sampler2D uCascadeRaw0;
uniform sampler2D uCascadeRaw1;
uniform sampler2D uCascadeRaw2;
uniform int uSoftShadows;     // 1 = contact-hardening, 0 = fixed 3x3

uniform samplerCube uIrradianceMap;
uniform samplerCube uSpecMap;
uniform float uIblMaxMip;
uniform float uIblIntensity;
// A flat fill added on top of the probe, for scenes that want *some*
// bounce light without the probe's shape.
uniform vec3 uAmbientFill;

// Screen-space inputs worked out from the depth pre-pass before this pass
// runs: how open each pixel is to the sky (ssao.frag), and how much of the
// haze in front of it is in sunlight (volumetric.frag).
uniform sampler2D uAoMap;
uniform sampler2D uShaftMap;
uniform vec2 uInvScreen;
uniform float uFrame;         // advances only while temporal AA is resolving

// Everything that glows, as lights. xyz + range; colour; and for a long thin
// emitter, half its length along its own axis — see evalLocalLights.
const int MAX_LIGHTS = 16;
uniform int uLightCount;
uniform vec4 uLightPos[MAX_LIGHTS];
uniform vec4 uLightColour[MAX_LIGHTS];
uniform vec4 uLightAxis[MAX_LIGHTS];

uniform float uFogDensity;
uniform float uFogFalloff;
uniform float uFogBase;
uniform vec3 uFogColour;
uniform float uInscatter;

uniform int uMaterial;        // MaterialType: 0 armour, 1 terrain, 2 emissive,
                              //               3 rock, 4 planet
uniform vec3 uTint;
uniform float uMetallic;
uniform float uRoughness;
uniform float uWear;          // armour: how chipped the paint is, 0..~1.5
uniform vec3 uTint2;          // planet: the colour continents mix toward
uniform float uCapExtent;     // planet: how far the polar caps reach, 0 = none
uniform vec3 uEmissive;
uniform float uEmissiveIntensity;
uniform float uAniso;         // 0 = isotropic reflections

// The mesh's own frame. uBoxHalf is the half-size of a box mesh in its own
// units and zero for anything that is not a box; uObjScale is the model
// matrix's scale and uObjRot its rotation, so vLocalPos * uObjScale is the
// fragment's position on the object in metres.
uniform vec3 uBoxHalf;
uniform vec3 uObjScale;
uniform mat3 uObjRot;
// 1 = the surface detail is fixed to the object (anything that moves),
// 0 = fixed to the world (anything that does not, so neighbouring walls
// share one pattern across the join).
uniform int uLocalShading;
uniform float uGrime;         // 0..1: streaks, dirt at the foot, dust on top
uniform vec3 uDustColour;     // the ground's own colour, which is what dust is
uniform float uWetness;       // terrain: 0 dry .. 1 standing water everywhere low

const float PI = 3.14159265359;

// ---------------------------------------------------------------- noise
float hash21(vec2 p) {
  p = fract(p * vec2(127.1, 311.7));
  p += dot(p, p + 34.23);
  return fract(p.x * p.y);
}

// Interleaved gradient noise (Jimenez): a different value at every pixel
// with no visible structure, which is what you want to turn a fixed sampling
// pattern by so its banding becomes grain instead.
float ign(vec2 px) {
  return fract(52.9829189 * fract(dot(px, vec2(0.06711056, 0.00583715))));
}

// Value noise that also returns its own slope. The slope is what bump
// mapping needs, and getting it here — from the same four corner values,
// exactly — is what replaced estimating it by sampling the noise three more
// times a small step away. That estimate was the source of this shader's
// first real bug (docs/NATIVE_RENDERER.md, "What broke"): a step that is
// wrong for the frequency measures nothing. An exact derivative has no step.
vec3 noised(vec2 x) {
  vec2 i = floor(x), f = fract(x);
  vec2 u = f * f * (3.0 - 2.0 * f);
  vec2 du = 6.0 * f * (1.0 - f);
  float a = hash21(i), b = hash21(i + vec2(1, 0));
  float c = hash21(i + vec2(0, 1)), d = hash21(i + vec2(1, 1));
  float k1 = b - a, k2 = c - a, k4 = a - b - c + d;
  return vec3(a + k1 * u.x + k2 * u.y + k4 * u.x * u.y,
              du * vec2(k1 + k4 * u.y, k2 + k4 * u.x));
}

// Fractal noise with its gradient. x = height in 0..1, yz = d(height)/dp.
//
// `footprint` is how much of p one pixel covers. Analytic noise has no mip
// chain, so each octave is faded to its own average once a pixel is wider
// than its cells — which is what a mip chain does for a texture, and what
// stops the far ground turning to static. The slope fades sooner than the
// height does: a normal that changes several times inside one pixel aliases
// in the highlight long before the colour does.
vec3 fbmD(vec2 p, float footprint, int octaves) {
  const mat2 turn = mat2(0.8, -0.6, 0.6, 0.8);
  mat2 m = mat2(1.0);
  float amp = 0.5, freq = 1.0, norm = 0.0;
  vec3 sum = vec3(0.0);
  for (int i = 0; i < octaves; i++) {
    float cover = footprint * freq;
    float fadeH = clamp(1.5 - cover * 1.4, 0.0, 1.0);
    float fadeG = clamp(1.25 - cover * 2.6, 0.0, 1.0);
    if (fadeH > 0.0) {
      vec3 n = noised(m * p + float(i) * vec2(17.3, 9.1));
      sum.x += amp * mix(0.5, n.x, fadeH);
      sum.yz += amp * fadeG * (transpose(m) * n.yz);
    } else {
      sum.x += amp * 0.5;
    }
    norm += amp;
    m = turn * m * 2.03;
    freq *= 2.03;
    amp *= 0.5;
  }
  return sum / norm;
}

// Triplanar projection weights: each of the three axis-aligned projections
// contributes in proportion to how much the normal faces that axis, sharpened
// so the blend between faces is narrow rather than a smear across a cube.
vec3 triplanarWeights(vec3 n) {
  vec3 w = pow(abs(n), vec3(5.0));
  return w / max(w.x + w.y + w.z, 1e-5);
}

// How much of each projection plane one pixel covers: x for the plane seen
// along X (its yz), y for the plane seen along Y (its xz), z likewise.
//
// A pixel on ground seen at a shallow angle is not a square of ground. It is
// a sliver: a few millimetres across and tens of centimetres deep. Fading
// detail by the long side — the obvious thing, and the first thing this did
// — throws away everything finer than the sliver is long, and the ground in
// front of you turns to smears pointing at the horizon. Fading by the short
// side keeps the detail and lets it alias along the long one. The geometric
// mean of the two, with the sliver never treated as thinner than a quarter
// of its length, is the compromise an anisotropic texture filter makes.
vec3 planeFootprints(vec3 p) {
  vec3 dx = dFdx(p), dy = dFdy(p);
  vec3 fp;
  float a, b;
  a = length(dx.yz); b = length(dy.yz);
  fp.x = sqrt(max(a, b) * max(min(a, b), max(a, b) * 0.25));
  a = length(dx.xz); b = length(dy.xz);
  fp.y = sqrt(max(a, b) * max(min(a, b), max(a, b) * 0.25));
  a = length(dx.xy); b = length(dy.xy);
  fp.z = sqrt(max(a, b) * max(min(a, b), max(a, b) * 0.25));
  return fp;
}

// A triplanar height field and its gradient in 3D. A projection that
// contributes almost nothing is not evaluated at all — on the flat,
// axis-aligned faces most of this world is built from that is two of the
// three, every time. `fp` is planeFootprints() of p.
void triplanarD(vec3 p, vec3 w, vec3 fp, float scale, int octaves, out float h, out vec3 g) {
  h = 0.0; g = vec3(0.0);
  float used = 0.0;
  if (w.x > 0.02) {
    vec3 n = fbmD(p.yz * scale, fp.x * scale, octaves);
    h += w.x * n.x; g += w.x * vec3(0.0, n.y, n.z) * scale; used += w.x;
  }
  if (w.y > 0.02) {
    vec3 n = fbmD(p.xz * scale + 31.7, fp.y * scale, octaves);
    h += w.y * n.x; g += w.y * vec3(n.y, 0.0, n.z) * scale; used += w.y;
  }
  if (w.z > 0.02) {
    vec3 n = fbmD(p.xy * scale + 67.1, fp.z * scale, octaves);
    h += w.z * n.x; g += w.z * vec3(n.y, n.z, 0.0) * scale; used += w.z;
  }
  h /= max(used, 1e-4);
  g /= max(used, 1e-4);
}

// Tilt a normal by a height gradient (Blinn): only the part of the slope
// that runs across the surface counts.
vec3 perturb(vec3 n, vec3 grad) {
  return normalize(n - (grad - n * dot(grad, n)));
}

// ---------------------------------------------------------------- planets
// A planet is shaded from its surface *direction*, not its world position.
// The world-position noise every other material uses is tuned for
// metre-scale detail, and on a body five hundred units across it smears
// into a few enormous flat patches. Direction-space noise gives features
// sized in fractions of the globe regardless of how big the globe is.
float hash31(vec3 p) {
  p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
  p *= 17.0;
  return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}
float valueNoise3(vec3 p) {
  vec3 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float n000 = hash31(i), n100 = hash31(i + vec3(1, 0, 0));
  float n010 = hash31(i + vec3(0, 1, 0)), n110 = hash31(i + vec3(1, 1, 0));
  float n001 = hash31(i + vec3(0, 0, 1)), n101 = hash31(i + vec3(1, 0, 1));
  float n011 = hash31(i + vec3(0, 1, 1)), n111 = hash31(i + vec3(1, 1, 1));
  return mix(mix(mix(n000, n100, f.x), mix(n010, n110, f.x), f.y),
             mix(mix(n001, n101, f.x), mix(n011, n111, f.x), f.y), f.z);
}
float fbm3(vec3 p) {
  float sum = 0.0, amp = 0.5, freq = 1.0;
  for (int i = 0; i < 5; i++) {
    sum += valueNoise3(p * freq) * amp;
    freq *= 2.07;
    amp *= 0.5;
  }
  return sum;
}

// A whole world, shaded from the surface direction. Continents come from
// direction-space fbm, so their size is a fraction of the globe rather than
// a fixed number of metres; latitude gives the bands and the polar caps.
vec3 shadePlanet(vec3 n, vec3 lowTint, vec3 highTint, float capExtent, out float roughOut) {
  vec3 dir = normalize(n);

  // Continents: two scales, the coarse one deciding land from sea and the
  // fine one breaking up the coastline so it isn't a smooth blob.
  float coarse = fbm3(dir * 2.3);
  float fine = fbm3(dir * 7.5 + vec3(11.3));
  float land = clamp(coarse * 0.78 + fine * 0.22, 0.0, 1.0);

  // Banding by latitude, the way weather sorts itself on a real world.
  float lat = dir.y;
  float bands = 0.5 + 0.5 * sin(lat * 9.0 + fbm3(dir * 3.1) * 3.0);

  vec3 albedo = mix(lowTint, highTint, smoothstep(0.38, 0.62, land));
  albedo *= 0.88 + 0.24 * bands;

  // Polar caps: driven by latitude, with the noise let in at the edge so
  // the boundary is ragged rather than a drawn-on circle.
  if (capExtent > 0.001) {
    float capEdge = 1.0 - capExtent;
    float cap = smoothstep(capEdge, capEdge + 0.22, abs(lat) - (fine - 0.5) * 0.18);
    albedo = mix(albedo, vec3(0.92, 0.95, 1.0), clamp(cap, 0.0, 1.0));
    roughOut = mix(0.92, 0.55, clamp(cap, 0.0, 1.0));   // ice is smoother
    return albedo;
  }

  roughOut = 0.92;
  return albedo;
}

// ------------------------------------------------------------------ boxes
// Almost everything here is a box, and nothing gives a box away as computer
// graphics faster than its edges: perfectly sharp, so no light ever catches
// them, and perfectly clean, when the edge is the first place anything real
// gets knocked. A fragment on a box knows how far it is from each pair of
// faces, so both can be fixed in the shader without a single extra triangle.
//
// Returns the local-frame normal turned toward the neighbouring face within
// a bevel's width of an edge (45 degrees at the edge itself — a rounded
// corner, lit like one), and reports how near the nearest edge is.
vec3 boxEdges(vec3 nL, float pix, out float edgeDist, out float bevelWidth) {
  vec3 size = uBoxHalf * uObjScale * 2.0;
  vec3 toFace = (uBoxHalf - abs(vLocalPos)) * uObjScale;   // metres to each pair of faces

  vec3 an = abs(nL);
  float top = max(an.x, max(an.y, an.z));
  vec3 across = vec3(1.0) - step(top - 1e-3, an);          // the two axes that run along this face

  bevelWidth = clamp(0.16 * min(size.x, min(size.y, size.z)), 0.0015, 0.028);
  vec3 near = clamp(1.0 - toFace / bevelWidth, 0.0, 1.0) * across;

  // A bevel narrower than a pixel is a highlight narrower than a pixel,
  // which is a sparkle. Let it go flat with distance instead.
  float resolved = smoothstep(0.35, 1.3, bevelWidth / max(pix, 1e-5));

  edgeDist = min(mix(1e3, toFace.x, across.x), min(mix(1e3, toFace.y, across.y), mix(1e3, toFace.z, across.z)));
  return normalize(nL + sign(vLocalPos) * near * near * resolved);
}

// --------------------------------------------------------------- surfaces
struct Surface {
  vec3 albedo;
  float metallic;
  float roughness;
  vec3 n;          // shading normal, in the frame it was given p and n in
  float cavity;    // 1 = open, less in seams and at the foot of walls
};

// One projection of the panel grid: x = metres to the nearest seam, y = a
// random number for this panel, zw = which way is away from the seam.
vec4 panelGrid(vec2 uv, float cell) {
  vec2 c = uv / cell;
  vec2 f = fract(c) - 0.5;
  vec2 d = abs(f) * cell;
  float id = hash21(floor(c + 0.5));
  return d.x < d.y ? vec4(d.x, id, sign(f.x), 0.0) : vec4(d.y, id, 0.0, sign(f.y));
}

// Plate, panel and painted metal: the walls, the cover, the whole station,
// every hostile and the gun in your hands.
//
//   edgeDist   metres to the nearest edge of the box this is on (large if
//              it is not on one)
//   rise/drop  metres above the foot of the box / below its top
//   upness     how much the surface faces the sky, world space
Surface shadeBuilt(vec3 p, vec3 n, vec3 fp, float edgeDist, float rise, float drop, float upness) {
  vec3 w = triplanarWeights(n);
  float pix = dot(w, fp);
  // Something that moves is a smaller thing with smaller plates on it.
  float cell = uLocalShading == 1 ? 0.19 : 0.55;

  // ---- the panel grid: seams, and which panel this is
  float seamDist = 0.0, panelId = 0.0;
  vec3 seamAway = vec3(0.0);
  if (w.x > 0.02) { vec4 g = panelGrid(p.yz, cell); seamDist += w.x * g.x; panelId += w.x * g.y; seamAway += w.x * vec3(0.0, g.z, g.w); }
  if (w.y > 0.02) { vec4 g = panelGrid(p.xz, cell); seamDist += w.y * g.x; panelId += w.y * g.y; seamAway += w.y * vec3(g.z, 0.0, g.w); }
  if (w.z > 0.02) { vec4 g = panelGrid(p.xy, cell); seamDist += w.z * g.x; panelId += w.z * g.y; seamAway += w.z * vec3(g.z, g.w, 0.0); }

  // The grid is a step function with no filtering of its own. Far enough
  // away that several panels land in one pixel, a plated hull is just a
  // hull — the way a photographed one is once its seams stop resolving.
  float gridFade = clamp(1.0 - pix / cell * 2.8, 0.0, 1.0);

  // ---- three scales of irregularity
  float hBlotch, hChip, hGrain;
  vec3 gBlotch, gChip, gGrain;
  triplanarD(p + 3.7, w, fp, 1.15, 3, hBlotch, gBlotch);   // hand-sized to arm-sized: smudges, uneven paint
  triplanarD(p, w, fp, 6.0, 3, hChip, gChip);              // where the paint has gone
  triplanarD(p + 9.1, w, fp, 38.0, 2, hGrain, gGrain);     // the surface's own tooth

  // ---- paint, and what is under it
  float seamGrime = (1.0 - smoothstep(0.0, 0.035, seamDist)) * gridFade;
  float groove = (1.0 - smoothstep(0.0, 0.011, seamDist)) * gridFade;
  float drift = (panelId - 0.5) * gridFade;

  // Paint goes first where things get knocked: along the edges of the box
  // and beside the seams. The noise makes it ragged; the edge makes it
  // likely.
  float wear = clamp(uWear, 0.0, 1.6);
  float edgeWear = 1.0 - smoothstep(0.0, 0.012 + 0.03 * wear, edgeDist);
  float chipField = hChip + groove * 0.10 + edgeWear * (0.20 + 0.16 * wear);
  float chip = smoothstep(0.69, 0.75, chipField) * min(wear, 1.0);
  chip *= clamp(1.3 - pix * 6.0 * 2.2, 0.0, 1.0);

  vec3 paint = uTint * (1.0 + drift * 0.14) * (0.86 + 0.28 * hBlotch);
  // What shows through depends on what the thing is: bright metal under the
  // paint on a machine, and on a wall just the darker stuff the wall is made
  // of. Chipping a plaster wall to steel is what made every interior look
  // like it had been splashed with whitewash.
  float metalUnder = smoothstep(0.35, 0.75, uMetallic);
  vec3 bare = mix(uTint * 0.74 + vec3(0.015), mix(uTint, vec3(0.72, 0.74, 0.77), 0.7), metalUnder);
  vec3 albedo = mix(paint, bare, chip * 0.75);
  albedo *= 1.0 - seamGrime * 0.38;

  Surface s;
  // Paint is a dielectric, the metal underneath it is not.
  s.metallic = clamp(mix(uMetallic * 0.55, mix(uMetallic, max(uMetallic, 0.75), metalUnder), chip), 0.0, 1.0);
  // A real painted panel is never evenly glossy: it is handled, wiped,
  // rained on. The blotches carry that, and one panel is a little shinier
  // than the next.
  s.roughness = uRoughness + (hBlotch - 0.5) * 0.30 + drift * 0.10 + seamGrime * 0.22 - chip * 0.08;
  s.cavity = 1.0 - groove * 0.55;

  // ---- weather. Only on things that stand in it.
  if (uGrime > 0.001) {
    float wall = 1.0 - smoothstep(0.25, 0.7, abs(upness));
    // Dirt kicked up and damp drawn up from the ground.
    float foot = (1.0 - smoothstep(0.0, 0.30 + 0.55 * hBlotch, rise)) * wall;
    // Rain carries whatever is on top down the face in lines.
    float lane = noised(vec2((p.x + p.z) * 5.5, p.y * 0.22)).x;
    float streak = smoothstep(0.52, 0.80, lane) * (1.0 - smoothstep(0.05, 1.1 + 1.6 * hBlotch, drop)) * wall;
    streak *= clamp(1.3 - pix * 5.5 * 2.5, 0.0, 1.0);
    float grime = clamp(foot * 0.80 + streak * 0.50, 0.0, 1.0) * uGrime;
    albedo = mix(albedo, albedo * vec3(0.36, 0.33, 0.30), grime);
    s.roughness = mix(s.roughness, 0.92, grime * 0.85);
    s.metallic *= 1.0 - grime * 0.8;
    s.cavity *= 1.0 - foot * 0.35 * uGrime;

    // Dust settles on whatever faces up, and it is the colour of the ground.
    float dust = smoothstep(0.55, 0.95, upness) * (0.30 + 0.55 * hBlotch) * uGrime;
    albedo = mix(albedo, uDustColour * 1.25, dust * 0.60);
    s.roughness = mix(s.roughness, 0.95, dust * 0.7);
    s.metallic *= 1.0 - dust * 0.7;
  }

  s.albedo = albedo;
  s.roughness = clamp(s.roughness, 0.045, 1.0);

  // ---- the small-scale normal: sheet that is not quite flat, a surface
  // with tooth, and a groove at each seam with real sides to catch light.
  float grooveSlope = 0.0;
  {
    float t = clamp(seamDist / 0.011, 0.0, 1.0);
    grooveSlope = 6.0 * t * (1.0 - t) * 0.75 * gridFade * clamp(1.6 - pix / 0.011, 0.0, 1.0);
  }
  vec3 grad = gBlotch * 0.030 + gGrain * 0.0030 * (0.4 + s.roughness) + seamAway * grooveSlope;
  s.n = perturb(n, grad);
  return s;
}

// The ground. Three scales of the same thing — the lie of the land, lumps
// you could trip on, and grit — because ground that is detailed at only one
// scale reads as a pattern, and it is the mix that reads as ground.
Surface shadeTerrain(vec3 p, vec3 n, vec3 fp, vec3 baseTint, float blend) {
  vec3 w = triplanarWeights(n);

  float hLie, hLump, hGrit;
  vec3 gLie, gLump, gGrit;
  triplanarD(p, w, fp, 0.11, 3, hLie, gLie);
  triplanarD(p + 11.0, w, fp, 0.85, 4, hLump, gLump);
  triplanarD(p + 23.0, w, fp, 6.5, 3, hGrit, gGrit);

  // Two kinds of ground, in patches painted per vertex — with the lumps let
  // in at the boundary so one gives way to the other raggedly.
  // A terrain mesh carries that paint per vertex. A plain box used as
  // ground has no such thing, so it takes its patches from the lie of the
  // land instead, shaped the same way Mesh::terrainPlane shapes them.
  if (dot(uBoxHalf, uBoxHalf) > 0.0) blend = clamp((hLie - 0.44) * 3.2 + 0.5, 0.0, 1.0);
  float patchy = smoothstep(0.34, 0.66, blend + (hLump - 0.5) * 0.55);
  vec3 tintA = baseTint;
  vec3 tintB = baseTint * vec3(1.12, 1.04, 0.92) + vec3(0.03);
  vec3 albedo = mix(tintA, tintB, patchy);
  // Hollows hold the dark, fine material; high spots are scuffed pale.
  albedo *= 0.70 + 0.40 * hLump + 0.16 * hLie;
  albedo *= 0.74 + 0.52 * hGrit;
  // Stones: the top few percent of the grit, standing proud and paler.
  float stone = smoothstep(0.66, 0.74, hGrit);
  albedo = mix(albedo, albedo * 1.35 + vec3(0.02), stone * 0.7);

  Surface s;
  s.metallic = 0.0;
  s.roughness = clamp(0.90 + (hLump - 0.5) * 0.14, 0.72, 1.0);
  s.cavity = 0.80 + 0.20 * smoothstep(0.25, 0.60, hLump);
  vec3 bumped = perturb(n, gLie * 0.55 + gLump * 0.30 + gGrit * 0.050);

  // Standing water. It finds the low ground, which is exactly what the two
  // larger height fields already describe, so a puddle sits in a hollow and
  // has the hollow's shape rather than being a decal.
  if (uWetness > 0.01) {
    float basin = hLie * 0.62 + hLump * 0.38;
    float level = mix(0.34, 0.54, clamp(uWetness, 0.0, 1.0));
    float water = 1.0 - smoothstep(level - 0.030, level, basin);
    float damp = (1.0 - smoothstep(level - 0.02, level + 0.11, basin)) * min(uWetness * 1.5, 1.0);

    // Wet ground is darker and shinier well before it is under water.
    albedo *= mix(1.0, 0.58, damp);
    s.roughness = mix(s.roughness, 0.38, damp * 0.85);
    // Water lies flat whatever is under it, and it is a mirror.
    bumped = normalize(mix(bumped, n, water));
    s.roughness = mix(s.roughness, 0.03, water);
    albedo = mix(albedo, albedo * 0.42, water);
    s.cavity = mix(s.cavity, 1.0, water);
  }

  s.albedo = albedo;
  s.n = bumped;
  return s;
}

// ---------------------------------------------------------- anisotropy
// Bend the reflection vector towards a grain axis before sampling the
// environment, so a highlight stretches along the grain instead of
// reflecting as an isotropic round blob — Kaplanyan's construction.
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
// sampling a precomputed split-sum LUT.
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

// One light's worth of surface response: what leaves toward the eye per unit
// of light arriving along L, cosine included.
vec3 brdf(vec3 N, vec3 V, vec3 L, float NoV, vec3 albedo, float metallic, float roughness, vec3 F0) {
  float NoL = max(dot(N, L), 0.0);
  if (NoL <= 0.0) return vec3(0.0);
  vec3 H = normalize(V + L);
  float NoH = max(dot(N, H), 0.0);
  float VoH = max(dot(V, H), 0.0);
  float D = distributionGGX(NoH, roughness);
  float G = geometrySmith(NoV, NoL, roughness);
  vec3 F = fresnelSchlick(VoH, F0);
  vec3 spec = (D * G * F) / max(4.0 * NoV * max(NoL, 0.02), 1e-4);
  vec3 kd = (vec3(1.0) - F) * (1.0 - metallic);
  return (kd * albedo / PI + spec) * NoL;
}

// ------------------------------------------------------------- shadows
const vec2 POISSON[12] = vec2[12](
  vec2(-0.326, -0.406), vec2(-0.840, -0.074), vec2(-0.696,  0.457), vec2(-0.203,  0.621),
  vec2( 0.962, -0.195), vec2( 0.473, -0.480), vec2( 0.519,  0.767), vec2( 0.185, -0.893),
  vec2( 0.507,  0.064), vec2( 0.896,  0.412), vec2(-0.322, -0.933), vec2(-0.792, -0.598));

// The sun is not a point, so a shadow is not a line: its edge is as wide as
// the gap between what cast it and what it lands on, times the sun's width.
// A boot's shadow is sharp at the heel; a pillar's is soft thirty metres out.
// That is the single most recognisable thing about real daylight, and a
// fixed-width filter gets it wrong in both directions at once.
//
// So: look around in the map for what is nearer the sun than this point
// (the blocker search), take how much nearer on average, and filter over a
// disc that wide (percentage-closer soft shadows, Fernando 2005).
float sampleCascade(sampler2DShadow map, sampler2D raw, mat4 viewProj, vec3 info,
                    vec3 worldPos, vec3 geoN, float NoL, mat2 spin) {
  float texelWorld = info.x * info.y;
  // Lift the lookup off the surface along its normal by about a texel, more
  // as the light grazes it — a surface that is nearly edge-on to the sun
  // covers many texels of depth per texel of map, and shadows itself in
  // stripes unless it is moved out of its own way.
  vec3 p = worldPos + geoN * texelWorld * (1.2 + 2.2 * (1.0 - NoL));
  vec4 lc = viewProj * vec4(p, 1.0);
  vec3 proj = lc.xyz / lc.w * 0.5 + 0.5;
  float ref = min(proj.z - 0.0005, 1.0);

  // How steeply the surface runs away from the light. A filter disc laid on
  // a slope reaches points nearer the sun than its own centre, and without
  // allowing for that the ground finds itself to be its own blocker.
  float slope = min(sqrt(max(1.0 - NoL * NoL, 0.0)) / max(NoL, 0.08), 6.0);

  if (uSoftShadows == 0) {
    float sum = 0.0;
    for (int x = -1; x <= 1; x++) {
      for (int y = -1; y <= 1; y++) {
        sum += texture(map, vec3(proj.xy + vec2(x, y) * info.x, ref));
      }
    }
    return sum / 9.0;
  }

  const float SUN_TAN = 0.040;        // a wider sun than the real one: softer, kinder
  float searchWorld = 0.42;
  float searchUv = max(searchWorld / info.y, info.x * 2.0);
  float searchBias = min(searchUv * info.y * slope, 1.2) / info.z;

  float blockerSum = 0.0, blockers = 0.0;
  for (int i = 0; i < 8; i++) {
    float d = texture(raw, proj.xy + spin * POISSON[i] * searchUv).r;
    if (d < ref - searchBias) { blockerSum += d; blockers += 1.0; }
  }

  // Nothing clearly in the way within reach. That is either open ground or
  // the last few centimetres of a contact shadow, and the tight filter is
  // right for both.
  float radiusUv = info.x * 1.3;
  if (blockers > 0.5) {
    float gap = (ref - blockerSum / blockers) * info.z;
    float penumbra = clamp(gap * SUN_TAN, 0.0, 0.55);
    radiusUv = max(penumbra / info.y, info.x * 1.3);
  }
  float filterBias = min(radiusUv * info.y * slope * 0.6, 0.8) / info.z;

  float sum = 0.0;
  for (int i = 0; i < 12; i++) {
    sum += texture(map, vec3(proj.xy + spin * POISSON[i] * radiusUv, ref - filterBias));
  }
  return sum / 12.0;
}

float shadowFactor(float viewDepth, vec3 geoN, float NoL) {
  float wSum = 0.0, sSum = 0.0;

  // Turn the sampling disc by a different angle at every pixel (and, while
  // temporal AA is on, every frame). Twelve taps in the same place everywhere
  // is twelve overlapping copies of the shadow edge; turned, it is grain,
  // and grain is what the temporal resolve is there to average away.
  float angle = ign(gl_FragCoord.xy + uFrame * 5.588238) * 6.2831853;
  mat2 spin = mat2(cos(angle), sin(angle), -sin(angle), cos(angle));

  float w0 = smoothstep(uCascade[0].range.x - uCascadeFade, uCascade[0].range.x + uCascadeFade, viewDepth) *
             (1.0 - smoothstep(uCascade[0].range.y - uCascadeFade, uCascade[0].range.y + uCascadeFade, viewDepth));
  float w1 = smoothstep(uCascade[1].range.x - uCascadeFade, uCascade[1].range.x + uCascadeFade, viewDepth) *
             (1.0 - smoothstep(uCascade[1].range.y - uCascadeFade, uCascade[1].range.y + uCascadeFade, viewDepth));
  float w2 = smoothstep(uCascade[2].range.x - uCascadeFade, uCascade[2].range.x + uCascadeFade, viewDepth) *
             (1.0 - smoothstep(uCascade[2].range.y - uCascadeFade, uCascade[2].range.y + uCascadeFade, viewDepth));

  if (w0 > 0.001) { sSum += w0 * sampleCascade(uCascadeMap0, uCascadeRaw0, uCascade[0].viewProj, uCascadeInfo[0], vWorldPos, geoN, NoL, spin); wSum += w0; }
  if (w1 > 0.001) { sSum += w1 * sampleCascade(uCascadeMap1, uCascadeRaw1, uCascade[1].viewProj, uCascadeInfo[1], vWorldPos, geoN, NoL, spin); wSum += w1; }
  if (w2 > 0.001) { sSum += w2 * sampleCascade(uCascadeMap2, uCascadeRaw2, uCascade[2].viewProj, uCascadeInfo[2], vWorldPos, geoN, NoL, spin); wSum += w2; }
  return wSum > 0.001 ? sSum / wSum : 1.0;
}

// ------------------------------------------------------------ local lights
// Every emissive thing in the scene, as a light on what is near it. A strip
// light is not a point: the nearest point *on* it to the surface is what
// lights the surface, which is the difference between a pool of light under
// the middle of a strip and a band of it along the whole length.
vec3 evalLocalLights(vec3 P, vec3 N, vec3 V, float NoV, vec3 albedo, float metallic, float roughness, vec3 F0) {
  vec3 sum = vec3(0.0);
  for (int i = 0; i < uLightCount; i++) {
    vec3 centre = uLightPos[i].xyz;
    float range = uLightPos[i].w;
    vec3 axis = uLightAxis[i].xyz;
    vec3 toP = P - centre;
    float al = dot(axis, axis);
    vec3 src = centre;
    if (al > 1e-6) src += axis * clamp(dot(toP, axis) / al, -1.0, 1.0);

    vec3 Lv = src - P;
    float d2 = dot(Lv, Lv);
    if (d2 > range * range) continue;
    float d = sqrt(d2);
    // Inverse square, windowed so it reaches exactly zero at its range and a
    // light leaving the set cannot be seen to leave. The small constant is
    // the emitter's own size: nothing is infinitely bright at its surface.
    float window = 1.0 - d2 * d2 / (range * range * range * range);
    // A point's light falls off with the square of distance. A line's does
    // not, close to: standing under a long strip, moving twice as far away
    // halves the light rather than quartering it, because twice as much of
    // the strip is now roughly as near as its nearest point. This is the
    // exact fall-off opposite the middle of a line of half-length h, and it
    // becomes the inverse square by itself once you are far enough away for
    // the line to look like a point. Treating a thirty-metre strip as a
    // point at its nearest end burned a white hole in the ceiling above it.
    float atten = window * window / (sqrt(d2 + 0.06) * sqrt(d2 + al + 0.06));
    // A rough surface under a small light gets a hard little highlight that
    // no real lamp of any size would give it; widen it by the lamp's size.
    float r = clamp(roughness + 0.12 / (d + 0.3), 0.0, 1.0);
    sum += brdf(N, V, Lv / max(d, 1e-4), NoV, albedo, metallic, r, F0) * uLightColour[i].rgb * atten;
  }
  return sum;
}

// ---------------------------------------------------------------- the air
// The analytic integral of an exponential height-fog distribution along the
// view ray: haze pools in low ground and thins as you climb. `sunlit` is
// how much of the haze along this ray the sun actually reaches.
vec3 applyFog(vec3 col, vec3 worldPos, float sunlit) {
  if (uFogDensity <= 0.0) return col;
  vec3 ray = worldPos - uCamPos;
  float rayLen = length(ray);
  vec3 dir = ray / max(rayLen, 1e-4);

  float t = uFogFalloff * ray.y;
  float span = (abs(t) < 1e-3) ? rayLen : rayLen * (1.0 - exp(-t)) / t;
  float optical = uFogDensity * exp(-uFogFalloff * (uCamPos.y - uFogBase)) * max(span, 0.0);
  float amount = 1.0 - exp(-optical);

  float towardsSun = max(dot(dir, -uSunDir), 0.0);
  // Haze in shadow still has the sky on it; it does not go black.
  vec3 fog = uFogColour * mix(0.52, 1.0, sunlit)
           + uSunColour * uInscatter * pow(towardsSun, 8.0) * sunlit;
  return mix(col, fog, clamp(amount, 0.0, 1.0));
}

void main() {
  vec2 screenUv = gl_FragCoord.xy * uInvScreen;
  float sunlit = texture(uShaftMap, screenUv).r;

  if (uMaterial == 2) {
    // Emissive: unlit, straight into the HDR buffer for bloom to pick up.
    fragColor = vec4(applyFog(uEmissive * uEmissiveIntensity, vWorldPos, sunlit), 1.0);
    return;
  }

  vec3 geoN = normalize(vNormal);
  vec3 V = normalize(uCamPos - vWorldPos);

  // ---------------- the frames
  // Local: the mesh's own axes, in metres. World: where it has been put.
  // Detail is worked out in one or the other (uLocalShading) and the result
  // brought to world space at the end.
  vec3 pL = vLocalPos * uObjScale;
  vec3 nL = normalize(vLocalNormal / uObjScale);
  vec3 fpL = planeFootprints(pL);
  vec3 fpW = planeFootprints(vWorldPos);
  // The plain size of a pixel on the surface, in metres, for the things
  // that are a width rather than a pattern.
  float pix = max(length(dFdx(vWorldPos)), length(dFdy(vWorldPos)));

  bool isBox = dot(uBoxHalf, uBoxHalf) > 0.0;
  float edgeDist = 1e3, bevelWidth = 0.0;
  float rise = 1e3, drop = 1e3;
  vec3 N = geoN;
  if (isBox && uMaterial == 0) {
    vec3 bevelled = boxEdges(nL, pix, edgeDist, bevelWidth);
    N = normalize(uObjRot * bevelled);
    nL = bevelled;
    // Height above the foot and below the top, measured along world up —
    // which for the upright boxes weather applies to is the box's own axis.
    float halfH = uBoxHalf.y * uObjScale.y;
    rise = pL.y + halfH;
    drop = halfH - pL.y;
  }

  vec3 albedo = uTint;
  float metallic = uMetallic;
  float roughness = uRoughness;
  float cavity = 1.0;

  if (uMaterial == 0) {
    bool local = uLocalShading == 1;
    Surface s = shadeBuilt(local ? pL : vWorldPos, local ? nL : N, local ? fpL : fpW,
                           edgeDist, rise, drop, geoN.y);
    albedo = s.albedo; metallic = s.metallic; roughness = s.roughness; cavity = s.cavity;
    N = local ? normalize(uObjRot * s.n) : s.n;
  } else if (uMaterial == 1) {
    Surface s = shadeTerrain(vWorldPos, N, fpW, uTint, vBlend);
    albedo = s.albedo; metallic = 0.0; roughness = s.roughness; cavity = s.cavity;
    N = s.n;
  } else if (uMaterial == 4) {
    float roughOut;
    albedo = shadePlanet(N, uTint, uTint2, uCapExtent, roughOut);
    roughness = roughOut;
    metallic = 0.0;
  } else {  // Rock
    vec3 w = triplanarWeights(N);
    float h; vec3 g;
    triplanarD(vWorldPos, w, fpW, 0.6, 4, h, g);
    albedo = uTint * (0.8 + h * 0.5);
    N = perturb(N, g * 0.5);
    roughness = clamp(uRoughness + (h - 0.5) * 0.12, 0.6, 1.0);
    metallic = 0.0;
  }

  // Specular anti-aliasing: a sharp reflection (low roughness) narrower
  // than a pixel's normal variance crawls with aliasing under a bump-mapped
  // surface; widen roughness by how fast the shading normal is changing
  // across the pixel's own footprint.
  vec3 dNx = dFdx(N), dNy = dFdy(N);
  float variance = 0.5 * (dot(dNx, dNx) + dot(dNy, dNy));
  float widen = min(2.0 * variance, 0.18);
  roughness = sqrt(clamp(roughness * roughness + widen, 0.0, 1.0));

  vec3 F0 = mix(vec3(0.04), albedo, metallic);
  // Clamped well above zero, not just above the divide-by-zero floor: as
  // NoV approaches zero at a grazing viewing angle, D*G*F / (4*NoV*NoL)
  // is a near-0/near-0 ratio that is extremely sensitive to the sub-pixel
  // noise ordinary interpolation leaves in N.
  float NoV = max(dot(N, V), 0.02);

  // Ambient occlusion from the depth buffer, and the material's own creases
  // on top of it.
  float ao = texture(uAoMap, screenUv).r * cavity;

  // ---------------- direct: the sun ----------------
  vec3 L = normalize(-uSunDir);
  float NoL = max(dot(N, L), 0.0);
  vec3 direct = vec3(0.0);
  if (NoL > 0.0 && uSunIntensity > 0.0) {
    float shadow = shadowFactor(-vViewPos.z, geoN, max(dot(geoN, L), 0.0));
    // A crease too small for the shadow map to hold is still a crease.
    float micro = mix(1.0, ao, 0.35);
    direct = brdf(N, V, L, NoV, albedo, metallic, roughness, F0) * uSunColour * uSunIntensity * shadow * micro;
  }

  // ---------------- direct: everything that glows ----------------
  if (uLightCount > 0) {
    direct += evalLocalLights(vWorldPos, N, V, NoV, albedo, metallic, roughness, F0) * mix(1.0, ao, 0.5);
  }

  // ---------------- indirect: image-based lighting ----------------
  vec3 Fibl = fresnelSchlick(NoV, F0);
  vec3 kdIbl = (vec3(1.0) - Fibl) * (1.0 - metallic);
  vec3 irradiance = texture(uIrradianceMap, N).rgb;
  // Light that gets into a crease bounces around in it before it gets out,
  // and comes out the colour of the crease. On a pale surface that makes
  // occlusion much weaker than the raw number says (Jimenez et al. 2016);
  // applying it raw is why SSAO so often looks like soot.
  vec3 aoA = 2.0404 * albedo - 0.3324;
  vec3 aoB = -4.7951 * albedo + 0.6417;
  vec3 aoC = 2.7552 * albedo + 0.6903;
  vec3 aoDiffuse = max(vec3(ao), ((ao * aoA + aoB) * ao + aoC) * ao);
  vec3 diffuseIbl = irradiance * albedo * kdIbl * aoDiffuse;

  vec3 R = uAniso > 0.001
    ? anisoBendReflection(N, V, vec3(0.0, 1.0, 0.0), uAniso)
    : reflect(-V, N);
  vec3 prefiltered = textureLod(uSpecMap, R, roughness * uIblMaxMip).rgb;
  vec2 ab = envBRDFApprox(NoV, roughness);
  // A reflection is occluded too, and more sharply than the diffuse is: a
  // mirror in a crease reflects the crease (Lagarde & de Rousiers 2014). And
  // a bumped normal can aim the reflection *into* the surface it is on,
  // which would otherwise show as light leaking out of the ground.
  float specOcc = clamp(pow(NoV + ao, exp2(-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
  float horizon = clamp(1.0 + dot(R, geoN), 0.0, 1.0);
  specOcc *= horizon * horizon;
  vec3 specularIbl = prefiltered * (F0 * ab.x + ab.y) * specOcc;

  vec3 ambient = (diffuseIbl + specularIbl) * uIblIntensity + uAmbientFill * albedo * aoDiffuse;

  // A planet seen from orbit has air on it: the limb glows where you look
  // through the most of it, on the day side.
  if (uMaterial == 4) {
    float rim = pow(1.0 - NoV, 3.2);
    float day = smoothstep(-0.15, 0.45, dot(geoN, L));
    vec3 air = mix(uTint2, vec3(0.45, 0.62, 1.0), 0.55);
    direct += air * rim * day * uSunIntensity * 0.22;
  }

  vec3 colour = direct + ambient + uEmissive * uEmissiveIntensity;
  fragColor = vec4(applyFog(colour, vWorldPos, sunlit), 1.0);
}
