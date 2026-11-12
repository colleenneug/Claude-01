#version 410 core
// One face of the light probe, painted from the same sky function the sky
// pass uses — which is what makes a shadow outdoors the colour of the sky
// above it and a puddle a picture of the clouds.
in vec2 vUv;
out vec4 fragColor;

#include "common/sky.glsl"

uniform mat4 uInvViewProj;
uniform float uSkyIntensity;

void main() {
  vec4 far = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
  vec3 dir = normalize(far.xyz / far.w);
  // No sun disc: the sun is a light in its own right (see pbr.frag), and a
  // probe that also holds it would light every surface with it twice.
  fragColor = vec4(skyRadiance(dir, 0.0, 4) * uSkyIntensity, 1.0);
}
