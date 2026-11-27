// =============================================================================
//  mesh_common.glsl — what mesh.vert and mesh.frag share.
//
//  Must match FrameUniforms and MeshDrawGpu in VulkanRenderer.cpp byte for
//  byte (static_asserts there check the C++ side).
// =============================================================================

// Set 0, binding 0: the camera and the lights, once per frame (std140).
layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProjection;  // world -> clip, reverse-Z
  vec4 cameraPosition;  // xyz: world
  vec4 sunDirection;    // xyz: the way the light travels (normalised)
  vec4 sunColour;       // rgb: colour x intensity
  vec4 ambient;         // rgb: light from everywhere
} frame;

// One per mesh drawn this frame, in a storage buffer found by its address.
struct MeshDraw {
  mat4 model;             // model -> world
  mat4 normalMatrix;      // inverse-transpose of the model's 3x3: keeps normals perpendicular under scaling
  vec4 baseColourFactor;  // linear RGBA
  vec4 factors;           // x metallic, y roughness, z normal scale
  uvec4 textures;         // indices into the bindless array: x base colour, y normal, z metallic-roughness
};                        // 176 bytes

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer DrawList {
  MeshDraw draws[];
};

// vkCmdDrawIndexed(..., firstInstance = draw index): gl_InstanceIndex picks
// this mesh's entry, and is passed to the fragment shader as a flat value.
layout(push_constant) uniform Push {
  DrawList drawList;
} push;
