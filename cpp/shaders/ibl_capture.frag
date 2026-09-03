#version 410 core
// Unlit flat colour: these panels are light sources, not surfaces, so
// nothing needs to illuminate them — the same idea as envmap.js's room().
out vec4 fragColor;
uniform vec3 uColour;

void main() { fragColor = vec4(uColour, 1.0); }
