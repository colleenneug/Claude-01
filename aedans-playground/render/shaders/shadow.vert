#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_GOOGLE_include_directive : require
// =============================================================================
//  shadow.vert — a mesh as the sun sees it, for the shadow map.
//
//  Depth only: there is no fragment shader, the rasteriser writes depth.
//  The same per-draw list as the main pass (mesh_common.glsl) gives each
//  mesh's transform; the sun's matrix replaces the camera's.
// =============================================================================
#include "mesh_common.glsl"

layout(location = 0) in vec3 inPosition;

void main() {
  MeshDraw draw = push.drawList.draws[gl_InstanceIndex];
  gl_Position = frame.lightViewProjection * draw.model * vec4(inPosition, 1.0);
}
