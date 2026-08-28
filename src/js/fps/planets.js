/* ============================================================
   Orbital destinations.

   Two worlds the cutter can reach once the ark's habitat ring is
   behind you. They are open arenas rather than corridors: a horizon,
   scattered cover, and hostiles in escalating waves until you either
   call extraction or lose your last harness charge.

   Each has a backdrop image if one is present in assets/, and a
   procedurally generated sky if not, so the destinations work with or
   without art.
   ============================================================ */
(function (SF) {
  'use strict';

  const UNLOCK_AFTER = 6;            // clear mission 6 to open the sky

  const DESTINATIONS = [
    {
      id: 'desert',
      name: "THRESHER'S REACH",
      sub: 'DESERT SURVEY STATION · ABANDONED YEAR 4',
      objective: 'Survive the dust shelf. Extract when you have had enough.',
      brief: 'A survey station the ark dropped on its way through, four years before the ' +
             'singing started. The relay still answers. Nothing else does. Whatever came up ' +
             'out of the shelf has had a long time to get used to the heat.',
      image: 'assets/desert-planet-bg.webp',
      sky: { top: '#3a2418', mid: '#c9763a', low: '#f0b46a', sun: '#fff0c0', haze: 0xd08a4a },
      ground: '#8a5a32', rockTint: '#6d4527', fog: 0xc4864a, fogDensity: 0.011,
      light: { key: 0xffd8a0, keyI: 2.2, hemiSky: 0xffb877, hemiGround: 0x4a2f18, hemiI: 1.1 },
      mix: [['drone', 0.45], ['warden', 0.2], ['thrall', 0.35]]
    },
    {
      id: 'frozen',
      name: 'COLD LANTERN',
      sub: 'FROZEN RELAY · LAST TRANSMISSION YEAR 6',
      objective: 'Survive the ice field. Extract when you have had enough.',
      brief: 'The relay that carried the ark\'s last clean message home, six years into the ' +
             'crossing. It is still transmitting on a loop, into a sky that has not changed ' +
             'in forty years. Something down there learned the loop.',
      image: 'assets/frozen-planet-bg.webp',
      sky: { top: '#0a1830', mid: '#4a7fa8', low: '#cfe6f5', sun: '#eaf6ff', haze: 0x8fc2e0 },
      ground: '#c8dceb', rockTint: '#9fc0d8', fog: 0xa8cfe6, fogDensity: 0.014,
      light: { key: 0xdcefff, keyI: 2.0, hemiSky: 0xbfe0ff, hemiGround: 0x3a4a5a, hemiI: 1.25 },
      mix: [['thrall', 0.5], ['drone', 0.3], ['warden', 0.2]]
    }
  ];

  const byId = (id) => DESTINATIONS.find((d) => d.id === id) || null;
  const unlocked = (ch) => SF.campaign.isCleared(ch, UNLOCK_AFTER);

  /* A destination behaves like a campaign mission as far as the game loop is
     concerned, so it can reuse the whole mission pipeline. */
  function asMission(dest) {
    return {
      id: dest.id, n: dest.name, zone: 'arena', from: null,
      name: dest.name, objective: dest.objective, brief: dest.brief,
      waves: [], beats: [
        [0.5, 'DIVISION', 'Cutter is holding at altitude. Call when you want out.'],
        [6.0, 'VOSS', 'Whatever is down there is not part of the composition. It is just hungry.']
      ],
      survival: true, planet: dest
    };
  }

  /* ---------- sky ---------- */

  /* Look for supplied art first; fall back to a generated sky. The bundler
     inlines anything under assets/ as a data URI, so both paths work in the
     single-file build. */
  function resolveAsset(path) {
    const table = window.__ASSETS || {};
    return table[path] || path;
  }

  function proceduralSky(spec) {
    const S = 1024;
    const cv = document.createElement('canvas');
    cv.width = S; cv.height = S / 2;
    const g = cv.getContext('2d');

    const grad = g.createLinearGradient(0, 0, 0, cv.height);
    grad.addColorStop(0, spec.sky.top);
    grad.addColorStop(0.55, spec.sky.mid);
    grad.addColorStop(1, spec.sky.low);
    g.fillStyle = grad;
    g.fillRect(0, 0, cv.width, cv.height);

    // a sun low on the horizon
    const sx = S * 0.66, sy = cv.height * 0.62;
    const halo = g.createRadialGradient(sx, sy, 0, sx, sy, 190);
    halo.addColorStop(0, spec.sky.sun);
    halo.addColorStop(0.18, spec.sky.sun);
    halo.addColorStop(1, 'rgba(255,255,255,0)');
    g.fillStyle = halo;
    g.fillRect(sx - 200, sy - 200, 400, 400);

    /* A companion world, kept well below the zenith: the dome maps the top of
       this canvas to the pole, where anything round gets smeared. */
    const px = S * 0.22, py = cv.height * 0.6, pr = 58;
    const disc = g.createRadialGradient(px - pr * 0.35, py - pr * 0.35, pr * 0.1, px, py, pr);
    disc.addColorStop(0, '#ffffff');
    disc.addColorStop(0.35, spec.sky.mid);
    disc.addColorStop(1, spec.sky.top);
    g.fillStyle = disc;
    g.beginPath(); g.arc(px, py, pr, 0, Math.PI * 2); g.fill();
    g.strokeStyle = 'rgba(255,255,255,.16)';
    g.lineWidth = 5;
    g.lineWidth = 4;
    g.beginPath(); g.ellipse(px, py, pr * 1.8, pr * 0.3, -0.35, 0, Math.PI * 2); g.stroke();

    // banded cloud / dust
    g.globalAlpha = 0.12;
    for (let i = 0; i < 90; i++) {
      const y = Math.random() * cv.height * 0.85;
      g.fillStyle = i % 2 ? '#ffffff' : spec.sky.top;
      g.fillRect(0, y, S, 1 + Math.random() * 7);
    }
    g.globalAlpha = 1;

    return new THREE.CanvasTexture(cv);
  }

  function buildSky(scene, spec, onReady) {
    const geo = new THREE.SphereGeometry(240, 40, 24);
    const mat = new THREE.MeshBasicMaterial({ side: THREE.BackSide, fog: false });
    const dome = new THREE.Mesh(geo, mat);
    dome.rotation.y = Math.PI * 0.25;
    scene.add(dome);

    const applyProcedural = () => {
      const t = proceduralSky(spec);
      t.encoding = THREE.sRGBEncoding;
      mat.map = t;
      mat.needsUpdate = true;
      if (onReady) onReady('generated');
    };

    const src = resolveAsset(spec.image);
    if (!src) { applyProcedural(); return dome; }

    new THREE.TextureLoader().load(
      src,
      (tex) => {
        tex.encoding = THREE.sRGBEncoding;
        tex.mapping = THREE.EquirectangularReflectionMapping;
        mat.map = tex;
        mat.needsUpdate = true;
        if (onReady) onReady('image');
      },
      undefined,
      applyProcedural            // no such file — generate one instead
    );
    return dome;
  }

  /* ---------- arena ---------- */

  function buildArena(scene, spec) {
    const colliders = [];
    const emitters = [];
    const group = new THREE.Group();
    const R = 62;                                   // playable radius

    scene.background = new THREE.Color(spec.sky.mid);
    scene.fog = new THREE.FogExp2(spec.fog, spec.fogDensity);
    buildSky(scene, spec);

    /* ground: a wide slab with a scattered rubble texture */
    const groundMat = SF.materials.painted(spec.ground, { repeat: [14, 14] });
    const ground = new THREE.Mesh(new THREE.BoxGeometry(R * 2.4, 1, R * 2.4), groundMat);
    ground.position.y = -0.5;
    ground.receiveShadow = true;
    group.add(ground);

    /* cover: rocks and spires, all solid */
    const rockMat = SF.materials.painted(spec.rockTint, { repeat: [2, 2] });
    const rock = (x, z, r, h) => {
      const m = new THREE.Mesh(new THREE.DodecahedronGeometry(r, 0), rockMat);
      m.position.set(x, h * 0.35, z);
      m.scale.set(1, h / r, 1);
      m.rotation.set(Math.random(), Math.random() * 6.28, Math.random());
      m.castShadow = m.receiveShadow = true;
      group.add(m);
      colliders.push({ min: { x: x - r * 0.8, z: z - r * 0.8 },
                       max: { x: x + r * 0.8, z: z + r * 0.8 },
                       top: h, bottom: 0 });
    };

    for (let i = 0; i < 44; i++) {
      const ang = Math.random() * Math.PI * 2;
      const dist = 9 + Math.random() * (R - 14);
      const r = 1.1 + Math.random() * 2.6;
      rock(Math.cos(ang) * dist, Math.sin(ang) * dist, r, r * (1.2 + Math.random() * 2.2));
    }
    // a ring of larger spires marking the edge of the playable ground
    for (let i = 0; i < 26; i++) {
      const ang = (i / 26) * Math.PI * 2 + Math.random() * 0.1;
      rock(Math.cos(ang) * (R + 3), Math.sin(ang) * (R + 3), 4 + Math.random() * 2.5, 14);
    }

    /* the extraction beacon you stand on to leave */
    const pad = new THREE.Mesh(new THREE.CylinderGeometry(3.4, 3.8, 0.4, 24),
      SF.materials.get('deck'));
    pad.position.set(0, 0.2, 0);
    pad.receiveShadow = true;
    group.add(pad);
    const beacon = new THREE.Mesh(new THREE.CylinderGeometry(0.35, 0.35, 3.4, 10),
      SF.materials.emissive(0x7dff9b, 2.4));
    beacon.position.set(0, 1.9, 0);
    group.add(beacon);
    emitters.push({ x: 0, y: 3.4, z: 0, colour: 0x7dff9b, intensity: 2.6, distance: 22 });

    for (let i = 0; i < 5; i++) {
      const ang = (i / 5) * Math.PI * 2;
      emitters.push({ x: Math.cos(ang) * 26, y: 5, z: Math.sin(ang) * 26,
                      colour: spec.sky.haze, intensity: 2.2, distance: 34 });
    }

    scene.add(group);

    return {
      group, colliders, emitters, props: [], nodeAnchors: [],
      zones: { arena: { x0: -R, x1: R, z0: -R, z1: R, cx: 0, cz: 0, name: spec.name } },
      playerStart: new THREE.Vector3(0, 0, 6),
      bounds: { minX: -R, maxX: R, minZ: -R, maxZ: R },
      extraction: new THREE.Vector3(0, 0, 0),
      beacon: beacon
    };
  }

  SF.planets = { DESTINATIONS, byId, unlocked, asMission, buildArena, buildSky,
                 UNLOCK_AFTER, resolveAsset };
})(window.SF);
