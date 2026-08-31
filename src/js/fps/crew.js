/* ============================================================
   The people in the station.

   A hub is not a room, it is the people standing in it. Four of them
   have posts and a job you can walk up to and do; the rest are crew,
   walking their own routes across three decks, standing in pairs,
   leaning on the gallery rail watching the Earth go past.

   The four you can talk to:

     VOSS       QUARTERMASTER   gear, parts, the bench
     KAUR       FLIGHT OFFICER  the ark and the ground below it
     ODIL       MUSTER          flying with someone
     THE ROOK   APPRAISER       breaks salvage down into parts

   The rest are set dressing that moves, which is the difference
   between a lobby and somewhere people live.
   ============================================================ */
(function (SF) {
  'use strict';

  const PLATE = 0.0042;              // nameplate world scale per canvas pixel
  const TALK_RANGE = 3.4;

  const VENDORS = [
    { id: 'armoury',  name: 'VOSS',      title: 'QUARTERMASTER',
      line: 'Gear, parts, and the bench.',
      x: -37, y: 7, z: 2, face: 1.57, colour: '#ffb454', cls: 'bulwark' },
    { id: 'campaign', name: 'KAUR',      title: 'FLIGHT OFFICER',
      line: 'The ark, and the ground below it.',
      x: 0, y: 0, z: 33, face: Math.PI, colour: '#5eeaff', cls: 'oracle' },
    { id: 'coop',     name: 'ODIL',      title: 'MUSTER',
      line: 'Nobody should be going out there alone.',
      x: 37, y: 7, z: 2, face: -1.57, colour: '#7dff9b', cls: 'wraith' },
    { id: 'appraise', name: 'THE ROOK',  title: 'APPRAISER',
      line: 'Bring me what you are not using.',
      x: -8, y: 0, z: -16, face: 0.4, colour: '#b98cff', cls: 'oracle' },
    { id: 'contracts', name: 'SHAW',     title: 'CONTRACTS',
      line: 'Work, if you want it. Come back when it is done.',
      x: 9, y: 0, z: -16, face: -0.4, colour: '#ff5ea8', cls: 'bulwark' }
  ];

  /* Crew routes: a loop of waypoints each, walked at a stroll. Deliberately
     laid across the concourse and the galleries rather than around the edges,
     because people you have to walk around are what make a room feel used. */
  const ROUTES = [
    [[-14, 0, -20], [-14, 0, 16], [6, 0, 16], [6, 0, -20]],
    [[10, 0, 22], [-10, 0, 22], [-10, 0, -6], [10, 0, -6]],
    [[0, 0, -40], [0, 0, -30], [8, 0, -22], [-8, 0, -22], [0, 0, -30]],
    [[-18, 7, 24], [18, 7, 24], [18, 7, -24], [-18, 7, -24]],
    [[24, 7, 10], [24, 7, -10], [40, 7, -4], [40, 7, 8]],
    [[-24, 7, -10], [-24, 7, 10], [-40, 7, 8], [-40, 7, -4]],
    [[-18, 14, 22], [18, 14, 22], [18, 14, -22], [-18, 14, -22]],
    [[6, 0, 38], [6, 0, 28], [-6, 0, 28], [-6, 0, 38]]
  ];

  /* People who stand still: leaning on rails, talking in pairs. */
  const IDLERS = [
    [-15.5, 7, -8, 1.57], [-15.5, 7, -5, 1.4],       // two at the gallery rail
    [15.5, 7, 12, -1.57],
    [-15.5, 14, 6, 1.57], [15.5, 14, -6, -1.57],
    [26, 0, 2, -1.57], [29, 0, 5, -2.2],             // a pair in the quartermaster's bay
    [-3, 0, 30, 3.0], [3, 0, 31, 3.3],               // queueing at flight control
    [-33, 14, -18, 0.6]                              // one in the cupola, watching
  ];

  const CHATTER = [
    'Sixteen hours in the seat and they want a debrief.',
    'You were on the ice run? I heard it sang at you.',
    'Cutter three is down again. Third time this month.',
    'They say the ark changed course. They always say that.',
    'Nobody has slept since the relay came back up.',
    'I keep the shutter open. You get used to the drop.'
  ];

  function plate(lines, colour) {
    const cv = document.createElement('canvas');
    cv.width = 512; cv.height = 160;
    const g = cv.getContext('2d');
    g.textAlign = 'center';
    g.lineWidth = 8;
    g.strokeStyle = 'rgba(0,0,0,.85)';

    g.font = '600 54px "Chakra Petch", system-ui, sans-serif';
    g.textBaseline = 'middle';
    g.strokeText(lines[0], 256, 46);
    g.fillStyle = colour;
    g.fillText(lines[0], 256, 46);

    if (lines[1]) {
      g.font = '400 30px "Share Tech Mono", ui-monospace, monospace';
      g.lineWidth = 6;
      g.strokeText(lines[1], 256, 104);
      g.fillStyle = 'rgba(230,244,252,.85)';
      g.fillText(lines[1], 256, 104);
    }

    const tex = new THREE.CanvasTexture(cv);
    if (THREE.sRGBEncoding !== undefined) tex.encoding = THREE.sRGBEncoding;
    const sprite = new THREE.Sprite(new THREE.SpriteMaterial({
      map: tex, transparent: true, depthTest: false, depthWrite: false
    }));
    sprite.scale.set(512 * PLATE, 160 * PLATE, 1);
    return sprite;
  }

  function create(ctx) {
    const { scene, level, player, hud } = ctx;
    const people = [];
    const vendors = [];
    let prompting = false;
    let useT = 0;
    let sayT = 8;

    const look = () => ({
      skin: Math.floor(Math.random() * SF.gear.SKINS.length),
      hair: Math.floor(Math.random() * SF.gear.HAIR_STYLES.length),
      hairColour: Math.floor(Math.random() * SF.gear.HAIR_COLOURS.length)
    });
    const CLASSES = ['bulwark', 'oracle', 'wraith'];

    function spawn(cls, x, y, z, yaw, gun) {
      const built = SF.avatar.build({ cls: cls, look: look(), gun: gun !== false });
      built.group.position.set(x, y, z);
      built.group.rotation.y = yaw;
      scene.add(built.group);
      const p = { built, group: built.group, phase: Math.random() * 6.28, amount: 0,
                  yaw: yaw, bob: Math.random() * 6.28 };
      people.push(p);
      return p;
    }

    /* ---------- the four with posts ---------- */
    for (const v of VENDORS) {
      const p = spawn(v.cls, v.x, v.y, v.z, v.face, false);
      const tag = plate([v.name, v.title], v.colour);
      tag.position.set(0, 2.24, 0);
      p.group.add(tag);
      p.tag = tag;
      vendors.push(Object.assign({}, v, { person: p }));

      /* A lit post to stand behind, so a vendor reads as a station and not
         as somebody who wandered off. */
      const desk = new THREE.Mesh(new THREE.BoxGeometry(2.6, 1.05, 0.9),
        new THREE.MeshStandardMaterial({ color: 0x30363f, metalness: 0.75, roughness: 0.45 }));
      desk.position.set(v.x - Math.sin(v.face) * -1.3, v.y + 0.52, v.z - Math.cos(v.face) * -1.3);
      desk.rotation.y = v.face;
      desk.castShadow = desk.receiveShadow = true;
      scene.add(desk);
      const glow = new THREE.Mesh(new THREE.BoxGeometry(2.4, 0.05, 0.7),
        SF.materials.emissive(new THREE.Color(v.colour).getHex(), 2.0));
      glow.position.copy(desk.position);
      glow.position.y = v.y + 1.06;
      glow.rotation.y = v.face;
      scene.add(glow);
      p.desk = desk; p.glow = glow;

      /* The board itself, behind the contracts post — a wall of lit slates
         is what tells you across a room that there is work here. */
      if (v.id === 'contracts') {
        const back = new THREE.Mesh(new THREE.BoxGeometry(4.6, 3.0, 0.25),
          new THREE.MeshStandardMaterial({ color: 0x1b2028, metalness: 0.6, roughness: 0.6 }));
        back.position.set(v.x - Math.sin(v.face) * 1.5, v.y + 1.8, v.z - Math.cos(v.face) * 1.5);
        back.rotation.y = v.face;
        scene.add(back);
        p.board = back;
        p.slates = [];
        for (let r = 0; r < 2; r++) {
          for (let c = 0; c < 3; c++) {
            const slate = new THREE.Mesh(new THREE.BoxGeometry(1.24, 1.14, 0.08),
              SF.materials.emissive(0xff5ea8, 1.5));
            const ox = (c - 1) * 1.42, oy = 0.65 - r * 1.3;
            /* Along the board's face, and proud of it — sharing the slab's
               centre buries them inside it. */
            slate.position.set(back.position.x + Math.cos(v.face) * ox + Math.sin(v.face) * 0.18,
                               back.position.y + oy,
                               back.position.z - Math.sin(v.face) * ox + Math.cos(v.face) * 0.18);
            slate.rotation.y = v.face;
            scene.add(slate);
            p.slates.push(slate);
          }
        }
      }
    }

    /* ---------- everyone else ---------- */
    ROUTES.forEach((route, i) => {
      const p = spawn(CLASSES[i % 3], route[0][0], route[0][1], route[0][2], 0, Math.random() < 0.4);
      p.route = route;
      p.leg = 0;
      p.speed = 1.5 + Math.random() * 0.8;
      p.wait = 0;
    });
    IDLERS.forEach(([x, y, z, yaw], i) => {
      spawn(CLASSES[(i + 1) % 3], x, y, z, yaw, Math.random() < 0.3);
    });

    /* ---------- ships going past the windows ---------- */
    const ships = [];
    for (let i = 0; i < 3; i++) {
      const g = new THREE.Group();
      const hull = new THREE.Mesh(new THREE.ConeGeometry(1.4, 8, 6),
        new THREE.MeshStandardMaterial({ color: 0x8a94a2, metalness: 0.8, roughness: 0.35 }));
      hull.rotation.x = Math.PI / 2;
      const burn = new THREE.Mesh(new THREE.SphereGeometry(0.9, 8, 6),
        new THREE.MeshBasicMaterial({ color: 0x8fd9ff, transparent: true, opacity: 0.7, fog: false }));
      burn.position.z = 4.6;
      g.add(hull, burn);
      scene.add(g);
      ships.push({ g, t: i * 0.33, speed: 0.03 + Math.random() * 0.02,
                   lane: i, r: 120 + i * 40 });
    }

    /* ---------- update ---------- */
    const tmp = new THREE.Vector3();

    function update(dt, holding, camera) {
      /* walkers */
      for (const p of people) {
        if (p.route) {
          const target = p.route[(p.leg + 1) % p.route.length];
          const dx = target[0] - p.group.position.x, dz = target[2] - p.group.position.z;
          const d = Math.hypot(dx, dz);
          if (d < 0.6) {
            p.leg = (p.leg + 1) % p.route.length;
            p.wait = Math.random() < 0.3 ? 1.5 + Math.random() * 3 : 0;
          }
          if (p.wait > 0) {
            p.wait -= dt;
            p.amount += (0 - p.amount) * Math.min(1, 6 * dt);
          } else {
            const want = Math.atan2(dx, dz) + Math.PI;   // the model faces -Z
            let diff = ((want - p.yaw + Math.PI * 3) % (Math.PI * 2)) - Math.PI;
            p.yaw += diff * Math.min(1, 5 * dt);
            p.group.rotation.y = p.yaw;
            p.group.position.x += (dx / d) * p.speed * dt;
            p.group.position.z += (dz / d) * p.speed * dt;
            p.amount += (1 - p.amount) * Math.min(1, 6 * dt);
          }
          p.phase += dt * (3.2 + p.speed);
          SF.avatar.stride(p.built.limbs, p.phase, p.amount * 0.55);
        } else {
          // standing: a small weight shift, so nobody is a statue
          p.bob += dt * 1.1;
          p.group.position.y = (p.group.userData.baseY == null
            ? (p.group.userData.baseY = p.group.position.y)
            : p.group.userData.baseY) + Math.sin(p.bob) * 0.012;
          SF.avatar.stride(p.built.limbs, p.bob * 0.4, 0.05);
        }
        if (p.tag && camera) p.tag.lookAt(camera.position);
      }

      /* ships on a slow circuit outside */
      for (const s of ships) {
        s.t += s.speed * dt;
        const a = s.t * Math.PI * 2;
        s.g.position.set(Math.cos(a) * s.r, 6 + s.lane * 14, Math.sin(a) * s.r);
        s.g.rotation.y = -a + Math.PI / 2;
      }

      /* overheard crew, once in a while, when you are near one of them */
      sayT -= dt;
      if (sayT <= 0) {
        sayT = 22 + Math.random() * 26;
        const near = people.filter((p) => p.route &&
          Math.hypot(p.group.position.x - player.position.x,
                     p.group.position.z - player.position.z) < 12 &&
          Math.abs(p.group.position.y - player.position.y) < 3);
        if (near.length) hud.say('CREW', CHATTER[Math.floor(Math.random() * CHATTER.length)], 3600);
      }

      /* the contracts board runs hot when you have work finished */
      const contracts = vendors.find((c) => c.id === 'contracts');
      if (contracts && contracts.person.slates) {
        const ready = SF.bounties.readyCount(ctx.character);
        const t = performance.now() / 340;
        contracts.person.slates.forEach((sl, i) => {
          sl.material.emissiveIntensity = ready
            ? 2.2 + Math.sin(t + i * 0.7) * 1.5
            : 0.9 + Math.sin(t * 0.3 + i) * 0.25;
        });
      }

      /* the ones you can talk to */
      let v = null, best = TALK_RANGE;
      for (const cand of vendors) {
        const d = Math.hypot(player.position.x - cand.x, player.position.z - cand.z);
        if (d < best && Math.abs(player.position.y - cand.y) < 2.4) { best = d; v = cand; }
      }
      if (!v) {
        if (prompting) { hud.pickup(''); prompting = false; }
        useT = 0;
        return false;
      }
      prompting = true;
      if (holding) {
        useT += dt;
        hud.pickup(v.name + ' — ' + Math.max(0, 0.5 - useT).toFixed(1) + 's');
        if (useT >= 0.5) {
          useT = 0;
          hud.pickup('');
          SF.audio.sfx.confirm();
          if (ctx.onVendor) ctx.onVendor(v);
        }
      } else {
        useT = 0;
        hud.pickup('HOLD F — ' + v.name + ', ' + v.title + ' · ' + v.line);
      }
      return true;
    }

    function destroy() {
      hud.pickup('');
      for (const p of people) {
        scene.remove(p.group);
        if (p.desk) scene.remove(p.desk);
        if (p.glow) scene.remove(p.glow);
        if (p.board) scene.remove(p.board);
        if (p.slates) for (const sl of p.slates) scene.remove(sl);
      }
      for (const s of ships) scene.remove(s.g);
      people.length = 0;
    }

    return { update, destroy, vendors, get count() { return people.length; } };
  }

  SF.crew = { create, VENDORS };
})(window.SF);
