// =============================================================================
//  frame.glsl — per-frame data every 3D shader reads (pbr.*, shadow.vert, sky.*).
//
//  Must match FrameUniforms in VulkanRenderer.cpp byte for byte (std140;
//  static_asserts there check the C++ side). sprite.vert declares only the
//  first member, which is allowed.
// =============================================================================

// Set 0, binding 0: the camera and the lights, once per frame.
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProjection;         // world -> clip, reverse-Z
  mat4 inverseViewProjection;  // clip -> world: the sky finds its view directions with it
  mat4 lightViewProjection;    // world -> the sun's shadow map (render/Shadow.h)
  vec4 cameraPosition;         // xyz: world
  vec4 sunDirection;           // xyz: the way the light travels (normalised)
  vec4 sunColour;              // rgb: colour x intensity
  vec4 ambient;                // rgb: a flat fill light
  vec4 environment;            // x intensity on meshes, y last specular mip
  vec4 shadow;                 // x on (1) / off (0), y normal offset (world units), z strength, w one texel (uv)
} frame;

// Set 1: every texture. Binding 0 is the bindless material array (pbr.frag);
// the rest are the environment (render/Environment.h) and the shadow map.
layout(set = 1, binding = 1) uniform samplerCube irradianceMap;  // matte light per normal
layout(set = 1, binding = 2) uniform samplerCube specularMap;    // reflections, mip = roughness
layout(set = 1, binding = 3) uniform sampler2D brdfLut;          // split-sum (scale, bias) by (n.v, roughness)
layout(set = 1, binding = 4) uniform samplerCube environmentMap; // the sky itself
layout(set = 1, binding = 5) uniform sampler2DShadow shadowMap;  // the sun's depth map; compares in hardware

// Output is LINEAR HDR: the scene renders into a 16-bit float image, and
// the composite pass (tonemap.frag) applies exposure and tone mapping.
