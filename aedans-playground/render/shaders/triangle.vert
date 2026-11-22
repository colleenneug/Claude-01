#version 450
// =============================================================================
//  triangle.vert — a hardcoded 2D triangle.
//
//  No vertex buffer: the three corners live in this shader, and
//  gl_VertexIndex (0, 1, 2 for vkCmdDraw(cmd, 3, ...)) picks one per vertex.
//
//  POSITIONS are in clip space: x and y run from -1 to +1 across the screen.
//  The renderer flips the viewport (negative height), so +y is UP, as in
//  OpenGL and GLM. Without the flip Vulkan's +y points down.
//
//  LAYOUT: `location = 0` here is matched by `location = 0` in triangle.frag.
//  Locations, not variable names, are what connect the two stages.
// =============================================================================

layout(location = 0) out vec3 outColour;

const vec2 kPositions[3] = vec2[](
    vec2( 0.0,  0.6),   // top
    vec2(-0.6, -0.5),   // bottom left
    vec2( 0.6, -0.5));  // bottom right

const vec3 kColours[3] = vec3[](
    vec3(1.0, 0.0, 0.0),   // red
    vec3(0.0, 1.0, 0.0),   // green
    vec3(0.0, 0.0, 1.0));  // blue

void main() {
  gl_Position = vec4(kPositions[gl_VertexIndex], 0.0, 1.0);
  outColour = kColours[gl_VertexIndex];
}
