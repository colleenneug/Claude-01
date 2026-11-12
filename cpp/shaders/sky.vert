#version 410 core
// The sky is one triangle at the far plane. Drawn after everything opaque
// with the depth test at LEQUAL, it only lands where nothing else did — so
// it costs the pixels of sky actually on screen, not the whole frame.
out vec2 vUv;

void main() {
  vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 1.0, 1.0);
}
