#version 410 core
// A 3x3 tent, blended additively into the next larger level (the caller
// enables GL_ONE, GL_ONE blending). Summing the levels on the way up is
// what gives bloom a wide, smooth skirt instead of a visible ring.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tSource;
uniform vec2 uTexel;
uniform float uRadius;
uniform float uStrength;

void main() {
  vec2 t = uTexel * uRadius;
  vec3 s = texture(tSource, vUv + vec2(-t.x,  t.y)).rgb
         + texture(tSource, vUv + vec2( 0.0,  t.y)).rgb * 2.0
         + texture(tSource, vUv + vec2( t.x,  t.y)).rgb
         + texture(tSource, vUv + vec2(-t.x,  0.0)).rgb * 2.0
         + texture(tSource, vUv                    ).rgb * 4.0
         + texture(tSource, vUv + vec2( t.x,  0.0)).rgb * 2.0
         + texture(tSource, vUv + vec2(-t.x, -t.y)).rgb
         + texture(tSource, vUv + vec2( 0.0, -t.y)).rgb * 2.0
         + texture(tSource, vUv + vec2( t.x, -t.y)).rgb;
  fragColor = vec4(s * (uStrength / 16.0), 1.0);
}
