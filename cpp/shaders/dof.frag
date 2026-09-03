#version 410 core
// Half-resolution depth-of-field blur. Runs only while the camera is aiming
// (see Renderer::renderFrame); the composite pass blends this in by the
// same circle-of-confusion it's computed with here, so background softness
// ramps in with the aim blend rather than snapping on.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tScene;
uniform sampler2D tDepth;
uniform vec2 uTexel;
uniform float uNear;
uniform float uFar;
uniform float uFocus;
uniform float uRange;
uniform float uMaxRadius;

float linearDepth(vec2 uv) {
  float d = texture(tDepth, uv).r;
  float ndc = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - ndc * (uFar - uNear));
}

void main() {
  float dist = linearDepth(vUv);
  float coc = smoothstep(uFocus, uFocus + uRange, dist);
  float r = coc * uMaxRadius;

  vec3 sum = texture(tScene, vUv).rgb;
  float w = 1.0;
  for (int i = 0; i < 12; i++) {
    float a = float(i) * 0.5236;              // 30 degrees apart
    float ring = (i < 6) ? 0.55 : 1.0;         // two rings, not one
    vec2 o = vec2(cos(a), sin(a)) * uTexel * r * ring;
    sum += texture(tScene, vUv + o).rgb;
    w += 1.0;
  }
  fragColor = vec4(sum / w, 1.0);
}
