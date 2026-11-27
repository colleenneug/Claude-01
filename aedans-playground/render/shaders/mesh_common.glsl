// =============================================================================
//  mesh_common.glsl — what pbr.vert, pbr.frag and shadow.vert share.
//
//  Must match MeshDrawGpu in VulkanRenderer.cpp byte for byte (static_asserts
//  there check the C++ side). The frame's uniforms are in frame.glsl.
// =============================================================================

#include "frame.glsl"

// One per mesh drawn this frame, in a storage buffer found by its address.
struct MeshDraw {
  mat4 model;             // model -> world
  mat4 normalMatrix;      // inverse-transpose of the model's 3x3: keeps normals perpendicular under scaling
  vec4 baseColourFactor;  // linear RGBA
  vec4 factors;           // x metallic, y roughness, z normal scale
  uvec4 textures;         // indices into the bindless array: x base colour, y normal, z metallic-roughness, w emissive
  vec4 emissive;          // rgb: light given off (x the emissive texture)
};                        // 192 bytes

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer DrawList {
  MeshDraw draws[];
};

// vkCmdDrawIndexed(..., firstInstance = draw index): gl_InstanceIndex picks
// this mesh's entry, and is passed to the fragment shader as a flat value.
layout(push_constant) uniform Push {
  DrawList drawList;
} push;
