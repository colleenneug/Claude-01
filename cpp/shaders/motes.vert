#version 410 core
// Dust motes, ported from the browser build's atmos.js: a fixed buffer of
// points that the vertex shader wraps into a box around the camera with
// mod(), so walking through them gives real parallax without ever touching
// the attribute array on the CPU side, and the same mote just reappears
// behind you as you pass it.
layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aSeed;
layout(location = 2) in float aScale;

uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uCamPos;
uniform float uBox;
uniform float uTime;
uniform float uSize;
uniform vec3 uSunDir;   // direction the light travels, world space

out float vAlpha;
out float vGlow;

void main() {
  vec3 p = aPosition + vec3(
    sin(uTime * 0.13 + aSeed * 6.2831),
    sin(uTime * 0.09 + aSeed * 3.1415) * 0.6,
    cos(uTime * 0.11 + aSeed * 5.1234)) * 0.7;

  vec3 rel = p - uCamPos;
  rel = mod(rel + uBox * 0.5, uBox) - uBox * 0.5;
  vec3 world = uCamPos + rel;

  vec4 viewPos = uView * vec4(world, 1.0);
  gl_Position = uProj * viewPos;

  float dist = max(-viewPos.z, 0.05);
  // Clamped hard: unclamped, a mote drifting within a few centimetres of the
  // lens would draw hundreds of pixels across, and thousands of those
  // blended additively is enough overdraw on its own to halve the frame rate.
  gl_PointSize = clamp((uSize * aScale) / dist, 1.0, 24.0);

  float edge = length(rel) / (uBox * 0.5);
  vAlpha = smoothstep(0.25, 1.2, dist) * (1.0 - smoothstep(0.72, 1.0, edge));

  // Forward scattering: brightest when the light keeps travelling roughly
  // straight through the mote and into the eye, i.e. when the mote-to-camera
  // direction lines up with the direction the light is already travelling —
  // which is why dust stays invisible until you turn into the beam.
  vec3 toEye = normalize(uCamPos - world);
  vGlow = 1.0 + 5.0 * pow(max(dot(toEye, uSunDir), 0.0), 12.0);
}
