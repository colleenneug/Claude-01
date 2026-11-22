#version 450
// =============================================================================
//  triangle.frag — colours each pixel of the triangle.
//
//  `inColour` arrives already blended between the three corners' colours
//  (the rasteriser interpolates every vertex output across the triangle).
//
//  LAYOUT: input location 0 matches triangle.vert's output location 0.
//  Output location 0 is colour attachment 0 of the rendering pass: the
//  swapchain image. Colours are linear; the sRGB swapchain format converts
//  them for the monitor when they are written.
// =============================================================================

layout(location = 0) in vec3 inColour;
layout(location = 0) out vec4 outColour;

void main() {
  outColour = vec4(inColour, 1.0);
}
