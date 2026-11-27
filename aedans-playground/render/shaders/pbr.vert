#version 460
#extension GL_EXT_buffer_reference : require
#extension GL_GOOGLE_include_directive : require
// =============================================================================
//  pbr.vert — one vertex of a mesh: into world space for lighting, and into
//  clip space for the rasteriser.
// =============================================================================

#include "mesh_common.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inTangent;  // w: bitangent sign
layout(location = 3) in vec2 inUv;

layout(location = 0) out vec3 outWorldPosition;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outTangent;
layout(location = 3) out vec2 outUv;
layout(location = 4) flat out uint outDrawIndex;

void main() {
  MeshDraw draw = push.drawList.draws[gl_InstanceIndex];
  vec4 world = draw.model * vec4(inPosition, 1.0);
  outWorldPosition = world.xyz;
  outNormal = mat3(draw.normalMatrix) * inNormal;
  outTangent = vec4(mat3(draw.model) * inTangent.xyz, inTangent.w);  // tangents follow the surface, like positions
  outUv = inUv;
  outDrawIndex = gl_InstanceIndex;
  gl_Position = frame.viewProjection * world;
}
