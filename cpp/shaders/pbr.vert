#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in float aBlend;   // terrain only: weight between the two stone layers

uniform mat4 uModel;
uniform mat3 uNormalMatrix;   // transpose(inverse(model)) — correct under non-uniform scale
uniform mat4 uView;
uniform mat4 uProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vViewPos;   // used only to pick/cross-fade shadow cascades by distance
out float vBlend;

void main() {
  vec4 world = uModel * vec4(aPosition, 1.0);
  vWorldPos = world.xyz;
  vNormal = normalize(uNormalMatrix * aNormal);
  vBlend = aBlend;

  vec4 viewPos = uView * world;
  vViewPos = viewPos.xyz;
  gl_Position = uProj * viewPos;
}
