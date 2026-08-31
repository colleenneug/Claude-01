/* ============================================================
   Orbital destinations.

   Two worlds the cutter can reach once the ark's habitat ring is
   below you. Both are Pale holdings — places the light reached
   before the ark did, and never left. They are open arenas rather than corridors: a horizon,
   scattered cover, and hostiles in escalating waves until you either
   call extraction or spend the last return the Deep will give you.

   Each has a backdrop image if one is present in assets/, and a
   procedurally generated sky if not, so the destinations work with or
   without art.
   ============================================================ */
(function (SF) {
  'use strict';

  const UNLOCK_AFTER = 6;            // clear mission 6 to open the sky

  /* Zone radius in metres. The zone covers ZONE_R^2 * pi of ground — at 620
     that is a hundred times the area of the original arena, about 1.24 km
     across. Everything below scales off this one number: cover density,
     event sites, ammunition, view distance and the sky. */
  const ZONE_R = 620;

  const DESTINATIONS = [
    {
      id: 'desert',
      name: "THRESHER'S REACH",
      sub: 'DESERT CHAPTERHOUSE · CONSECRATED YEAR 4',
      objective: 'Raid the dust shelf. Leave when you want to.',
      brief: 'A survey station the ark blessed on its way through, two years before it did the ' +
             'same to itself. The Order down there has been keeping the lamps lit for nobody ever ' +
             'since, and it has had a long time to get used to the heat.',
      image: 'assets/desert-planet-bg.webp',
      /* Violet sky, twin moons, a far-off spire city — matching the supplied
         backdrop closely enough that the generated fallback is not jarring. */
      sky: { top: '#2a1038', mid: '#7b3f7a', low: '#e0975c', sun: '#ffd9a0', haze: 0xb066a0,
             style: 'twin-moons', city: '#3a2246', ground: '#c2703a' },
      cover: 'ruin',
      ground: '#b06a38', rockTint: '#8a5a3c', fog: 0xa9663f, fogDensity: 0.010,
      light: { key: 0xffd8a0, keyI: 2.2, hemiSky: 0xffb877, hemiGround: 0x4a2f18, hemiI: 1.1 },
      faction: {
        name: 'THE PALE ORDER',
        mix: [['scarab', 0.44], ['marauder', 0.42], ['colossus', 0.14]],
        elite: 'colossus',
        population: 16
      }
    },
    {
      id: 'frozen',
      name: 'COLD LANTERN',
      sub: 'FROZEN RELAY · STILL BURNING YEAR 6',
      objective: 'Raid the ice field. Leave when you want to.',
      brief: 'The relay that carried the ark\'s last clean message home, six years into the ' +
             'crossing. The Lamplit are still down there feeding it, into a sky that has not ' +
             'changed in forty years. They will not stop for you either.',
      image: 'assets/frozen-planet-bg.webp',
      /* Night sky and stars with a banded gas giant, over a pale ice field. */
      sky: { top: '#050a18', mid: '#132743', low: '#8fb4cc', sun: '#dfeeff', haze: 0x6fa8d0,
             style: 'gas-giant', ground: '#c6dbe8' },
      cover: 'crystal',
      /* Snow is a bright albedo under a strong key: dial both back or the
         whole field blows out to white and takes the HUD with it. */
      ground: '#93aec2', rockTint: '#7fb4d4', fog: 0x86b0cc, fogDensity: 0.012,
      light: { key: 0xdcefff, keyI: 1.05, hemiSky: 0xbfe0ff, hemiGround: 0x2f3d4a, hemiI: 0.7 },
      faction: {
        name: 'THE LAMPLIT',
        mix: [['mote', 0.4], ['revenant', 0.46], ['hoarfrost', 0.14]],
        elite: 'hoarfrost',
        population: 16
      }
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
        [0.5, 'CHOIRMASTER', 'The cutter holds at altitude. Stay as long as you like — nothing here is on a clock.'],
        [7.0, 'CANDLE', 'They kept the lamps lit for a ship that was never coming. Do not expect them to be reasonable.'],
        [16.0, 'CHOIRMASTER', 'We will flag anything worth your time as it comes up.']
      ],
      patrol: true, planet: dest
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
    const geo = new THREE.SphereGeometry(2600, 48, 28);
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
    const R = ZONE_R;

    scene.background = new THREE.Color(spec.sky.mid);
    /* Thin the fog right out: at this scale the old density was a wall two
       hundred metres away. */
    scene.fog = new THREE.FogExp2(spec.fog, spec.fogDensity * 0.12);
    buildSky(scene, spec);

    /* Ground. One large slab, with the texture repeated hard so it does not
       smear across a kilometre. */
    const groundMat = SF.materials.painted(spec.ground, { repeat: [110, 110] });
    const ground = new THREE.Mesh(new THREE.BoxGeometry(R * 2.3, 1, R * 2.3), groundMat);
    ground.position.y = -0.5;
    ground.receiveShadow = true;
    group.add(ground);

    /* ---------- cover ----------
       Thousands of rocks at this scale, so they are drawn as instanced
       meshes: one draw call each rather than one per rock. */
    const rockMat = SF.materials.painted(spec.rockTint, { repeat: [2, 2] });
    const crystalMat = new THREE.MeshStandardMaterial({
      color: 0x7fc0dc, emissive: new THREE.Color(0x2f9fd0), emissiveIntensity: 0.45,
      metalness: 0.1, roughness: 0.12, transparent: true, opacity: 0.86
    });
    const isCrystal = spec.cover === 'crystal';
    const coverMat = isCrystal ? crystalMat : rockMat;
    const coverGeo = isCrystal ? new THREE.ConeGeometry(1, 1, 6)
                               : new THREE.DodecahedronGeometry(1, 0);
    // unit geometries throughout, so one scaling rule grounds all of them
    const bigGeo = isCrystal ? new THREE.ConeGeometry(1, 1, 7)
                             : new THREE.BoxGeometry(1, 1, 1);

    /* Density per square metre, so the world feels the same however big it is. */
    const AREA = Math.PI * R * R;
    const SMALL = Math.min(5200, Math.round(AREA / 900));
    const LARGE = Math.min(1400, Math.round(AREA / 3400));

    const dummy = new THREE.Object3D();

    function place(obj, geo, x, z, r, h) {
      if (isCrystal) {                       // cone: unit height, centre pivot
        obj.position.set(x, h * 0.5, z);
        obj.scale.set(r, h, r);
        obj.rotation.set((Math.random() - 0.5) * 0.24, Math.random() * 6.28,
                         (Math.random() - 0.5) * 0.24);
      } else if (geo.type === 'BoxGeometry') {
        obj.position.set(x, h * 0.46, z);    // a touch into the ground
        obj.scale.set(r * 1.6, h, r * 1.6);
        obj.rotation.set(0, Math.random() * 6.28, 0);
      } else {                               // dodecahedron: spans two units
        obj.position.set(x, h * 0.3, z);
        obj.scale.set(r, h * 0.5, r);
        obj.rotation.set(Math.random(), Math.random() * 6.28, Math.random());
      }
      obj.updateMatrix();
    }

    function scatter(geo, mat, count, sizeFn) {
      const mesh = new THREE.InstancedMesh(geo, mat, count);
      mesh.castShadow = mesh.receiveShadow = true;
      mesh.frustumCulled = true;
      for (let i = 0; i < count; i++) {
        const ang = Math.random() * Math.PI * 2;
        // sqrt keeps the scatter even rather than clumped at the middle
        const dist = 14 + Math.sqrt(Math.random()) * (R - 20);
        const x = Math.cos(ang) * dist, z = Math.sin(ang) * dist;
        const { r, h } = sizeFn();

        /* Cones and boxes are one unit tall and pivot at their centre; the
           dodecahedron spans two units. Ground each accordingly, and sink the
           rocks slightly so they read as outcrops rather than dropped props. */
        place(dummy, geo, x, z, r, h);
        mesh.setMatrixAt(i, dummy.matrix);

        colliders.push({ min: { x: x - r * 0.8, z: z - r * 0.8 },
                         max: { x: x + r * 0.8, z: z + r * 0.8 },
                         top: h, bottom: 0 });
      }
      mesh.instanceMatrix.needsUpdate = true;
      group.add(mesh);
      return mesh;
    }

    scatter(coverGeo, coverMat, SMALL, () => {
      const r = 1.1 + Math.random() * 2.6;
      return { r: r, h: r * (1.2 + Math.random() * 2.2) };
    });
    scatter(bigGeo, coverMat, LARGE, () => {
      const r = 3.4 + Math.random() * 3.4;
      return { r: r, h: r * (2.2 + Math.random() * 2.6) };
    });

    /* A ring of tall spires marking the edge of the world. */
    const edge = new THREE.InstancedMesh(bigGeo, coverMat, 220);
    for (let i = 0; i < 220; i++) {
      const ang = (i / 220) * Math.PI * 2;
      const x = Math.cos(ang) * (R + 8), z = Math.sin(ang) * (R + 8);
      const r = 7 + Math.random() * 5, h = 34 + Math.random() * 26;
      place(dummy, bigGeo, x, z, r, h);
      edge.setMatrixAt(i, dummy.matrix);
      colliders.push({ min: { x: x - r, z: z - r }, max: { x: x + r, z: z + r },
                       top: h, bottom: 0 });
    }
    edge.instanceMatrix.needsUpdate = true;
    edge.castShadow = true;
    group.add(edge);

    /* ---------- the landing pad ---------- */
    const pad = new THREE.Mesh(new THREE.CylinderGeometry(4.5, 5, 0.4, 28),
      SF.materials.get('deck'));
    pad.position.set(0, 0.2, 0);
    pad.receiveShadow = true;
    group.add(pad);
    const beacon = new THREE.Mesh(new THREE.CylinderGeometry(0.4, 0.4, 60, 10),
      SF.materials.emissive(0x7dff9b, 2.4));
    beacon.position.set(0, 30, 0);
    group.add(beacon);
    emitters.push({ x: 0, y: 4, z: 0, colour: 0x7dff9b, intensity: 3.0, distance: 34 });

    /* ---------- event sites ---------- */
    const eventAnchors = [];
    const RINGS = [0.3, 0.55, 0.8];
    for (const ring of RINGS) {
      const n = Math.round(6 * ring * 2);
      for (let i = 0; i < n; i++) {
        const ang = (i / n) * Math.PI * 2 + ring * 2.1;
        eventAnchors.push({ x: Math.cos(ang) * R * ring, z: Math.sin(ang) * R * ring });
      }
    }
    for (const a of eventAnchors) {
      emitters.push({ x: a.x, y: 6, z: a.z, colour: spec.sky.haze, intensity: 2.0, distance: 40 });
    }

    scene.add(group);

    return {
      group, colliders, emitters, props: [], nodeAnchors: [], eventAnchors,
      space: SF.spatial.create(colliders, 24),
      zones: { arena: { x0: -R, x1: R, z0: -R, z1: R, cx: 0, cz: 0, name: spec.name } },
      playerStart: new THREE.Vector3(0, 0, 8),
      bounds: { minX: -R, maxX: R, minZ: -R, maxZ: R },
      extraction: new THREE.Vector3(0, 0, 0),
      radius: R,
      beacon: beacon
    };
  }

  SF.planets = { DESTINATIONS, byId, unlocked, asMission, buildArena, buildSky,
                 UNLOCK_AFTER, ZONE_R, resolveAsset };
})(window.SF);
