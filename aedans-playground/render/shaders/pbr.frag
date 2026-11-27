#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
// =============================================================================
//  pbr.frag — glTF's metallic-roughness material, lit by the sun.
//
//  TEXTURES come from one big ("bindless") array, set 1, binding 0: each
//  draw names its textures by index, so changing material needs no
//  descriptor set switch. The index is the same for the whole draw.
//
//  SHADING (physically based, the model glTF assets are authored for):
//    normal      the vertex normal, bent by the normal map: the map's RGB is
//                a direction in TANGENT space (x along the texture's u, y
//                along v, z straight out), turned into world space with the
//                tangent, bitangent and normal (the "TBN" basis).
//    diffuse     base colour / pi, for non-metals only (metals have none).
//    specular    Cook-Torrance: GGX distribution (how many micro-facets face
//                the halfway vector; rougher = wider, dimmer highlight),
//                Smith-Schlick geometry (facets shadowing each other) and
//                Schlick's Fresnel (everything gets more reflective at
//                glancing angles). Non-metals reflect 4% head-on; metals
//                reflect their base colour.
//    environment image-based lighting (render/Environment.h):
//                diffuse   the irradiance map at the normal, x base colour,
//                          for the part not reflected (non-metals);
//                specular  the prefiltered map at the reflection direction,
//                          at the mip for this roughness, x (F0 x scale +
//                          bias) from the BRDF table (the split sum).
//    ambient     an optional flat fill: base colour for non-metals, the
//                reflected colour for metals.
//    emissive    light the surface gives off itself (lamps, screens), added
//                last; bright enough, it blooms.
//
//  SHADOWS (frame.shadow.x = 1) dim the SUN's light only: the environment
//  still lights shadowed surfaces, as the sky does. Looked up in the sun's
//  shadow map:
//    - moved out along the surface normal first (normal offset, against
//      acne on surfaces at a slant to the light);
//    - 3x3 taps of a sampler2DShadow: each tap compares the depth in
//      hardware and filters the 2x2 results, so the nine together give a
//      soft edge (percentage-closer filtering, PCF);
//    - faded out near the edges of the map and outside its depth range,
//      so what the map does not cover is lit, never black.
//
//  Output is linear HDR; the composite pass tone maps it (tonemap.frag).
// =============================================================================

#include "mesh_common.glsl"

layout(set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) in vec3 inWorldPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;
layout(location = 3) in vec2 inUv;
layout(location = 4) flat in uint inDrawIndex;

layout(location = 0) out vec4 outColour;

const float kPi = 3.14159265359;

// How much of the sun reaches this point: 1 lit, 0 in full shadow.
float sunVisibility(vec3 worldPosition, vec3 surfaceNormal) {
  if (frame.shadow.x < 0.5) return 1.0;
  vec4 clip = frame.lightViewProjection * vec4(worldPosition + surfaceNormal * frame.shadow.y, 1.0);
  vec3 ndc = clip.xyz / clip.w;
  vec2 uv = ndc.xy * 0.5 + 0.5;  // the shadow pass's viewport is not flipped: uv follows ndc
  float edge = max(abs(ndc.x), abs(ndc.y));
  float inside = (1.0 - smoothstep(0.9, 1.0, edge)) * step(0.0, ndc.z) * step(ndc.z, 1.0);
  if (inside <= 0.0) return 1.0;
  float texel = frame.shadow.w;
  float lit = 0.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      lit += texture(shadowMap, vec3(uv + vec2(x, y) * texel, ndc.z));  // 1 where ndc.z >= stored (reverse-Z: not behind)
    }
  }
  lit /= 9.0;
  return mix(1.0, lit, inside * frame.shadow.z);
}

float distributionGgx(float nDotH, float roughness) {
  float a = roughness * roughness;  // "perceptual" roughness squared, as glTF specifies
  float a2 = a * a;
  float d = nDotH * nDotH * (a2 - 1.0) + 1.0;
  return a2 / (kPi * d * d);
}

float geometrySmith(float nDotV, float nDotL, float roughness) {
  float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
  return (nDotV / (nDotV * (1.0 - k) + k)) * (nDotL / (nDotL * (1.0 - k) + k));
}

vec3 fresnelSchlick(float cosTheta, vec3 f0) {
  return f0 + (1.0 - f0) * pow(1.0 - cosTheta, 5.0);
}

void main() {
  MeshDraw draw = push.drawList.draws[inDrawIndex];

  vec4 baseColour = texture(textures[draw.textures.x], inUv) * draw.baseColourFactor;
  vec2 metallicRoughness = texture(textures[draw.textures.z], inUv).bg;  // glTF: metallic in B, roughness in G
  float metallic = clamp(metallicRoughness.x * draw.factors.x, 0.0, 1.0);
  float roughness = clamp(metallicRoughness.y * draw.factors.y, 0.04, 1.0);  // 0 makes a pinpoint highlight

  // The surface normal, bent by the normal map. A double-sided material's
  // back face is lit as a surface facing the other way.
  vec3 n = normalize(gl_FrontFacing ? inNormal : -inNormal);
  vec3 t = normalize(inTangent.xyz - n * dot(n, inTangent.xyz));  // re-orthogonalise after interpolation
  vec3 b = cross(n, t) * inTangent.w;
  vec3 mapped = texture(textures[draw.textures.y], inUv).xyz * 2.0 - 1.0;
  mapped.xy *= draw.factors.z;
  n = normalize(mat3(t, b, n) * mapped);

  vec3 v = normalize(frame.cameraPosition.xyz - inWorldPosition);
  vec3 l = normalize(-frame.sunDirection.xyz);
  vec3 h = normalize(v + l);
  float nDotL = max(dot(n, l), 0.0);
  float nDotV = max(dot(n, v), 1e-4);
  float nDotH = max(dot(n, h), 0.0);
  float vDotH = max(dot(v, h), 0.0);

  vec3 f0 = mix(vec3(0.04), baseColour.rgb, metallic);
  vec3 fresnel = fresnelSchlick(vDotH, f0);
  vec3 specular = distributionGgx(nDotH, roughness) * geometrySmith(nDotV, nDotL, roughness) * fresnel /
                  (4.0 * nDotV * max(nDotL, 1e-4));
  vec3 diffuse = (1.0 - fresnel) * (1.0 - metallic) * baseColour.rgb / kPi;
  vec3 direct = (diffuse + specular) * frame.sunColour.rgb * nDotL;
  if (nDotL > 0.0) direct *= sunVisibility(inWorldPosition, normalize(gl_FrontFacing ? inNormal : -inNormal));
  vec3 ambient = frame.ambient.rgb * mix(baseColour.rgb, f0, metallic);

  // Image-based lighting. Fresnel for the environment uses n.v with a
  // roughness-aware ceiling: rough surfaces do not turn into mirrors at
  // glancing angles.
  vec3 environment = vec3(0.0);
  if (frame.environment.x > 0.0) {
    vec3 fresnelRough = f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(1.0 - nDotV, 5.0);
    vec3 diffuseIbl = texture(irradianceMap, n).rgb * baseColour.rgb * (1.0 - fresnelRough) * (1.0 - metallic);
    vec3 r = reflect(-v, n);
    vec3 prefiltered = textureLod(specularMap, r, roughness * frame.environment.y).rgb;
    vec2 scaleBias = texture(brdfLut, vec2(nDotV, roughness)).rg;
    vec3 specularIbl = prefiltered * (f0 * scaleBias.x + scaleBias.y);
    environment = (diffuseIbl + specularIbl) * frame.environment.x;
  }

  vec3 emitted = texture(textures[draw.textures.w], inUv).rgb * draw.emissive.rgb;

  outColour = vec4(direct + ambient + environment + emitted, 1.0);  // linear HDR
}
