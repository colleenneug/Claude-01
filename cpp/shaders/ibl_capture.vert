#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;   // unused; the box generator always emits it
layout(location = 2) in float aBlend;   // unused

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;

void main() {
  gl_Position = uProj * uView * uModel * vec4(aPosition, 1.0);
}
