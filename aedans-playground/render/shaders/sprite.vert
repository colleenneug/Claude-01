#version 450
// =============================================================================
//  sprite.vert — one coloured, rotated rectangle per draw.
//
//  No vertex buffer: vkCmdDraw(cmd, 6, ...) runs this 6 times, and
//  gl_VertexIndex picks a corner of the two triangles that make the quad.
//
//  PUSH CONSTANTS carry the sprite: 48 bytes recorded straight into the
//  command buffer with each draw. The C++ side is SpritePushConstants in
//  VulkanRenderer.cpp; the two layouts must match byte for byte.
//
//  World units are pixels with (0, 0) at the window's centre and +y up (the
//  renderer flips the viewport), so dividing by half the viewport size
//  gives clip space directly.
// =============================================================================

layout(push_constant) uniform Sprite {
  vec2 position;      // offset  0: centre, pixels
  vec2 halfSize;      // offset  8: half width and height, pixels
  vec2 rotation;      // offset 16: (cos, sin) of the angle
  vec2 viewportHalf;  // offset 24: half the framebuffer size, pixels
  vec4 colour;        // offset 32: linear RGBA
} sprite;

layout(location = 0) out vec4 outColour;

const vec2 kCorners[6] = vec2[](
    vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),   // first triangle
    vec2(-1.0, -1.0), vec2( 1.0,  1.0), vec2(-1.0,  1.0));  // second triangle

void main() {
  vec2 local = kCorners[gl_VertexIndex] * sprite.halfSize;
  vec2 rotated = vec2(local.x * sprite.rotation.x - local.y * sprite.rotation.y,
                      local.x * sprite.rotation.y + local.y * sprite.rotation.x);
  vec2 world = rotated + sprite.position;
  gl_Position = vec4(world / sprite.viewportHalf, 0.0, 1.0);
  outColour = sprite.colour;
}
