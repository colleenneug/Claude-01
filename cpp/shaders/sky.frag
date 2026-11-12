#version 410 core
// The sky pass. Until this was its own pass the sky was painted in the final
// composite, after bloom and after anti-aliasing — so the sun, the brightest
// thing in any outdoor frame, was the one bright thing that never bloomed.
// Here it goes into the HDR buffer with everything else.
in vec2 vUv;
out vec4 fragColor;

#include "common/sky.glsl"

uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform float uSkyIntensity;

// Sun shafts (see volumetric.frag). Haze between the eye and the sky that
// sits in something's shadow is darker than haze in the open.
uniform sampler2D tShafts;
uniform float uShaftStrength;

void main() {
  vec4 far = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
  vec3 dir = normalize(far.xyz / far.w - uCamPos);

  vec3 col = skyRadiance(dir, 1.0, 5) * uSkyIntensity;
  float lit = texture(tShafts, vUv).r;
  col *= mix(1.0, 0.72 + 0.28 * lit, uShaftStrength);
  fragColor = vec4(col, 1.0);
}
