#version 410 core
// The "big triangle" trick: one triangle, no vertex buffer, that covers the
// whole screen and clips cleanly. Every post-processing pass in this
// project uses this same vertex stage; only the fragment shader changes.
out vec2 vUv;

void main() {
  vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
  vUv = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
