#version 410 core
// Diffuse half of the probe: for each direction, everything the hemisphere
// around it can see, weighted by the cosine. A matte surface facing that way
// is lit by exactly this. Sampling a near-top mip of the specular chain was
// the stand-in before; it is roughly the right blur and the wrong weighting,
// and it leaks light from behind the surface.
in vec2 vUv;
out vec4 fragColor;

uniform samplerCube uEnv;
uniform mat4 uInvViewProj;
uniform float uEnvSize;

const float PI = 3.14159265359;
const uint SAMPLES = 96u;

float radicalInverse(uint bits) {
  bits = (bits << 16u) | (bits >> 16u);
  bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
  bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
  bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
  bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
  return float(bits) * 2.3283064365386963e-10;
}

void main() {
  vec4 far = uInvViewProj * vec4(vUv * 2.0 - 1.0, 1.0, 1.0);
  vec3 n = normalize(far.xyz / far.w);
  vec3 up = abs(n.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
  vec3 tx = normalize(cross(up, n));
  vec3 ty = cross(n, tx);

  float texelSolidAngle = 4.0 * PI / (6.0 * uEnvSize * uEnvSize);
  vec3 sum = vec3(0.0);
  for (uint i = 0u; i < SAMPLES; i++) {
    // Cosine-weighted, so the cosine is in the distribution and every sample
    // counts the same.
    float u = float(i) / float(SAMPLES), v = radicalInverse(i);
    float phi = 2.0 * PI * u;
    float cosTheta = sqrt(1.0 - v), sinTheta = sqrt(v);
    vec3 l = tx * cos(phi) * sinTheta + ty * sin(phi) * sinTheta + n * cosTheta;
    float pdf = cosTheta / PI + 1e-4;
    float mip = max(0.5 * log2(1.0 / (float(SAMPLES) * pdf) / texelSolidAngle) + 1.0, 0.0);
    sum += textureLod(uEnv, l, mip).rgb;
  }
  fragColor = vec4(sum / float(SAMPLES), 1.0);
}
