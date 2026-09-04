#version 410 core
layout(location = 0) in vec2 aPos;   // unit quad, 0..1

uniform vec4 uRect;    // x, y, w, h — pixels, origin top-left
uniform vec2 uScreen;

void main() {
  vec2 px = uRect.xy + aPos * uRect.zw;
  vec2 ndc = vec2(px.x / uScreen.x * 2.0 - 1.0, 1.0 - px.y / uScreen.y * 2.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
}
