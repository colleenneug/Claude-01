#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in float aSeed;
layout(location = 2) in float aScale;

uniform mat4 uView;
uniform mat4 uProj;
uniform vec3 uCamPos;
uniform float uBox;
uniform float uTime;
uniform float uSize;
uniform float uDistanceScaled;
uniform vec3 uSunDir;
uniform int uTerrainType;

out float vAlpha;
out float vGlow;

void main() {
  vec3 drift;
  if (uTerrainType == 4) {
    // Volcanic embers: strong upward drift, lateral wander
    drift = vec3(
      sin(uTime * 0.18 + aSeed * 6.2831) * 0.5,
      uTime * 0.4 + sin(uTime * 0.25 + aSeed * 4.0) * 0.3,
      cos(uTime * 0.15 + aSeed * 5.1234) * 0.5);
  } else if (uTerrainType == 1) {
    // Snow: slow, mostly horizontal drift with gentle fall
    drift = vec3(
      sin(uTime * 0.07 + aSeed * 6.2831) * 1.2,
      -uTime * 0.15 + sin(uTime * 0.05 + aSeed * 3.1415) * 0.2,
      cos(uTime * 0.06 + aSeed * 5.1234) * 1.0);
  } else if (uTerrainType == 3) {
    // Metal sparks: fast, erratic
    drift = vec3(
      sin(uTime * 0.3 + aSeed * 6.2831) * 0.4,
      sin(uTime * 0.22 + aSeed * 3.1415) * 0.3,
      cos(uTime * 0.28 + aSeed * 5.1234) * 0.4);
  } else {
    // Stone / red rock: default gentle float
    drift = vec3(
      sin(uTime * 0.13 + aSeed * 6.2831),
      sin(uTime * 0.09 + aSeed * 3.1415) * 0.6,
      cos(uTime * 0.11 + aSeed * 5.1234)) * 0.7;
  }

  vec3 p = aPosition + drift;

  vec3 rel = p - uCamPos;
  rel = mod(rel + uBox * 0.5, uBox) - uBox * 0.5;
  vec3 world = uCamPos + rel;

  vec4 viewPos = uView * vec4(world, 1.0);
  gl_Position = uProj * viewPos;

  float dist = max(-viewPos.z, 0.05);
  float scaled = (uSize * aScale) / dist;
  gl_PointSize = clamp(mix(uSize * aScale, scaled, uDistanceScaled), 1.0, 24.0);

  float edge = length(rel) / (uBox * 0.5);
  vAlpha = smoothstep(0.25, 1.2, dist) * (1.0 - smoothstep(0.72, 1.0, edge));

  vec3 toEye = normalize(uCamPos - world);
  vGlow = 1.0 + 5.0 * pow(max(dot(toEye, uSunDir), 0.0), 12.0);
}
