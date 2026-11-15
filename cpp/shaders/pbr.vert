#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aBlend;   // terrain only: weight between the two stone layers

uniform mat4 uModel;
uniform mat3 uNormalMatrix;   // transpose(inverse(model)) — correct under non-uniform scale
uniform mat4 uView;
uniform mat4 uProj;
// Where this vertex is on screen now and where it was a frame ago, both
// without the anti-aliasing jitter. The difference is how far the surface
// moved, and the temporal resolve needs that to find last frame's picture
// of a thing that is not standing still (see taa.frag).
uniform mat4 uCleanViewProj;
uniform mat4 uPrevViewProj;
uniform mat4 uPrevModel;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vViewPos;   // used only to pick/cross-fade shadow cascades by distance
out float vBlend;
// The mesh's own frame, before the model matrix. Two things need it: a box
// has to know how far a fragment is from its own edges to round them off and
// wear them (see boxEdges in pbr.frag), and anything that moves has to carry
// its surface detail with it rather than slide through a pattern nailed to
// the world.
out vec3 vLocalPos;
out vec3 vLocalNormal;
out vec4 vClipNow;
out vec4 vClipThen;

// This vertex stage is shared with the depth pre-pass (prepass.frag), and the
// colour pass then tests LEQUAL against what the pre-pass wrote. That only
// works if both programs land every vertex on exactly the same depth, which
// GLSL does not promise for two separately compiled programs unless asked.
invariant gl_Position;

void main() {
  vec4 world = uModel * vec4(aPosition, 1.0);
  vWorldPos = world.xyz;
  vNormal = normalize(uNormalMatrix * aNormal);
  vBlend = aBlend;
  vLocalPos = aPosition;
  vLocalNormal = aNormal;
  vClipNow = uCleanViewProj * world;
  vClipThen = uPrevViewProj * (uPrevModel * vec4(aPosition, 1.0));

  vec4 viewPos = uView * world;
  vViewPos = viewPos.xyz;
  gl_Position = uProj * viewPos;
}
