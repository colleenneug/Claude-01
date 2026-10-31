#version 410 core
// Final composite: depth-of-field blend, bloom, volumetric fog, lens
// aberration, ACES filmic tone mapping, sRGB encode, grain, vignette. The
// scene arrives here as linear HDR radiance with no tone mapping applied —
// see Renderer::create, which sets NoToneMapping-equivalent behaviour by
// simply never calling a tone-mapping step before this pass — which is what
// lets bloom and the fog's sun inscatter see highlights a tone-mapped
// buffer would already have thrown away.
in vec2 vUv;
out vec4 fragColor;

uniform sampler2D tScene;
uniform sampler2D tDepth;
uniform sampler2D tBloom;
uniform sampler2D tDof;
uniform sampler2D tAO;
uniform float uSSAO;

uniform mat4 uInvViewProj;
uniform vec3 uCamPos;
uniform float uNear;
uniform float uFar;

uniform float uAim;
uniform float uDofFocus;
uniform float uDofRange;

uniform float uFogDensity;
uniform float uFogFalloff;
uniform float uFogBase;
uniform vec3 uFogColour;
uniform float uInscatter;

uniform vec3 uSunDir;
uniform vec3 uSunColour;

// The sky. Until this existed the background was simply the clear colour,
// which the fog below then blended to fully — so every outdoor frame had a
// flat wash of fog colour above the horizon and no sun in it anywhere, and
// the light in the scene came from a direction the picture never showed.
uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform float uSkyIntensity;   // 0 keeps the clear colour, for open space

uniform float uExposure;
uniform float uTime;
uniform float uGrain;
uniform float uVignette;
uniform float uAberration;
uniform float uBloom;
uniform vec2 uSunScreenPos;   // sun projected onto screen, for light shafts
uniform float uShaftIntensity; // 0 disables light shafts

float linearDepth(float d) {
  float ndc = d * 2.0 - 1.0;
  return (2.0 * uNear * uFar) / (uFar + uNear - ndc * (uFar - uNear));
}

// Reconstruct the world-space position a fragment's depth corresponds to,
// by unprojecting its NDC coordinate through the inverse view-projection
// matrix — cleaner than carrying the tan(fov) projection parameters through
// from the camera, now that the full matrices are on hand either way.
vec3 worldPosFromDepth(vec2 uv, float depth01) {
  vec4 ndc = vec4(uv * 2.0 - 1.0, depth01 * 2.0 - 1.0, 1.0);
  vec4 wp = uInvViewProj * ndc;
  return wp.xyz / wp.w;
}

// three's ACES fit, so the grade matches whichever renderer the reader
// compares this against.
vec3 rrtOdtFit(vec3 v) {
  vec3 a = v * (v + 0.0245786) - 0.000090537;
  vec3 b = v * (0.983729 * v + 0.4329510) + 0.238081;
  return a / b;
}
vec3 acesFilm(vec3 colour) {
  const mat3 IN = mat3(
    vec3(0.59719, 0.07600, 0.02840),
    vec3(0.35458, 0.90834, 0.13383),
    vec3(0.04823, 0.01566, 0.83777));
  const mat3 OUT = mat3(
    vec3( 1.60475, -0.10208, -0.00327),
    vec3(-0.53108,  1.10813, -0.07276),
    vec3(-0.07367, -0.00605,  1.07602));
  colour = IN * (colour / 0.6);
  colour = rrtOdtFit(colour);
  return clamp(OUT * colour, 0.0, 1.0);
}

vec3 linearToSRGB(vec3 c) {
  return mix(pow(c, vec3(0.41666)) * 1.055 - vec3(0.055),
             c * 12.92, vec3(lessThanEqual(c, vec3(0.0031308))));
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

float skyNoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash(i), b = hash(i + vec2(1, 0));
  float c = hash(i + vec2(0, 1)), d = hash(i + vec2(1, 1));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float skyFbm(vec2 p) {
  float sum = 0.0, amp = 0.5;
  for (int i = 0; i < 4; i++) {
    sum += skyNoise(p) * amp;
    p *= 2.17;
    amp *= 0.48;
  }
  return sum;
}

vec3 skyColour(vec3 dir) {
  float up = clamp(dir.y, -1.0, 1.0);

  // Rayleigh-like scattering gradient: more blue at zenith, warmer at horizon
  float zenithMix = pow(clamp(up, 0.0, 1.0), 0.6);
  float horizonMix = pow(clamp(1.0 - abs(up), 0.0, 1.0), 2.5);
  vec3 col = mix(uSkyHorizon, uSkyZenith, zenithMix);
  col = mix(col, uSkyHorizon * 1.15, horizonMix);

  col *= mix(1.0, 0.38, clamp(-up * 4.0, 0.0, 1.0));

  // Procedural clouds: two layers of fbm at different scales, visible
  // only in the upper half of the sky. The clouds thin out at high
  // altitude (near zenith) and thicken near the horizon.
  {
    float cloudFade = smoothstep(0.0, 0.08, up);
    vec2 cloudUv = dir.xz / max(dir.y, 0.06) * 0.6;
    float cloud1 = skyFbm(cloudUv * 0.8 + uTime * 0.002);
    float cloud2 = skyFbm(cloudUv * 2.6 + vec2(7.3, 3.1) + uTime * 0.004);
    float cloud3 = skyFbm(cloudUv * 0.3 + vec2(2.1, 5.7) + uTime * 0.001);
    float cloud = smoothstep(0.38, 0.64, cloud1 * 0.45 + cloud2 * 0.25 + cloud3 * 0.30);
    cloud *= cloudFade;
    cloud *= smoothstep(0.92, 0.15, up);
    float towardsSun = max(dot(dir, -uSunDir), 0.0);
    vec3 cloudLit = mix(uSkyHorizon * 1.6, vec3(0.82, 0.78, 0.74), 0.4);
    cloudLit += uSunColour * pow(towardsSun, 3.0) * 0.35;
    cloudLit += uSunColour * pow(towardsSun, 12.0) * 0.2;
    col = mix(col, cloudLit, cloud * 0.55);
  }

  float towards = max(dot(dir, -uSunDir), 0.0);
  col += uSunColour * pow(towards, 900.0) * 16.0;
  col += uSunColour * pow(towards, 64.0) * 0.18;
  col += uSunColour * pow(towards, 8.0) * 0.30;
  col += uSunColour * pow(towards, 2.0) * 0.07 * horizonMix;
  return col;
}

void main() {
  vec2 uv = vUv;
  vec2 fromCentre = uv - 0.5;
  float r2 = dot(fromCentre, fromCentre);

  // Lens aberration: the three channels focus at slightly different radii,
  // more so towards the edge of the frame.
  float ab = uAberration * 0.0016 * (0.35 + r2);
  vec3 col;
  col.r = texture(tScene, uv + fromCentre * ab).r;
  col.g = texture(tScene, uv).g;
  col.b = texture(tScene, uv - fromCentre * ab).b;

  float depth01 = texture(tDepth, uv).r;
  float dist = linearDepth(depth01);

  // Depth of field, blended in by the same circle-of-confusion the half-res
  // pass computed, scaled by how far down the sights the camera currently is.
  if (uAim > 0.001) {
    float coc = smoothstep(uDofFocus, uDofFocus + uDofRange, dist) * uAim;
    col = mix(col, texture(tDof, uv).rgb, clamp(coc, 0.0, 1.0));
  }

  // Screen-space ambient occlusion: darken corners and contact areas.
  if (uSSAO > 0.5 && depth01 < 0.99999) {
    float ao = texture(tAO, uv).r;
    col *= ao;
  }

  // Volumetric fog: the analytic integral of an exponential height
  // distribution along the view ray, so haze pools in low ground and thins
  // as you climb, and looking towards the sun scatters its colour back.
  vec3 worldPos = worldPosFromDepth(uv, depth01);
  vec3 ray = worldPos - uCamPos;
  float rayLen = length(ray);
  vec3 worldDir = ray / max(rayLen, 1e-4);

  float k = uFogFalloff;
  float dY = worldPos.y - uCamPos.y;
  float t = k * dY;
  float span = (abs(t) < 1e-3) ? rayLen : rayLen * (1.0 - exp(-t)) / t;
  float optical = uFogDensity * exp(-k * (uCamPos.y - uFogBase)) * max(span, 0.0);
  float fogAmount = 1.0 - exp(-optical);

  float towardsSun = max(dot(worldDir, -uSunDir), 0.0);
  vec3 fogColour = uFogColour
    + uSunColour * uInscatter * pow(towardsSun, 8.0)
    + uSunColour * uInscatter * 0.15 * pow(towardsSun, 2.0);

  // Nothing was drawn here, so this is sky. It carries its own horizon haze,
  // so the fog integral — which would run to the far plane and swamp it —
  // does not apply.
  bool isSky = depth01 >= 0.99999;
  if (isSky && uSkyIntensity > 0.001) {
    col = skyColour(worldDir) * uSkyIntensity;
  } else {
    col = mix(col, fogColour, clamp(fogAmount, 0.0, 1.0));
  }

  if (uShaftIntensity > 0.001) {
    vec2 sunUv = uSunScreenPos;
    vec2 delta = (sunUv - uv) * 0.008;
    vec2 tc = uv;
    float shaft = 0.0;
    float decay = 1.0;
    for (int i = 0; i < 48; i++) {
      tc += delta;
      float sd = texture(tDepth, clamp(tc, 0.001, 0.999)).r;
      shaft += smoothstep(0.9999, 1.0, sd) * decay;
      decay *= 0.97;
    }
    shaft /= 48.0;
    float sunDist = length(sunUv - 0.5);
    float sunOnScreen = 1.0 - smoothstep(0.3, 0.8, sunDist);
    sunOnScreen *= smoothstep(-0.3, 0.1, dot(worldDir, -uSunDir));
    col += uSunColour * shaft * uShaftIntensity * sunOnScreen * 0.6;
  }

  // Bloom, added while still linear HDR.
  col += texture(tBloom, uv).rgb * uBloom;

  // Everything above this line is radiance; everything below is a picture.
  col = acesFilm(col * uExposure);
  col = linearToSRGB(col);

  col += (hash(uv * vec2(1024.0, 768.0) + uTime) - 0.5) * uGrain;
  col *= clamp(1.0 - uVignette * r2 * 1.9, 0.0, 1.0);

  fragColor = vec4(col, 1.0);
}
