/* ============================================================
   Atmosphere: airborne dust, and the parameters the volumetric fog in
   fps/engine.js integrates against.

   A light beam you cannot see is just a bright patch on the floor. What
   sells a shaft of light is the stuff floating in it, so this hangs a
   few thousand motes around the camera and lets them catch the sun.

   The motes are one draw call and cost nothing on the CPU: their
   positions are fixed in the buffer and the vertex shader wraps them
   into a box that follows the camera, so walking through them gives
   real parallax without ever touching the attribute array.
   ============================================================ */
(function (SF) {
  'use strict';

  /* How thick the air is, per environment. density is the fog at the
     camera's own height; falloff is how fast it thins with altitude, so
     a low falloff is a planet-wide haze and a high one is mist pooling
     on the deck. */
  const FOG = {
    ship:    { density: 0.0085, falloff: 0.10, base: -1.5, colour: 0x14212f,
               inscatter: 0.30, motes: 2600, moteBox: 26, moteSize: 30,
               moteColour: 0xbfd8ea, moteOpacity: 0.22 },
    station: { density: 0.0045, falloff: 0.06, base: -3.0, colour: 0x1b2c40,
               inscatter: 0.45, motes: 3400, moteBox: 40, moteSize: 34,
               moteColour: 0xdfeaf5, moteOpacity: 0.18 },
    /* The planet arenas are six hundred metres across, so their fog is
       aerial perspective rather than a room's haze: thin per metre, but
       adding up to a visible fade over that distance. */
    desert:  { density: 0.0020, falloff: 0.022, base: -1.0, colour: 0x6b4a3a,
               inscatter: 0.35, motes: 4200, moteBox: 48, moteSize: 40,
               moteColour: 0xf0cfa4, moteOpacity: 0.24 },
    ice:     { density: 0.0017, falloff: 0.026, base: -1.0, colour: 0x9db6c8,
               inscatter: 0.28, motes: 4200, moteBox: 48, moteSize: 38,
               moteColour: 0xe8f4ff, moteOpacity: 0.22 }
  };

  const MOTE_VERT = `
uniform vec3 uCamPos;
uniform float uBox;
uniform float uTime;
uniform float uSize;
uniform float uPixelRatio;
uniform vec3 uSunDir;          // direction the light travels, world space

attribute float aSeed;
attribute float aScale;

varying float vAlpha;
varying float vGlow;

void main() {
  /* Drift: each mote wanders on its own slow figure, seeded so no two
     move together. Real dust is pushed around by air, not falling. */
  vec3 p = position + vec3(
    sin( uTime * 0.13 + aSeed * 6.2831 ),
    sin( uTime * 0.09 + aSeed * 3.1415 ) * 0.6,
    cos( uTime * 0.11 + aSeed * 5.1234 ) ) * 0.7;

  /* Wrap into a box centred on the camera. The buffer never changes;
     the same mote simply reappears on the far side once you pass it. */
  vec3 rel = p - uCamPos;
  rel = mod( rel + uBox * 0.5, uBox ) - uBox * 0.5;
  vec3 world = uCamPos + rel;

  vec4 mv = viewMatrix * vec4( world, 1.0 );
  gl_Position = projectionMatrix * mv;

  float dist = max( - mv.z, 0.05 );
  gl_PointSize = ( uSize * aScale * uPixelRatio ) / dist;

  /* Fade in past the near plane, and out at the edge of the box so motes
     do not pop into existence at the boundary. */
  float edge = length( rel ) / ( uBox * 0.5 );
  vAlpha = smoothstep( 0.25, 1.2, dist ) * ( 1.0 - smoothstep( 0.72, 1.0, edge ) );

  /* Catching the light: a mote between you and the sun scatters forward,
     which is why dust is invisible until you turn into the beam. */
  vec3 toEye = normalize( uCamPos - world );
  vGlow = 1.0 + 5.0 * pow( max( dot( toEye, uSunDir ), 0.0 ), 12.0 );
}`;

  const MOTE_FRAG = `
uniform vec3 uColour;
uniform float uOpacity;
varying float vAlpha;
varying float vGlow;

void main() {
  float d = length( gl_PointCoord - vec2( 0.5 ) );
  float a = smoothstep( 0.5, 0.08, d );
  if ( a <= 0.001 ) discard;
  gl_FragColor = vec4( uColour * vGlow, a * vAlpha * uOpacity );
}`;

  function motes(scene, spec) {
    const count = spec.motes;
    const box = spec.moteBox;
    const pos = new Float32Array(count * 3);
    const seed = new Float32Array(count);
    const scale = new Float32Array(count);
    for (let i = 0; i < count; i++) {
      pos[i * 3]     = (Math.random() - 0.5) * box;
      pos[i * 3 + 1] = (Math.random() - 0.5) * box;
      pos[i * 3 + 2] = (Math.random() - 0.5) * box;
      seed[i] = Math.random();
      // a few large motes among many small ones reads far better than one size
      scale[i] = 0.35 + Math.pow(Math.random(), 3) * 1.9;
    }

    const geo = new THREE.BufferGeometry();
    geo.setAttribute('position', new THREE.BufferAttribute(pos, 3));
    geo.setAttribute('aSeed', new THREE.BufferAttribute(seed, 1));
    geo.setAttribute('aScale', new THREE.BufferAttribute(scale, 1));

    const mat = new THREE.ShaderMaterial({
      uniforms: {
        uCamPos: { value: new THREE.Vector3() },
        uBox: { value: box },
        uTime: { value: 0 },
        uSize: { value: spec.moteSize },
        uPixelRatio: { value: 1 },
        uSunDir: { value: new THREE.Vector3(0, -1, 0) },
        uColour: { value: new THREE.Color(spec.moteColour) },
        uOpacity: { value: spec.moteOpacity }
      },
      vertexShader: MOTE_VERT,
      fragmentShader: MOTE_FRAG,
      transparent: true,
      depthWrite: false,          // dust never occludes anything
      depthTest: true,            // but a wall occludes it
      blending: THREE.AdditiveBlending
    });

    const points = new THREE.Points(geo, mat);
    points.frustumCulled = false;  // the shader moves them; the bounds are a lie
    points.renderOrder = 3;
    points.matrixAutoUpdate = false;
    scene.add(points);
    return { points, geo, mat };
  }

  /* preset: one of the FOG keys. Returns the rig; call update() each frame
     and hand fog to the engine once at build. */
  function create(scene, renderer, preset, opts) {
    const spec = Object.assign({}, FOG[preset] || FOG.ship, opts || {});
    const dust = motes(scene, spec);
    dust.mat.uniforms.uPixelRatio.value = renderer.getPixelRatio();

    return {
      spec,
      /* Everything the engine's fog integral needs, in one object. */
      fog: {
        density: spec.density, falloff: spec.falloff, base: spec.base,
        colour: new THREE.Color(spec.colour), inscatter: spec.inscatter
      },
      setSun(dirWorld) { dust.mat.uniforms.uSunDir.value.copy(dirWorld).normalize(); },
      setDensity(v) { dust.mat.uniforms.uOpacity.value = v; },
      update(dt, camPos, time) {
        dust.mat.uniforms.uTime.value = time;
        dust.mat.uniforms.uCamPos.value.copy(camPos);
      },
      dispose() {
        scene.remove(dust.points);
        dust.geo.dispose();
        dust.mat.dispose();
      }
    };
  }

  SF.atmos = { create, FOG };
})(window.SF);
