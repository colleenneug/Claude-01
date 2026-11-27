// =============================================================================
//  bake_common.glsl — shared by the image-based-lighting bake shaders
//  (bake_*.comp; render/src/IblBaker.cpp runs them).
//
//  Each invocation fills one texel of one face of a cube map: x, y = the
//  texel, z = the face (dispatch: ceil(size / 8) x ceil(size / 8) x 6).
//  The output is one mip level of the cube, seen as a 2D array of 6 layers.
// =============================================================================

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(set = 0, binding = 1, rgba16f) uniform writeonly image2DArray outputFaces;

// Must match BakePush in IblBaker.cpp.
layout(push_constant) uniform Push {
  vec4 zenith;        // procedural sky colours (rgb)
  vec4 horizon;
  vec4 ground;
  vec4 sunDirection;  // xyz: the way the light travels
  vec4 glow;          // rgb colour, w strength
  vec4 params;        // x roughness, y sample count, z source size (texels per face, or panorama width), w output size
} push;

const float kPi = 3.14159265359;

// The direction through texel `id` of face id.z, for an output of `size`.
// The Vulkan specification's cube map face table, inverted: face 0..5 =
// +x, -x, +y, -y, +z, -z; s right, t DOWN within each face.
vec3 cubeDirection(uvec3 id, float size) {
  vec2 st = (vec2(id.xy) + 0.5) / size * 2.0 - 1.0;
  float s = st.x;
  float t = st.y;
  vec3 d;
  switch (id.z) {
    case 0u: d = vec3(1.0, -t, -s); break;
    case 1u: d = vec3(-1.0, -t, s); break;
    case 2u: d = vec3(s, 1.0, t); break;
    case 3u: d = vec3(s, -1.0, -t); break;
    case 4u: d = vec3(s, -t, 1.0); break;
    default: d = vec3(-s, -t, -1.0); break;
  }
  return normalize(d);
}

// The mip level to read for a sample standing for `pdf` of `count` samples
// (filtered importance sampling): wider samples read blurrier levels. Never
// below 4x4 faces: smaller levels average across the horizon and would
// bleed the ground into the sky (and back).
float sampleLod(float pdf, uint count) {
  float faceSize = push.params.z;
  float texelSolidAngle = 4.0 * kPi / (6.0 * faceSize * faceSize);
  float lod = 0.5 * log2(1.0 / (float(count) * pdf * texelSolidAngle)) + 1.0;
  return clamp(lod, 0.0, max(log2(faceSize) - 2.0, 0.0));
}

bool outside(uvec3 id) {
  return id.x >= uint(push.params.w) || id.y >= uint(push.params.w);
}

void store(uvec3 id, vec3 colour) {
  imageStore(outputFaces, ivec3(id), vec4(colour, 1.0));
}

// Evenly spread sample points (see BrdfLut.cpp, its C++ twin).
vec2 hammersley(uint i, uint n) {
  uint bits = bitfieldReverse(i);
  return vec2(float(i) / float(n), float(bits) * 2.3283064365386963e-10);
}

// An orthonormal frame around n: (tangent, bitangent, n) as columns.
mat3 basisAround(vec3 n) {
  vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 t = normalize(cross(up, n));
  vec3 b = cross(n, t);
  return mat3(t, b, n);
}
