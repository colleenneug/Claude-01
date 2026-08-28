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
      /* Violet sky, twin moons, a far-off spire city — matching the supplied
         backdrop closely enough that the generated fallback is not jarring. */
      sky: { top: '#2a1038', mid: '#7b3f7a', low: '#e0975c', sun: '#ffd9a0', haze: 0xb066a0,
             style: 'twin-moons', city: '#3a2246', ground: '#c2703a' },
      cover: 'ruin',
      ground: '#b06a38', rockTint: '#8a5a3c', fog: 0xa9663f, fogDensity: 0.010,
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
      /* Night sky and stars with a banded gas giant, over a pale ice field. */
      sky: { top: '#050a18', mid: '#132743', low: '#8fb4cc', sun: '#dfeeff', haze: 0x6fa8d0,
             style: 'gas-giant', ground: '#c6dbe8' },
      cover: 'crystal',
      /* Snow is a bright albedo under a strong key: dial both back or the
         whole field blows out to white and takes the HUD with it. */
      ground: '#93aec2', rockTint: '#7fb4d4', fog: 0x86b0cc, fogDensity: 0.012,
      light: { key: 0xdcefff, keyI: 1.05, hemiSky: 0xbfe0ff, hemiGround: 0x2f3d4a, hemiI: 0.7 },
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
    const S = 2048, H = S / 2;
    const cv = document.createElement('canvas');
    cv.width = S; cv.height = H;
    const g = cv.getContext('2d');
    const sky = spec.sky;

    const grad = g.createLinearGradient(0, 0, 0, H);
    grad.addColorStop(0, sky.top);
    grad.addColorStop(0.52, sky.mid);
    grad.addColorStop(0.82, sky.low);
    grad.addColorStop(1, sky.ground || sky.low);
    g.fillStyle = grad;
    g.fillRect(0, 0, S, H);

    const horizon = H * 0.80;

    /* a banded, ringed world, drawn with its terminator on the correct side */
    const gasGiant = (cx, cy, r) => {
      g.save();
      g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); g.clip();
      const body = g.createLinearGradient(cx - r, cy, cx + r, cy);
      body.addColorStop(0, '#f2e4cf');
      body.addColorStop(0.55, '#d8c1a0');
      body.addColorStop(1, '#6a5340');
      g.fillStyle = body;
      g.fillRect(cx - r, cy - r, r * 2, r * 2);
      // latitude bands
      for (let i = -8; i <= 8; i++) {
        const y = cy + (i / 9) * r;
        g.globalAlpha = 0.16 + Math.random() * 0.14;
        g.fillStyle = i % 2 ? '#a8764a' : '#fff3e0';
        g.fillRect(cx - r, y, r * 2, r * (0.055 + Math.random() * 0.05));
      }
      // a storm
      g.globalAlpha = 0.5;
      g.fillStyle = '#c4632f';
      g.beginPath(); g.ellipse(cx + r * 0.24, cy + r * 0.22, r * 0.2, r * 0.11, 0.2, 0, 6.28); g.fill();
      g.globalAlpha = 1;
      g.restore();
      // limb shading
      const shade = g.createRadialGradient(cx - r * 0.4, cy - r * 0.4, r * 0.2, cx, cy, r);
      shade.addColorStop(0, 'rgba(0,0,0,0)');
      shade.addColorStop(1, 'rgba(0,0,0,.55)');
      g.fillStyle = shade;
      g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); g.fill();
    };

    const moon = (cx, cy, r, tint) => {
      const d = g.createRadialGradient(cx - r * 0.35, cy - r * 0.35, r * 0.15, cx, cy, r);
      d.addColorStop(0, '#ffffff');
      d.addColorStop(0.6, tint);
      d.addColorStop(1, 'rgba(20,14,30,.9)');
      g.fillStyle = d;
      g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); g.fill();
      g.fillStyle = 'rgba(60,40,70,.28)';
      for (let i = 0; i < 7; i++) {
        const a = Math.random() * 6.28, rr = Math.random() * r * 0.7;
        g.beginPath();
        g.arc(cx + Math.cos(a) * rr, cy + Math.sin(a) * rr, r * (0.06 + Math.random() * 0.12), 0, 6.28);
        g.fill();
      }
    };

    if (sky.style === 'gas-giant') {
      // stars, thicker toward the zenith
      for (let i = 0; i < 900; i++) {
        const y = Math.pow(Math.random(), 1.5) * horizon;
        const a = 0.25 + Math.random() * 0.75;
        g.fillStyle = `rgba(255,255,255,${a * (1 - y / horizon) * 0.9})`;
        g.fillRect(Math.random() * S, y, Math.random() < 0.15 ? 2 : 1, Math.random() < 0.15 ? 2 : 1);
      }
      gasGiant(S * 0.5, H * 0.5, H * 0.21);
      // faint aurora over the ice
      const au = g.createLinearGradient(0, horizon - 120, 0, horizon);
      au.addColorStop(0, 'rgba(120,220,255,0)');
      au.addColorStop(1, 'rgba(120,220,255,.16)');
      g.fillStyle = au;
      g.fillRect(0, horizon - 120, S, 120);
    } else {
      // twin moons over a violet sky
      moon(S * 0.34, H * 0.5, H * 0.095, '#e8e2ea');
      moon(S * 0.44, H * 0.57, H * 0.052, '#d8cfe0');
      for (let i = 0; i < 260; i++) {
        const y = Math.pow(Math.random(), 1.7) * horizon * 0.8;
        g.fillStyle = `rgba(255,235,255,${0.1 + Math.random() * 0.3})`;
        g.fillRect(Math.random() * S, y, 1, 1);
      }
      // a spire city on the horizon, far right
      g.fillStyle = sky.city || '#3a2246';
      const baseX = S * 0.7;
      for (let i = 0; i < 16; i++) {
        const w = 6 + Math.random() * 22;
        const h = 26 + Math.random() * 96;
        const x = baseX + i * 26 + Math.random() * 10;
        g.fillRect(x, horizon - h, w, h);
        if (Math.random() < 0.4) {                       // a slender tower
          g.fillRect(x + w * 0.35, horizon - h - 40 - Math.random() * 60, 3, 60);
        }
      }
      g.globalAlpha = 0.5;
      g.fillStyle = '#7fe3ff';
      for (let i = 0; i < 40; i++) {                      // lit windows
        g.fillRect(baseX + Math.random() * 430, horizon - Math.random() * 120, 2, 2);
      }
      g.globalAlpha = 1;
    }

    // haze where the sky meets the ground
    const haze = g.createLinearGradient(0, horizon - 90, 0, horizon + 20);
    haze.addColorStop(0, 'rgba(255,255,255,0)');
    haze.addColorStop(1, sky.low);
    g.fillStyle = haze;
    g.fillRect(0, horizon - 90, S, 110);

    const tex = new THREE.CanvasTexture(cv);
    tex.encoding = THREE.sRGBEncoding;
    return tex;
  }

  function buildSky(scene, spec, onReady) {
    const geo = new THREE.SphereGeometry(240, 40, 24);
    const mat = new THREE.MeshBasicMaterial({ side: THREE.BackSide, fog: false });
    const dome = new THREE.Mesh(geo, mat);
    dome.rotation.y = Math.PI * 0.25;
    scene.add(dome);

    const applyProcedural = () => {
      const t = proceduralSky(spec);
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

    /* Cover, in each world's own material: ice crystals that glow from
       within, or the weathered pillars of whatever stood on the shelf. */
    const rockMat = SF.materials.painted(spec.rockTint, { repeat: [2, 2] });
    const crystalMat = new THREE.MeshStandardMaterial({
      color: 0x7fc0dc, emissive: new THREE.Color(0x2f9fd0), emissiveIntensity: 0.45,
      metalness: 0.1, roughness: 0.12, transparent: true, opacity: 0.86
    });

    const rock = (x, z, r, h) => {
      let m;
      if (spec.cover === 'crystal') {
        m = new THREE.Mesh(new THREE.ConeGeometry(r * 0.8, h, 6), crystalMat);
        m.position.set(x, h * 0.5, z);
        m.rotation.set((Math.random() - 0.5) * 0.24, Math.random() * 6.28, (Math.random() - 0.5) * 0.24);
      } else if (spec.cover === 'ruin' && h > 5) {
        m = new THREE.Mesh(new THREE.BoxGeometry(r * 1.5, h, r * 1.5), rockMat);
        m.position.set(x, h * 0.5, z);
        m.rotation.y = Math.random() * 6.28;
      } else {
        m = new THREE.Mesh(new THREE.DodecahedronGeometry(r, 0), rockMat);
        m.position.set(x, h * 0.35, z);
        m.scale.set(1, h / r, 1);
        m.rotation.set(Math.random(), Math.random() * 6.28, Math.random());
      }
      m.castShadow = m.receiveShadow = true;
      group.add(m);
      colliders.push({ min: { x: x - r * 0.8, z: z - r * 0.8 },
                       max: { x: x + r * 0.8, z: z + r * 0.8 },
                       top: h, bottom: 0 });
      return m;
    };

    for (let i = 0; i < 44; i++) {
      const ang = Math.random() * Math.PI * 2;
      const dist = 9 + Math.random() * (R - 14);
      const r = 1.1 + Math.random() * 2.6;
      const x = Math.cos(ang) * dist, z = Math.sin(ang) * dist;
      rock(x, z, r, r * (1.2 + Math.random() * 2.2));
      if (spec.cover === 'crystal' && i % 6 === 0) {
        emitters.push({ x: x, y: 2.2, z: z, colour: 0x5ec8ff, intensity: 1.6, distance: 12 });
      }
    }
    // a ring of larger spires marking the edge of the playable ground
    for (let i = 0; i < 26; i++) {
      const ang = (i / 26) * Math.PI * 2 + Math.random() * 0.1;
      rock(Math.cos(ang) * (R + 3), Math.sin(ang) * (R + 3), 3.4 + Math.random() * 2, 8.5);
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
