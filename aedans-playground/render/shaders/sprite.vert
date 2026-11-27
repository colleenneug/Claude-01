#version 450
#extension GL_EXT_buffer_reference : require
// =============================================================================
//  sprite.vert — every sprite in one instanced draw.
//
//  vkCmdDraw(cmd, 6, spriteCount, 0, 0) runs this 6 x spriteCount times:
//    gl_VertexIndex   (0..5) picks a corner of the quad's two triangles;
//    gl_InstanceIndex (0..spriteCount-1) picks the sprite.
//
//  THE SPRITE DATA lives in a storage buffer the CPU rewrites each frame
//  (one per frame in flight, persistently mapped). The shader finds it
//  through the buffer's 64-bit GPU address, passed in a push constant: a
//  "buffer reference", so the sprites need no descriptor set.
//
//  The struct layouts must match SpriteGpu and DrawPushConstants in
//  VulkanRenderer.cpp byte for byte (static_asserts there check the C++ side).
//
//  THE CAMERA comes from a per-frame uniform buffer (set 0, binding 0): its
//  view-projection matrix takes world positions (x right, y up, z towards
//  the viewer) to clip space, with reverse-Z depth (engine/Camera.h). Each
//  sprite's world z is its `depth.x`, so the depth test orders layers.
// =============================================================================

struct Sprite {
  vec2 position;   // offset  0: centre, world units
  vec2 halfSize;   // offset  8
  vec2 rotation;   // offset 16: (cos, sin), computed once per sprite on the CPU
  vec2 depth;      // offset 24: x = world z; y unused (keeps colour 16-byte aligned)
  vec4 colour;     // offset 32: linear RGBA
};                 // 48 bytes

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer SpriteList {
  Sprite sprites[];
};

layout(push_constant) uniform Draw {
  SpriteList spriteList;  // GPU address of this frame's sprite buffer
} draw;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProjection;    // world -> clip, reverse-Z
} frame;

layout(location = 0) out vec4 outColour;

const vec2 kCorners[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),   // first triangle
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0));  // second triangle

void main() {
  Sprite sprite = draw.spriteList.sprites[gl_InstanceIndex];
  vec2 local = kCorners[gl_VertexIndex] * sprite.halfSize;
  vec2 rotated = vec2(local.x * sprite.rotation.x - local.y * sprite.rotation.y,
                      local.x * sprite.rotation.y + local.y * sprite.rotation.x);
  gl_Position = frame.viewProjection * vec4(rotated + sprite.position, sprite.depth.x, 1.0);
  outColour = sprite.colour;
}
