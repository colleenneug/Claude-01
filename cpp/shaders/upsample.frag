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
uniform float uAnamorphic;

void main() {
  float hStretch = 1.0 + uAnamorphic * 2.0;
  float vShrink  = 1.0 / (1.0 + uAnamorphic * 0.5);
  vec2 t = uTexel * uRadius * vec2(hStretch, vShrink);

  vec3 s = texture(tSource, vUv + vec2(-t.x,  t.y)).rgb
         + texture(tSource, vUv + vec2( 0.0,  t.y)).rgb * 2.0
         + texture(tSource, vUv + vec2( t.x,  t.y)).rgb
         + texture(tSource, vUv + vec2(-t.x,  0.0)).rgb * 2.0
         + texture(tSource, vUv                    ).rgb * 4.0
         + texture(tSource, vUv + vec2( t.x,  0.0)).rgb * 2.0
         + texture(tSource, vUv + vec2(-t.x, -t.y)).rgb
         + texture(tSource, vUv + vec2( 0.0, -t.y)).rgb * 2.0
         + texture(tSource, vUv + vec2( t.x, -t.y)).rgb;

  if (uAnamorphic > 0.01) {
    vec2 tWide = t * vec2(2.0, 0.3);
    vec3 extra = texture(tSource, vUv + vec2(-tWide.x, 0.0)).rgb
               + texture(tSource, vUv + vec2( tWide.x, 0.0)).rgb;
    s += extra * uAnamorphic;
    fragColor = vec4(s * (uStrength / (16.0 + 2.0 * uAnamorphic)), 1.0);
  } else {
    fragColor = vec4(s * (uStrength / 16.0), 1.0);
  }
}
