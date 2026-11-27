// =============================================================================
//  frame.glsl — per-frame data every 3D shader reads (mesh.*, sky.*).
//
//  Must match FrameUniforms in VulkanRenderer.cpp byte for byte (std140;
//  static_asserts there check the C++ side). sprite.vert declares only the
//  first member, which is allowed.
// =============================================================================

// Set 0, binding 0: the camera and the lights, once per frame.
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProjection;         // world -> clip, reverse-Z
  mat4 inverseViewProjection;  // clip -> world: the sky finds its view directions with it
  vec4 cameraPosition;         // xyz: world
  vec4 sunDirection;           // xyz: the way the light travels (normalised)
  vec4 sunColour;              // rgb: colour x intensity
  vec4 ambient;                // rgb: a flat fill light
  vec4 environment;            // x intensity on meshes, y last specular mip, z exposure, w tone map (1 = on)
} frame;

// Set 1: every texture. Binding 0 is the bindless material array (mesh.frag);
// the rest are the environment (render/Environment.h).
layout(set = 1, binding = 1) uniform samplerCube irradianceMap;  // matte light per normal
layout(set = 1, binding = 2) uniform samplerCube specularMap;    // reflections, mip = roughness
layout(set = 1, binding = 3) uniform sampler2D brdfLut;          // split-sum (scale, bias) by (n.v, roughness)
layout(set = 1, binding = 4) uniform samplerCube environmentMap; // the sky itself

// Khronos PBR Neutral tone mapping: below ~0.76 colours pass unchanged
// (base colours stay true); brighter ones roll off smoothly towards white
// instead of clipping, and lose a little saturation as they do.
vec3 toneMapPbrNeutral(vec3 colour) {
  const float startCompression = 0.8 - 0.04;
  const float desaturation = 0.15;
  float x = min(colour.r, min(colour.g, colour.b));
  float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
  colour -= offset;
  float peak = max(colour.r, max(colour.g, colour.b));
  if (peak < startCompression) return colour;
  const float d = 1.0 - startCompression;
  float newPeak = 1.0 - d * d / (peak + d - startCompression);
  colour *= newPeak / peak;
  float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
  return mix(colour, vec3(newPeak), g);
}

// Exposure, then tone mapping when asked: the last step of every lit shader.
vec3 present(vec3 colour) {
  colour *= frame.environment.z;
  return frame.environment.w > 0.5 ? toneMapPbrNeutral(colour) : colour;
}
