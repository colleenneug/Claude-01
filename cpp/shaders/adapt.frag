#version 410 core
// Eye adaptation: move the exposure toward what this frame asks for, over
// time, the way an eye or a camera does. One texel, read back next frame.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tLuminance;   // log-luminance, mipmapped
uniform sampler2D tPrevious;    // last frame's adapted log-luminance, 1x1
uniform float uTopMip;
uniform float uRate;            // 1 - exp(-dt * speed); 1 snaps

void main() {
  float target = textureLod(tLuminance, vec2(0.5), uTopMip).r;
  float previous = texelFetch(tPrevious, ivec2(0), 0).r;
  fragColor = vec4(mix(previous, target, uRate), 0.0, 0.0, 1.0);
}
