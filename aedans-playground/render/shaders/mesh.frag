#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require
// =============================================================================
//  mesh.frag — glTF's metallic-roughness material, lit by the sun.
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
//    ambient     a flat term: base colour for non-metals, the reflected
//                colour for metals. A stand-in until image-based lighting.
//
//  Output is linear; the sRGB swapchain encodes it.
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
  vec3 ambient = frame.ambient.rgb * mix(baseColour.rgb, f0, metallic);

  outColour = vec4(direct + ambient, 1.0);
}
