#version 450
// =============================================================================
//  sprite.frag — fills the sprite with its colour.
//
//  Input location 0 matches sprite.vert's output location 0; output location
//  0 is the swapchain image. Alpha is blended by the pipeline.
// =============================================================================

// Sprites draw straight onto the swapchain, after tone mapping. On a
// _UNORM swapchain the shader must encode sRGB itself (see tonemap.frag).
layout(constant_id = 0) const bool kEncodeSrgb = false;

layout(location = 0) in vec4 inColour;
layout(location = 0) out vec4 outColour;

vec3 encodeSrgb(vec3 linear) {
  linear = clamp(linear, 0.0, 1.0);
  vec3 low = linear * 12.92;
  vec3 high = 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055;
  return mix(high, low, lessThanEqual(linear, vec3(0.0031308)));
}

void main() {
  outColour = vec4(kEncodeSrgb ? encodeSrgb(inColour.rgb) : inColour.rgb, inColour.a);
}
