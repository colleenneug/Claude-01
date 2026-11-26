#version 450
// =============================================================================
//  sprite.frag — fills the sprite with its colour.
//
//  Input location 0 matches sprite.vert's output location 0; output location
//  0 is the swapchain image. Alpha is blended by the pipeline.
// =============================================================================

layout(location = 0) in vec4 inColour;
layout(location = 0) out vec4 outColour;

void main() {
  outColour = inColour;
}
