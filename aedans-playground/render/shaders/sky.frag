#version 460
#extension GL_GOOGLE_include_directive : require
// =============================================================================
//  sky.frag — the environment, seen behind everything, and the sun's disk.
//
//  The environment cube at full detail (mip 0) in this pixel's direction.
//  The sun's disk is added here, from FrameData::sun, not baked into the
//  cube: it follows the light, and the bakes do not count it twice.
// =============================================================================
#include "frame.glsl"

layout(location = 0) in vec4 inNear;
layout(location = 1) in vec4 inFar;
layout(location = 0) out vec4 outColour;

void main() {
  vec3 direction = normalize(inFar.xyz / inFar.w - inNear.xyz / inNear.w);
  vec3 colour = textureLod(environmentMap, direction, 0.0).rgb;
  // The sun: about two and a half times its real size (0.53 degrees), so
  // it reads on screen; a soft edge so it is not a hard-edged coin.
  float c = dot(direction, -frame.sunDirection.xyz);
  colour += frame.sunColour.rgb * 12.0 * smoothstep(0.99990, 0.99997, c);
  outColour = vec4(colour, 1.0);  // linear HDR: tone mapped by the composite pass
}
