/* ============================================================
   Cascaded shadow maps.

   One directional light covering a hundred and forty metres with a
   2048 map spends about seven centimetres of shadow map per texel, so
   a low sun throws shadows with staircase edges and the contact shadow
   under your own boots disappears entirely. Cascades fix that by
   splitting the view distance into slices and giving each slice its own
   shadow map, fitted tightly: the nearest slice gets centimetre texels,
   the farthest still reaches the horizon.

   three ships CSM only as an ES module, and this project stays on
   classic scripts, so this is a compact hand-rolled equivalent:

     * N directional lights, all pointing the same way, added once so
       the scene's light count never changes (see fps/lights.js for why
       that matters).
     * Each frame every cascade's shadow camera is refitted to a bounding
       sphere around its slice of the view frustum, and snapped to its own
       texel grid so the edges do not crawl when you walk.
     * A fragment patch (registered with fps/shading.js) masks each
       cascade light to its own depth band, with a soft overlap so the
       seam between cascades is not a visible line.

   Load order: after fps/shading.js.
   ============================================================ */
(function (SF) {
  'use strict';

  let active = null;          // the one rig, or null when no mission is running

  /* ---------- the fragment patch ----------
     Registered once, at load. It emits nothing unless a rig is running,
     and its cache key changes with the cascade count so a program built
     for three cascades is never reused for four. */
  SF.shading.register({
    name: 'csm',
    key() { return active ? 'c' + active.cascades : ''; },
    apply(shader, material) {
      if (!active || material.userData.pbr === 'skip') return;
      const n = active.cascades;

      shader.uniforms.uCsmRanges = { value: active.ranges };
      shader.uniforms.uCsmFade = { value: active.fade };

      SF.shading.declare(shader, `
uniform vec2 uCsmRanges[ ${n} ];
uniform float uCsmFade;

/* 1 inside this cascade's depth band, falling off across uCsmFade at each
   edge. Neighbouring bands use the same edges with opposite slopes, so the
   two always sum to one and the handover is invisible. */
float sfCsmWeight( vec2 band ) {
  float d = - vViewPosition.z;
  float f = max( uCsmFade, 0.001 );
  return smoothstep( band.x - f, band.x + f, d ) *
         ( 1.0 - smoothstep( band.y - f, band.y + f, d ) );
}`);

      /* The directional loop is unrolled by three, so UNROLLED_LOOP_INDEX
         is a literal by the time this compiles and the array index is
         constant. Cascade lights are added to the scene first, so they
         occupy directional slots 0..n-1; any other directional light in
         the scene sits past the guard and is left alone. */
      SF.shading.patchChunk(shader, 'lights_fragment_begin',
        'getDirectionalLightInfo( directionalLight, geometry, directLight );',
        'getDirectionalLightInfo( directionalLight, geometry, directLight );\n' +
        '\t\t#if UNROLLED_LOOP_INDEX < ' + n + '\n' +
        '\t\tdirectLight.color *= sfCsmWeight( uCsmRanges[ UNROLLED_LOOP_INDEX ] );\n' +
        '\t\t#endif');
    }
  });

  /* ---------- scratch ---------- */
  const _dir = new THREE.Vector3();
  const _centre = new THREE.Vector3();
  const _corner = new THREE.Vector3();
  const _rot = new THREE.Matrix4();
  const _inv = new THREE.Matrix4();
  const _up = new THREE.Vector3(0, 1, 0);
  const _altUp = new THREE.Vector3(0, 0, 1);
  const _corners = [];
  for (let i = 0; i < 8; i++) _corners.push(new THREE.Vector3());

  /* ---------- the rig ---------- */
  function create(scene, camera, opts) {
    const o = Object.assign({
      cascades: 3,
      near: 1,                     // cascade 0 starts here, not at camera.near
      far: 220,                    // beyond this the sun still lights, unshadowed
      lambda: 0.7,                 // 0 uniform splits, 1 fully logarithmic
      mapSizes: [2048, 2048, 1024, 1024],
      fade: 2.5,                   // metres of overlap between cascades
      bias: -0.0004,
      normalBias: 0.02,
      radius: 1.6,                 // PCF softness
      backOff: 90,                 // how far behind a slice casters still count
      colour: 0xffffff,
      intensity: 2.0,
      direction: new THREE.Vector3(-0.42, -0.26, -0.34).normalize()
    }, opts || {});

    const n = Math.max(1, Math.min(4, o.cascades | 0));
    const lights = [];
    const ranges = [];
    for (let i = 0; i < n; i++) ranges.push(new THREE.Vector2(0, 1e5));

    /* Every cascade is added before anything else that lights the scene,
       so the fragment patch can rely on them holding slots 0..n-1. */
    for (let i = 0; i < n; i++) {
      const light = new THREE.DirectionalLight(o.colour, o.intensity);
      light.castShadow = true;
      light.shadow.mapSize.setScalar(o.mapSizes[Math.min(i, o.mapSizes.length - 1)]);
      light.shadow.bias = o.bias;
      light.shadow.normalBias = o.normalBias;
      light.shadow.radius = o.radius;
      const cam = light.shadow.camera;
      cam.near = 0.5;
      cam.far = 500;
      scene.add(light);
      scene.add(light.target);
      lights.push(light);
    }

    /* Practical split scheme: logarithmic splits keep near texels tiny but
       waste the far cascades, uniform splits do the opposite, so blend. */
    const splits = [o.near];
    for (let i = 1; i < n; i++) {
      const p = i / n;
      const log = o.near * Math.pow(o.far / o.near, p);
      const uni = o.near + (o.far - o.near) * p;
      splits.push(o.lambda * log + (1 - o.lambda) * uni);
    }
    splits.push(o.far);

    for (let i = 0; i < n; i++) {
      // the first band has no lower edge and the last none above, so the
      // sun still lights whatever falls outside the shadowed range
      ranges[i].set(i === 0 ? -1e4 : splits[i], i === n - 1 ? 1e5 : splits[i + 1]);
    }

    const rig = {
      cascades: n, ranges, fade: o.fade, lights,
      direction: o.direction.clone(),

      /* Point the sun. Low angles are the whole point: a sun a few degrees
         above the horizon is what draws shadows the length of a courtyard. */
      setDirection(v) { rig.direction.copy(v).normalize(); },

      setIntensity(v) { for (const l of lights) l.intensity = v; },
      setColour(c) { for (const l of lights) l.color.set(c); },

      /* Refit every cascade to the camera. Called once a frame, before the
         renderer walks the shadow maps. */
      update() {
        const cam = camera;
        cam.updateMatrixWorld();
        _dir.copy(rig.direction);

        // a light-space rotation, used only to snap the centre to texels
        const up = Math.abs(_dir.y) > 0.99 ? _altUp : _up;
        _rot.lookAt(_centre.set(0, 0, 0), _dir, up);
        _inv.copy(_rot).invert();

        const tanHalf = Math.tan(THREE.MathUtils.degToRad(cam.fov * 0.5));

        for (let i = 0; i < n; i++) {
          const nearD = splits[i], farD = splits[i + 1];

          // the slice's eight corners, view space then world
          let k = 0;
          for (const d of [nearD, farD]) {
            const h = tanHalf * d, w = h * cam.aspect;
            for (const sx of [-1, 1]) {
              for (const sy of [-1, 1]) {
                _corners[k++].set(sx * w, sy * h, -d).applyMatrix4(cam.matrixWorld);
              }
            }
          }

          /* Fit a sphere rather than a box: a sphere is the same size
             whichever way the camera is facing, so the shadow map does not
             resize — and so its texels do not shimmer — as you turn. */
          _centre.set(0, 0, 0);
          for (let c = 0; c < 8; c++) _centre.add(_corners[c]);
          _centre.multiplyScalar(1 / 8);
          let radius = 0;
          for (let c = 0; c < 8; c++) {
            radius = Math.max(radius, _corner.copy(_corners[c]).sub(_centre).length());
          }
          radius = Math.ceil(radius * 8) / 8;      // quantise so it stops breathing

          // snap the centre to this cascade's own texel grid
          const light = lights[i];
          const texel = (radius * 2) / light.shadow.mapSize.x;
          _centre.applyMatrix4(_inv);
          _centre.x = Math.floor(_centre.x / texel) * texel;
          _centre.y = Math.floor(_centre.y / texel) * texel;
          _centre.applyMatrix4(_rot);

          light.position.copy(_centre).addScaledVector(_dir, -(radius + o.backOff));
          light.target.position.copy(_centre);
          light.target.updateMatrixWorld();
          light.updateMatrixWorld();

          const sc = light.shadow.camera;
          sc.left = -radius; sc.right = radius;
          sc.top = radius; sc.bottom = -radius;
          sc.near = 0.5;
          sc.far = radius * 2 + o.backOff * 2;
          sc.updateProjectionMatrix();
        }
      },

      /* Resize the shadow maps in place. three allocates the map lazily from
         mapSize, so dropping the existing one is what makes a new size take
         effect; it costs one reallocation, which is why this only ever runs
         on a quality change. */
      setMapSizes(sizes) {
        for (let i = 0; i < lights.length; i++) {
          const want = sizes[Math.min(i, sizes.length - 1)];
          const shadow = lights[i].shadow;
          if (shadow.mapSize.x === want) continue;
          shadow.mapSize.setScalar(want);
          if (shadow.map) { shadow.map.dispose(); shadow.map = null; }
        }
      },

      dispose() {
        for (const l of lights) {
          if (l.shadow.map) l.shadow.map.dispose();
          scene.remove(l.target);
          scene.remove(l);
        }
        if (active === rig) active = null;
      }
    };

    /* Materials compiled from here on carry the cascade mask. Anything
       already compiled is invalidated by the caller's enforce() sweep. */
    active = rig;
    return rig;
  }

  SF.csm = { create, get active() { return active; } };
})(window.SF);
