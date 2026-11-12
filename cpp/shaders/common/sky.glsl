// The sky, in one place. Three programs need it and they have to agree: the
// sky pass paints it behind the scene, the probe capture turns it into the
// light that fills every shadow and shows in every reflection, and a puddle
// that reflects a different sky from the one overhead is the kind of thing
// nobody can name and everybody notices.
//
// It is a painted sky, not a simulated one. A mission says what colour its
// zenith and its horizon are (content/missions/*.cfg), and that is art
// direction a scattering integral would overrule. What is physical is the
// shape: where the haze sits, how the glow gathers round the sun, how a cloud
// goes dark through its own thickness and bright along the edge the sun is
// behind.

uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform vec3 uSkySunDir;      // the direction the light travels
uniform vec3 uSkySunColour;
uniform vec3 uSkyGround;      // what is below the horizon line
uniform float uCloudCover;    // 0 = clear, 1 = overcast
uniform float uSkyTime;

float skyHash(vec2 p) {
  p = fract(p * vec2(127.1, 311.7));
  p += dot(p, p + 34.23);
  return fract(p.x * p.y);
}
float skyNoise(vec2 p) {
  vec2 i = floor(p), f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(skyHash(i), skyHash(i + vec2(1, 0)), f.x),
             mix(skyHash(i + vec2(0, 1)), skyHash(i + vec2(1, 1)), f.x), f.y);
}
float skyFbm(vec2 p, int octaves) {
  float sum = 0.0, amp = 0.5, norm = 0.0;
  // Each octave is turned a little, or the lattice the noise sits on lines
  // up across octaves and the cloud deck grows visible rows.
  const mat2 turn = mat2(0.8, -0.6, 0.6, 0.8);
  for (int i = 0; i < octaves; i++) {
    sum += skyNoise(p) * amp;
    norm += amp;
    p = turn * p * 2.03 + vec2(7.1, 3.7);
    amp *= 0.52;
  }
  return sum / norm;
}

// How much cloud is in this direction, 0..1. The deck is a flat sheet
// overhead; dividing by height puts the far clouds low and close together
// the way perspective does, and the small constant stops the horizon running
// off to infinity.
float skyCloudDensity(vec2 uv, int octaves) {
  float n = skyFbm(uv, octaves);
  // fbm of value noise lives around 0.5 and almost never reaches its ends, so
  // "cover" is mapped onto the part of the range it actually occupies.
  float threshold = mix(0.74, 0.30, clamp(uCloudCover, 0.0, 1.0));
  return smoothstep(threshold - 0.04, threshold + 0.26, n);
}

// disc: 1 draws the sun itself, 0 leaves it out. The probe leaves it out —
// the sun is already a light, and a probe that also holds it lights
// everything twice.
vec3 skyRadiance(vec3 dir, float disc, int cloudOctaves) {
  float up = clamp(dir.y, -1.0, 1.0);
  float towards = max(dot(dir, -uSkySunDir), 0.0);

  // Zenith to horizon. The haze is a band at the horizon, not a linear
  // blend: you look through forty times as much air sideways as upwards.
  float band = pow(clamp(1.0 - max(up, 0.0), 0.0, 1.0), 3.0);
  vec3 col = mix(uSkyZenith, uSkyHorizon, band);

  // The sky is brighter and warmer on the sun's side, most of all low down.
  col += uSkySunColour * pow(towards, 2.0) * 0.07 * band;

  // Forward scattering round the sun: Henyey-Greenstein, which is the shape
  // of the glow you see through dust — narrow and hot at the centre with a
  // long tail — rather than a power curve picked by eye.
  const float g = 0.80;
  float hg = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * towards, 1.5);
  col += uSkySunColour * hg * 0.0118;

  // The disc: half a degree across in life, a little more here so it is more
  // than a pixel, with the limb darker than the middle.
  float sunDisc = 0.0;
  if (disc > 0.0) {
    const float cosR = 0.99982;                    // ~1.1 degrees across
    float edge = smoothstep(cosR, cosR + 0.00008, towards);
    float limb = mix(0.55, 1.0, smoothstep(cosR, 1.0, towards));
    sunDisc = edge * limb * disc;
  }

  // Clouds.
  if (uCloudCover > 0.005 && up > 0.015) {
    vec2 uv = dir.xz / (up + 0.10) * 0.62 + uSkyTime * vec2(0.0045, 0.0016);
    float d = skyCloudDensity(uv, cloudOctaves);
    if (d > 0.001) {
      // Light the cloud by asking how much cloud lies between here and the
      // sun: step toward it and look again. Thick that way means dark here.
      vec2 sunXZ = -uSkySunDir.xz;
      float sl = length(sunXZ);
      vec2 toSun = sl > 1e-3 ? sunXZ / sl : vec2(1.0, 0.0);
      float d2 = skyCloudDensity(uv + toSun * 0.16, max(cloudOctaves - 1, 2));
      float through = exp(-d2 * 1.9) * (1.0 - 0.45 * d);

      vec3 lit = uSkyHorizon * 1.10 + uSkySunColour * (0.26 + 1.1 * pow(towards, 5.0));
      vec3 shade = mix(uSkyZenith, uSkyHorizon, 0.40) * 0.80;
      vec3 cloud = mix(shade, lit, through);

      float alpha = d * 0.94 * smoothstep(0.015, 0.20, up);
      col = mix(col, cloud, alpha);
      sunDisc *= 1.0 - alpha;
    }
  }

  col += uSkySunColour * sunDisc * 42.0;

  // Below the horizon line there is ground, not a mirrored sky.
  float below = clamp(-up * 7.0, 0.0, 1.0);
  col = mix(col, uSkyGround, below);
  return col;
}
