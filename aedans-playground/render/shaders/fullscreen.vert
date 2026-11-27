#version 460
// =============================================================================
//  fullscreen.vert — one triangle that covers the render target.
//
//  vkCmdDraw(3, 1, 0, 0): corners (-1,-1), (3,-1), (-1,3). The post shaders
//  read their inputs by pixel position (gl_FragCoord), so whichever way the
//  viewport is flipped, each pixel reads its own place.
// =============================================================================

void main() {
  vec2 corner = vec2(float((gl_VertexIndex << 1) & 2), float(gl_VertexIndex & 2)) * 2.0 - 1.0;
  gl_Position = vec4(corner, 0.0, 1.0);
}
