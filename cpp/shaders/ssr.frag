#version 410 core
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tScene;
uniform sampler2D tDepth;
uniform mat4 uProj;
uniform mat4 uInvProj;
uniform mat4 uView;
uniform float uNear;
uniform float uFar;
uniform vec2 uTexelSize;

vec3 viewPosFromDepth(vec2 uv, float depth01) {
  vec4 ndc = vec4(uv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
  vec4 vp = uInvProj * ndc;
  return vp.xyz / vp.w;
}

float linearDepth(float d) {
  float ndc = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - ndc * (uFar - uNear));
}

float edgeFade(vec2 uv) {
  vec2 d = smoothstep(0.0, 0.07, uv) * (1.0 - smoothstep(0.93, 1.0, uv));
  return d.x * d.y;
}

void main() {
  float depth01 = texture(tDepth, vUv).r;
  if (depth01 >= 0.99999) { fragColor = vec4(0.0); return; }

  vec3 viewPos = viewPosFromDepth(vUv, depth01);

  vec3 ddx = dFdx(viewPos);
  vec3 ddy = dFdy(viewPos);
  vec3 viewNormal = normalize(cross(ddy, ddx));

  float depthDisc = length(ddx) + length(ddy);
  float smoothness = 1.0 - smoothstep(0.02, 0.15, depthDisc);
  if (smoothness < 0.05) { fragColor = vec4(0.0); return; }

  vec3 viewDir = normalize(viewPos);
  vec3 reflDir = reflect(viewDir, viewNormal);

  float fresnel = pow(1.0 - max(dot(viewNormal, -viewDir), 0.0), 3.0);
  float confidence = fresnel * smoothness;
  if (confidence < 0.01) { fragColor = vec4(0.0); return; }

  float rayLen = 8.0;
  float stepSize = rayLen / 24.0;
  vec3 rayStep = reflDir * stepSize;

  vec3 rayPos = viewPos + reflDir * 0.05;
  vec3 hitColor = vec3(0.0);
  float hitConf = 0.0;

  for (int i = 0; i < 24; i++) {
    rayPos += rayStep;

    vec4 projPos = uProj * vec4(rayPos, 1.0);
    vec2 sampleUv = (projPos.xy / projPos.w) * 0.5 + 0.5;

    if (sampleUv.x < 0.0 || sampleUv.x > 1.0 ||
        sampleUv.y < 0.0 || sampleUv.y > 1.0) break;

    float sampleDepth = texture(tDepth, sampleUv).r;
    if (sampleDepth >= 0.99999) continue;

    vec3 sampleViewPos = viewPosFromDepth(sampleUv, sampleDepth);
    float depthDiff = rayPos.z - sampleViewPos.z;

    if (depthDiff > 0.0 && depthDiff < stepSize * 2.0) {
      vec3 refinePos = rayPos;
      vec3 refineStep = rayStep * 0.5;
      for (int j = 0; j < 4; j++) {
        refinePos -= refineStep;
        refineStep *= 0.5;
        vec4 rp = uProj * vec4(refinePos, 1.0);
        vec2 ruv = (rp.xy / rp.w) * 0.5 + 0.5;
        float rd = texture(tDepth, ruv).r;
        vec3 rvp = viewPosFromDepth(ruv, rd);
        if (refinePos.z > rvp.z) refinePos += refineStep;
      }

      vec4 finalProj = uProj * vec4(refinePos, 1.0);
      vec2 finalUv = (finalProj.xy / finalProj.w) * 0.5 + 0.5;

      float fade = edgeFade(finalUv);
      float distFade = 1.0 - smoothstep(0.5, 1.0, float(i) / 24.0);

      hitColor = texture(tScene, finalUv).rgb;
      hitConf = confidence * fade * distFade;
      break;
    }
  }

  fragColor = vec4(hitColor, clamp(hitConf, 0.0, 1.0));
}
