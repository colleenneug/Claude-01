#version 410 core
// How bright is the frame, on the whole? Log-luminance of the scene into a
// small target; its mip chain then averages it down to one texel. The
// logarithm is the point: the average of a dark room with one lamp in it
// should be "a dark room", and a linear average says "half a lamp".
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tScene;
uniform vec2 uTexel;          // one texel of the scene

void main() {
  // The target is far smaller than the scene, so one tap would read one
  // pixel in several hundred and flicker as things move under it.
  float sum = 0.0;
  for (int y = -1; y <= 1; y++) {
    for (int x = -1; x <= 1; x++) {
      vec3 c = textureLod(tScene, vUv + vec2(x, y) * uTexel * 5.0, 0.0).rgb;
      sum += log(max(dot(c, vec3(0.2126, 0.7152, 0.0722)), 1e-4));
    }
  }
  fragColor = vec4(sum / 9.0, 0.0, 0.0, 1.0);
}
