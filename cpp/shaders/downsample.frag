#version 410 core
// The thirteen-tap filter from Call of Duty's "Next Generation Post
// Processing" — a plain box filter flickers badly on small bright things
// at these mip sizes; this one is stable.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tSource;
uniform vec2 uTexel;

void main() {
  vec2 t = uTexel;
  vec3 a = texture(tSource, vUv + t * vec2(-2.0,  2.0)).rgb;
  vec3 b = texture(tSource, vUv + t * vec2( 0.0,  2.0)).rgb;
  vec3 c = texture(tSource, vUv + t * vec2( 2.0,  2.0)).rgb;
  vec3 d = texture(tSource, vUv + t * vec2(-2.0,  0.0)).rgb;
  vec3 e = texture(tSource, vUv                        ).rgb;
  vec3 f = texture(tSource, vUv + t * vec2( 2.0,  0.0)).rgb;
  vec3 g = texture(tSource, vUv + t * vec2(-2.0, -2.0)).rgb;
  vec3 h = texture(tSource, vUv + t * vec2( 0.0, -2.0)).rgb;
  vec3 i = texture(tSource, vUv + t * vec2( 2.0, -2.0)).rgb;
  vec3 j = texture(tSource, vUv + t * vec2(-1.0,  1.0)).rgb;
  vec3 k = texture(tSource, vUv + t * vec2( 1.0,  1.0)).rgb;
  vec3 l = texture(tSource, vUv + t * vec2(-1.0, -1.0)).rgb;
  vec3 m = texture(tSource, vUv + t * vec2( 1.0, -1.0)).rgb;

  vec3 o = (j + k + l + m) * 0.5 * 0.25
         + (a + b + d + e) * 0.125 * 0.25
         + (b + c + e + f) * 0.125 * 0.25
         + (d + e + g + h) * 0.125 * 0.25
         + (e + f + h + i) * 0.125 * 0.25;
  fragColor = vec4(o, 1.0);
}
