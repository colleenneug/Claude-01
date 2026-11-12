#version 410 core
// Split-sum prefilter (Karis, "Real Shading in Unreal Engine 4"): each mip
// of the specular probe is the environment seen through a GGX lobe of one
// roughness, so a rough surface samples a blurred probe once instead of
// integrating a sharp one per pixel.
//
// This replaces a box-filtered mip chain. A box filter averages a square of
// texels; a GGX lobe has a bright core and a long tail, and it is the tail
// that makes a highlight on brushed metal look like a highlight on brushed
// metal rather than a smaller, blurrier copy of the light.
in vec2 vUv;
out vec4 fragColor;

uniform samplerCube uEnv;
uniform mat4 uInvViewProj;
uniform float uRoughness;
uniform float uEnvSize;      // face size of uEnv's top mip, in texels

const float PI = 3.14159265359;
const uint SAMPLES = 48u;

float radicalInverse(uint bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10;
}
vec2 hammersley(uint i, uint n) { return vec2(float(i) / float(n), radicalInverse(i)); }

vec3 importanceSampleGGX(vec2 xi, vec3 n, float a) {
  float phi = 2.0 * PI * xi.x;
  float cosTheta = sqrt((1.0 - xi.y) / (1.0 + (a * a - 1.0) * xi.y));
  float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
  vec3 h = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
  vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 tx = normalize(cross(up, n));
  vec3 ty = cross(n, tx);
  return normalize(tx * h.x + ty * h.y + n * h.z);
}

void main() {
  vec4 far = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
  vec3 n = normalize(far.xyz / far.w);

  if (uRoughness < 0.02) {
    fragColor = vec4(textureLod(uEnv, n, 0.0).rgb, 1.0);
    return;
  }

  float a = uRoughness * uRoughness;
  float a2 = a * a;
  float texelSolidAngle = 4.0 * PI / (6.0 * uEnvSize * uEnvSize);

  vec3 sum = vec3(0.0);
  float weight = 0.0;
  for (uint i = 0u; i < SAMPLES; i++) {
    vec3 h = importanceSampleGGX(hammersley(i, SAMPLES), n, a);
    vec3 l = normalize(2.0 * dot(n, h) * h - n);
    float nl = dot(n, l);
    if (nl <= 0.0) continue;

    // Each sample stands for a patch of the lobe; read the source at the mip
    // whose texels are about that big. Without this, forty-eight samples of
    // a probe with one bright strip in it is forty-eight chances to hit or
    // miss the strip, and the result is fireflies.
    float nh = max(dot(n, h), 0.0);
    float d = a2 / (PI * pow(nh * nh * (a2 - 1.0) + 1.0, 2.0));
    float pdf = d * 0.25 + 1e-4;
    float sampleSolidAngle = 1.0 / (float(SAMPLES) * pdf);
    float mip = max(0.5 * log2(sampleSolidAngle / texelSolidAngle) + 1.0, 0.0);

    sum += textureLod(uEnv, l, mip).rgb * nl;
    weight += nl;
  }
  fragColor = vec4(sum / max(weight, 1e-4), 1.0);
}
