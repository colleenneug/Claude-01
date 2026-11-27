// =============================================================================
//  post_common.glsl — shared by bloom.frag and tonemap.frag.
//  Must match PostPush in render/src/PostProcess.cpp.
// =============================================================================

layout(set = 0, binding = 0) uniform sampler2D source;  // what this pass reads
layout(set = 0, binding = 1) uniform sampler2D bloom;   // tonemap.frag only: the finished bloom

layout(push_constant) uniform Push {
  vec4 a;  // per pass: see each shader
  vec4 b;
} push;

float luminance(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
