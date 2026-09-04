#version 410 core
// Cascade depth-only pass: transform to one cascade's light space and stop.
// No fragment-side work at all — the fixed-function depth test writes the
// depth buffer, which is the entire output this pass needs.
layout(location = 0) in vec3 aPosition;

uniform mat4 uLightViewProj;
uniform mat4 uModel;

void main() {
  gl_Position = uLightViewProj * uModel * vec4(aPosition, 1.0);
}
