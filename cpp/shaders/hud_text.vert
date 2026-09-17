#version 410 core
// Batched HUD text: the CPU expands every lit font pixel into two
// triangles in screen-pixel space and uploads the whole frame's text as one
// buffer, so however much text is on screen it costs one draw call rather
// than one per pixel of every glyph. Position arrives already in pixels
// (origin top-left, like the rest of the HUD) and is converted here.
layout(location = 0) in vec2 aPixelPos;
layout(location = 1) in vec4 aColour;

uniform vec2 uScreen;

out vec4 vColour;

void main() {
  vec2 ndc = vec2(aPixelPos.x / uScreen.x * 2.0 - 1.0,
                  1.0 - aPixelPos.y / uScreen.y * 2.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  vColour = aColour;
}
