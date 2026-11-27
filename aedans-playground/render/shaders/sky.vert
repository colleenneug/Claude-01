#version 460
#extension GL_GOOGLE_include_directive : require
// =============================================================================
//  sky.vert — one triangle that covers the screen, on the far plane.
//
//  vkCmdDraw(3, 1, 0, 0): corners (-1,-1), (3,-1), (-1,3) in clip space, so
//  the visible square [-1, 1] is inside it. Depth 0 is the far plane with
//  reverse-Z, so the depth test (GREATER_OR_EQUAL against what meshes
//  wrote) keeps the sky only where nothing was drawn.
//
//  For each corner, the world points on the near and far planes behind it
//  (through the inverse view-projection). They are passed on unprojected
//  (w not divided), which interpolates correctly across the screen; the
//  fragment shader divides and subtracts to get its view direction. Works
//  for perspective and orthographic cameras alike.
// =============================================================================
#include "frame.glsl"

layout(location = 0) out vec4 outNear;
layout(location = 1) out vec4 outFar;

void main() {
  vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
  outNear = frame.inverseViewProjection * vec4(corner, 1.0, 1.0);  // reverse-Z: 1 is near
  outFar = frame.inverseViewProjection * vec4(corner, 0.0, 1.0);
  gl_Position = vec4(corner, 0.0, 1.0);
}
