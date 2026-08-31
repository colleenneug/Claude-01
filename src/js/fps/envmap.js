/* ============================================================
   Image-based lighting.

   A MeshStandardMaterial with no environment is lit only by the lamps
   you place, so every metal surface in the game has been reflecting
   nothing — which is why they read as flat grey plastic rather than
   as metal. This builds a small room out of emissive panels, runs it
   through three's prefiltered-mipmap generator, and hands back a cube
   map the whole scene can reflect.

   It costs one render at load and nothing per frame, and it is the
   single largest difference between "untextured 3D" and "a place".
   ============================================================ */
(function (SF) {
  'use strict';

  /* Each preset describes the light around you rather than a picture:
     what is overhead, what is underfoot, and the two or three coloured
     things big enough to show up in a reflection. */
  const PRESETS = {
    ship:    { sky: 0x2a3644, ground: 0x0d1118, key: 0xbfe0ff, keyI: 3.2,
               accents: [[0xffb454, 1.6, -1, 0.2, -0.6], [0x5eeaff, 1.2, 1, 0.1, 0.5]] },
    station: { sky: 0x3a4a5e, ground: 0x141a22, key: 0xfff3e0, keyI: 5.5,
               accents: [[0x2f74c8, 3.0, 0, -0.85, 0.2],     // Earth, underneath
                         [0x5eeaff, 1.4, -1, 0.15, -0.4],
                         [0xffb454, 1.2, 1, 0.2, 0.4]] },
    desert:  { sky: 0x7b3f7a, ground: 0xb06a38, key: 0xffd9a0, keyI: 7.0,
               accents: [[0xe0975c, 2.4, 0, -0.3, -1], [0x2a1038, 0.6, 0, 0.9, 0]] },
    ice:     { sky: 0x8fb4cc, ground: 0x9fb6c8, key: 0xdfeeff, keyI: 6.0,
               accents: [[0x132743, 1.0, 0, 0.8, 0], [0x6fa8d0, 1.8, 1, -0.2, 0.6]] }
  };

  /* An inward-facing box of unlit panels. Unlit is the point: these are
     light sources, not surfaces, so nothing needs to illuminate them. */
  function room(spec) {
    const scene = new THREE.Scene();
    const geo = new THREE.BoxGeometry(1, 1, 1);
    const junk = [geo];

    const panel = (colour, intensity, x, y, z, sx, sy, sz) => {
      const mat = new THREE.MeshBasicMaterial({
        color: new THREE.Color(colour).multiplyScalar(intensity), side: THREE.BackSide
      });
      const m = new THREE.Mesh(geo, mat);
      m.position.set(x, y, z);
      m.scale.set(sx, sy, sz);
      scene.add(m);
      junk.push(mat);
      return m;
    };

    // the shell: sky above, ground below, walls between
    panel(spec.sky, 1, 0, 0, 0, 20, 20, 20);
    panel(spec.ground, 1, 0, -10.5, 0, 20, 1, 20);
    // the key: a bright ceiling panel, which is what a reflection reads as
    panel(spec.key, spec.keyI, 0, 9.2, 0, 9, 0.3, 9);

    for (const [colour, intensity, ax, ay, az] of spec.accents || []) {
      panel(colour, intensity, ax * 8, ay * 8, az * 8, 7, 5, 7);
    }
    return { scene, junk };
  }

  /* Build the cube map. The result belongs to the caller, who disposes it
     with the rest of the mission. */
  function build(renderer, presetName) {
    const spec = PRESETS[presetName] || PRESETS.ship;
    const { scene, junk } = room(spec);
    const pmrem = new THREE.PMREMGenerator(renderer);
    pmrem.compileEquirectangularShader();
    const target = pmrem.fromScene(scene, 0.02);
    pmrem.dispose();
    for (const j of junk) j.dispose();
    return target;
  }

  SF.envmap = { build, PRESETS };
})(window.SF);
