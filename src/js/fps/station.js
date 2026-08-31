/* ============================================================
   THE CRADLE — the station you come back to.

   Not a mission. Nothing here shoots at you. It is the place the
   Division kept flying: the last assembled station in orbit, modules
   bolted end to end across forty years and still holding pressure,
   with a hundred people living in it and the ark going past overhead
   every ninety-four minutes.

   Three decks around an open concourse, joined by four stair flights
   and two lifts. The module names are the ones on the hull, which is
   to say the real ones — UNITY, DESTINY, HARMONY, TRANQUILITY, and
   the CUPOLA, which is still the best window anyone has ever built.

   Layout, in metres. The spine runs along Z, the decks stack in Y:

     DECK A  y  0   arrivals, concourse floor, the airlock out
     DECK B  y  7   gallery ring, the two side labs
     DECK C  y 14   upper ring and the cupola

   The concourse is open through all three, so from the floor you can
   see the cupola, and from the cupola you can see the floor.
   ============================================================ */
(function (SF) {
  'use strict';

  const DECK = { a: 0, b: 7, c: 14 };
  const CEIL = 22;                       // the top of the concourse volume
  const RISE = 0.29;                     // one stair step — under the player's step height
  const LIFT_R = 2.6;                    // the car
  const LIFT_AT = [[-25, -25], [25, 25]];
  /* The shaft is barely wider than the car: the gap you step over on arrival
     has to be narrower than the player's own radius or it is a hole. */
  const SHAFT = LIFT_R + 0.15;
  const shaftWells = () => LIFT_AT.map(([x, z]) =>
    ({ x0: x - SHAFT, x1: x + SHAFT, z0: z - SHAFT, z1: z + SHAFT }));

  const MODULES = [
    { id: 'unity',      name: 'UNITY',       deck: 'a', sub: 'ARRIVALS' },
    { id: 'destiny',    name: 'DESTINY',     deck: 'a', sub: 'CONCOURSE' },
    { id: 'quest',      name: 'QUEST',       deck: 'a', sub: 'AIRLOCK' },
    { id: 'zvezda',     name: 'ZVEZDA',      deck: 'a', sub: 'QUARTERMASTER' },
    { id: 'harmony',    name: 'HARMONY',     deck: 'b', sub: 'GALLERY' },
    { id: 'columbus',   name: 'COLUMBUS',    deck: 'b', sub: 'ARMOURY' },
    { id: 'kibo',       name: 'KIBO',        deck: 'b', sub: 'FIRETEAM' },
    { id: 'tranquility', name: 'TRANQUILITY', deck: 'c', sub: 'UPPER RING' },
    { id: 'cupola',     name: 'CUPOLA',      deck: 'c', sub: 'OBSERVATION' }
  ];

  function build(scene) {
    const group = new THREE.Group();
    scene.add(group);
    const colliders = [];
    const emitters = [];
    const M = SF.materials;

    /* ---------- helpers ---------- */
    /* Everything is axis-aligned boxes given as extents rather than centres:
       a station is a set of rooms, and rooms are easier to write as the space
       they occupy than as a centre plus a size. */
    function box(x0, x1, y0, y1, z0, z1, mat, solid) {
      const w = x1 - x0, h = y1 - y0, d = z1 - z0;
      // one texture tile per two metres, whatever the surface's size
      const mesh = new THREE.Mesh(M.tiledBox(w, h, d, 2.2), mat);
      mesh.position.set((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2);
      mesh.castShadow = solid !== false;
      mesh.receiveShadow = true;
      group.add(mesh);
      if (solid !== false) {
        colliders.push({ min: { x: x0, z: z0 }, max: { x: x1, z: z1 }, top: y1, bottom: y0 });
      }
      return mesh;
    }

    /* A deck plate with rectangular holes cut in it: the atrium, and a well
       over each stair flight. Cutting the wells matters more than it sounds
       — a flight that runs under an unbroken deck stops you dead when your
       head reaches the slab, about two thirds of the way up.

       Rather than special-casing shapes, cut the plate on every hole edge and
       emit the cells that no hole covers. */
    function plateWith(x0, x1, z0, z1, y, holes, mat) {
      const T = 0.4;
      const xs = [x0, x1], zs = [z0, z1];
      for (const h of holes) {
        for (const v of [h.x0, h.x1]) if (v > x0 && v < x1) xs.push(v);
        for (const v of [h.z0, h.z1]) if (v > z0 && v < z1) zs.push(v);
      }
      xs.sort((a, b) => a - b);
      zs.sort((a, b) => a - b);
      for (let i = 0; i < xs.length - 1; i++) {
        for (let j = 0; j < zs.length - 1; j++) {
          const ax = xs[i], bx = xs[i + 1], az = zs[j], bz = zs[j + 1];
          if (bx - ax < 0.01 || bz - az < 0.01) continue;
          const cx = (ax + bx) / 2, cz = (az + bz) / 2;
          if (holes.some((h) => cx > h.x0 && cx < h.x1 && cz > h.z0 && cz < h.z1)) continue;
          box(ax, bx, y - T, y, az, bz, mat);
        }
      }
    }

    function plate(x0, x1, z0, z1, y, mat) {
      box(x0, x1, y - 0.4, y, z0, z1, mat);
    }

    /* A railing along one edge — solid, because the whole point of a gallery
       is that you can lean on the edge and not fall off it. */
    function rail(x0, x1, z0, z1, y) {
      const post = M.get('pipe');
      box(x0, x1, y, y + 1.05, z0, z1, post);
    }

    /* A flight of stairs as a run of steps. Each rise is under the player's
       step height, so walking into it climbs it — no ramp special-casing. */
    function stairs(x0, x1, zFrom, zTo, yFrom, yTo, mat) {
      const n = Math.round((yTo - yFrom) / RISE);
      const dz = (zTo - zFrom) / n;
      for (let i = 0; i < n; i++) {
        const y = yFrom + RISE * (i + 1);
        const za = zFrom + dz * i, zb = zFrom + dz * (i + 1);
        box(x0, x1, y - RISE - 0.25, y, Math.min(za, zb), Math.max(za, zb), mat);
      }
      return { x0, x1, zFrom, zTo, yFrom, yTo };
    }

    const light = (x, y, z, colour, intensity, distance) =>
      emitters.push({ x, y, z, colour, intensity, distance });

    const hull = M.get('stnHull');
    const dark = M.get('stnDark');
    const deckMat = M.get('stnDeck');
    const grate = M.get('stnGrate');
    const glass = M.get('glass');
    const pipe = M.get('pipe');

    /* ---------- deck A ---------- */
    // concourse floor, arrivals, airlock, quartermaster bay
    plate(-20, 20, -26, 26, DECK.a, deckMat);
    plate(-11, 11, -54, -26, DECK.a, grate);            // UNITY, arrivals
    plate(-9, 9, 26, 46, DECK.a, grate);                // QUEST, the airlock
    plate(20, 42, -10, 14, DECK.a, grate);              // ZVEZDA side bay

    /* ---------- deck B: a gallery ring around the open middle ---------- */
    /* The atrium, plus a well over each of the two flights coming up from A. */
    const WELL_B = [
      { x0: -13, x1: 13, z0: -19, z1: 19 },
      { x0: -23, x1: -15, z0: -16, z1: 8 },
      { x0: 15, x1: 23, z0: -8, z1: 16 }
    ].concat(shaftWells());
    plateWith(-28, 28, -34, 34, DECK.b, WELL_B, deckMat);
    plate(-46, -28, -8, 12, DECK.b, grate);             // COLUMBUS arm
    plate(28, 46, -8, 12, DECK.b, grate);               // KIBO arm

    /* ---------- deck C: a smaller ring, and the cupola ---------- */
    const WELL_C = [
      { x0: -13, x1: 13, z0: -19, z1: 19 },
      { x0: -22, x1: -15, z0: 4, z1: 28 },
      { x0: 15, x1: 22, z0: -28, z1: -4 }
    ].concat(shaftWells());
    plateWith(-24, 24, -30, 30, DECK.c, WELL_C, deckMat);
    /* The cupola hangs off the port side of deck C rather than off the bow,
       because a cupola over another module is a window onto that module's
       roof. Out here there is nothing below it at any deck — which is the
       entire specification for this room. */
    plate(-30, -24, -20, -14, DECK.c, grate);           // the neck you walk in along
    // and then the floor becomes the window: the cupola looks *down*
    box(-43.4, -30, DECK.c - 0.3, DECK.c, -25.4, -12.6, M.get('stnPane'));
    // mullions, so it reads as a built window and not a hole
    /* Matte and dark: a window frame lit from behind by a lamp and in front
       by an environment map turns into two white bars across the view. */
    const mullion = new THREE.MeshStandardMaterial({ color: 0x171c23, metalness: 0.15,
                                                     roughness: 0.9, envMapIntensity: 0.15 });
    for (const x of [-39.9, -36.7, -33.5])
      box(x - 0.12, x + 0.12, DECK.c - 0.32, DECK.c + 0.06, -25.4, -12.6, mullion, false);
    for (const z of [-22.2, -19, -15.8])
      box(-43.4, -30, DECK.c - 0.32, DECK.c + 0.06, z - 0.12, z + 0.12, mullion, false);
    // a rim you can hold on to at the edge of it
    box(-10, 10, DECK.c, DECK.c + 1.0, -32.4, -32, pipe);

    /* ---------- the hull around it all ---------- */
    const WALL = 1.2;
    // outer shell of the concourse volume
    box(-30 - WALL, -30, DECK.a - 1, CEIL, -36, 36, hull);
    box(30, 30 + WALL, DECK.a - 1, CEIL, -36, 36, hull);
    box(-30, 30, DECK.a - 1, CEIL, 36, 36 + WALL, hull);
    box(-30, 30, CEIL, CEIL + 0.6, -36, 36, dark, false);   // the roof, not a collider

    // arrivals tube walls
    box(-12, -11, DECK.a - 1, 7, -54, -26, hull);
    box(11, 12, DECK.a - 1, 7, -54, -26, hull);
    box(-12, 12, DECK.a - 1, 7, -55, -54, hull);
    box(-12, 12, 7, 7.6, -54, -26, dark, false);

    // airlock tube
    box(-10, -9, DECK.a - 1, 6.5, 26, 46, hull);
    box(9, 10, DECK.a - 1, 6.5, 26, 46, hull);
    box(-10, 10, DECK.a - 1, 6.5, 46, 47, hull);
    box(-10, 10, 6.5, 7, 26, 46, dark, false);

    // side bays and lab arms get walls too, so you cannot walk off into space
    for (const bay of [
      { x0: 20, x1: 42, z0: -10, z1: 14, y: DECK.a, h: 6.5 },
      { x0: -46, x1: -28, z0: -8, z1: 12, y: DECK.b, h: 6.0 },
      { x0: 28, x1: 46, z0: -8, z1: 12, y: DECK.b, h: 6.0 }
    ]) {
      box(bay.x0, bay.x1, bay.y - 1, bay.y + bay.h, bay.z0 - 1, bay.z0, hull);
      box(bay.x0, bay.x1, bay.y - 1, bay.y + bay.h, bay.z1, bay.z1 + 1, hull);
      const far = bay.x0 > 0 ? bay.x1 : bay.x0;
      box(far - (bay.x0 > 0 ? 0 : 1), far + (bay.x0 > 0 ? 1 : 0),
          bay.y - 1, bay.y + bay.h, bay.z0, bay.z1, hull);
      box(bay.x0, bay.x1, bay.y + bay.h, bay.y + bay.h + 0.5, bay.z0, bay.z1, dark, false);
    }

    // the cupola: a walled neck out from the ring, then the bay itself
    box(-30, -24, DECK.c - 1, DECK.c + 4.5, -21, -20, hull);
    box(-30, -24, DECK.c - 1, DECK.c + 4.5, -14, -13, hull);
    box(-30, -24, DECK.c + 4.5, DECK.c + 5, -21, -13, dark, false);
    box(-45, -43.4, DECK.c - 1, DECK.c + 5, -26.4, -11.6, hull);
    box(-45, -29, DECK.c - 1, DECK.c + 5, -26.4, -25.4, hull);
    box(-45, -29, DECK.c - 1, DECK.c + 5, -12.6, -11.6, hull);
    box(-30, -29, DECK.c - 1, DECK.c + 5, -26.4, -21, hull);
    box(-30, -29, DECK.c - 1, DECK.c + 5, -13, -11.6, hull);
    // a rail at the lip where the grating stops and the glass starts
    box(-30.2, -29.8, DECK.c, DECK.c + 1.0, -25.4, -12.6, mullion);

    /* ---------- railings around every drop ---------- */
    // deck B gallery, inner edge
    rail(-13, 13, -19.2, -19, DECK.b);
    rail(-13, 13, 19, 19.2, DECK.b);
    rail(-13.2, -13, -19, 19, DECK.b);
    rail(13, 13.2, -19, 19, DECK.b);
    // deck C ring, inner edge
    rail(-13, 13, -19.2, -19, DECK.c);
    rail(-13, 13, 19, 19.2, DECK.c);
    rail(-13.2, -13, -19, 19, DECK.c);
    rail(13, 13.2, -19, 19, DECK.c);
    // the stair wells, on the two long sides — the ends are where you walk in
    rail(-23.2, -23, -16, 8, DECK.b);
    rail(-15, -14.8, -16, 8, DECK.b);
    rail(14.8, 15, -8, 16, DECK.b);
    rail(23, 23.2, -8, 16, DECK.b);
    rail(-22.2, -22, 4, 28, DECK.c);
    rail(-15, -14.8, 4, 28, DECK.c);
    rail(14.8, 15, -28, -4, DECK.c);
    rail(22, 22.2, -28, -4, DECK.c);

    // deck C outer edge, where it overhangs deck B — broken where a shaft
    // comes up through it
    rail(-24, 24, 29.8, 30, DECK.c);
    rail(-24, 24, -30, -29.8, DECK.c);
    rail(-24, -23.8, -30, -25 - SHAFT, DECK.c);
    rail(-24, -23.8, -25 + SHAFT, 30, DECK.c);
    rail(23.8, 24, -30, 25 - SHAFT, DECK.c);
    rail(23.8, 24, 25 + SHAFT, 30, DECK.c);

    /* ---------- stairs ---------- */
    /* Two flights up each side of the concourse, offset front to back so the
       climb reads as a route through the room rather than a ladder. */
    const flights = [
      stairs(-22, -16, -14, 8, DECK.a, DECK.b, deckMat),
      stairs(16, 22, 14, -8, DECK.a, DECK.b, deckMat),
      stairs(-21, -16, 26, 4, DECK.b, DECK.c, deckMat),
      stairs(16, 21, -26, -4, DECK.b, DECK.c, deckMat)
    ];
    // landings, so the top of a flight is a floor and not a lip
    box(-22, -16, DECK.b - 0.4, DECK.b, 8, 12, deckMat);
    box(16, 22, DECK.b - 0.4, DECK.b, -12, -8, deckMat);
    box(-21, -16, DECK.c - 0.4, DECK.c, 0, 4, deckMat);
    box(16, 21, DECK.c - 0.4, DECK.c, -4, 0, deckMat);

    /* ---------- lifts ---------- */
    /* Two of them, at opposite corners, each serving all three decks. The car
       is a real floor you stand on; holding the call key sends it to the next
       deck up and wraps back to A from the top. The shaft is drawn as a glass
       tube so you can watch the other one move. */
    const lifts = [];
    for (const [lx, lz] of LIFT_AT) {
      const R = LIFT_R;
      const tube = new THREE.Mesh(
        new THREE.CylinderGeometry(R + 0.3, R + 0.3, CEIL - DECK.a + 1, 20, 1, true), glass);
      tube.position.set(lx, (CEIL + DECK.a) / 2, lz);
      group.add(tube);

      const car = new THREE.Group();
      const floor = new THREE.Mesh(new THREE.CylinderGeometry(R, R, 0.3, 20), M.get('hazard'));
      floor.receiveShadow = true;
      const ring = new THREE.Mesh(new THREE.TorusGeometry(R, 0.08, 6, 24),
        M.emissive(0x5eeaff, 2.4));
      ring.rotation.x = Math.PI / 2;
      ring.position.y = 1.1;
      const post = new THREE.Mesh(new THREE.CylinderGeometry(0.1, 0.1, 2.4, 8), pipe);
      post.position.set(0, 1.2, -R + 0.3);
      car.add(floor, ring, post);
      car.position.set(lx, DECK.a, lz);
      group.add(car);

      /* The car's own collider is mutated as it moves — the spatial hash is
         built once, so a lift that changed cells would fall out of it. Both
         lifts sit still in X and Z, so its bucket never changes. */
      const col = { min: { x: lx - R, z: lz - R }, max: { x: lx + R, z: lz + R },
                    top: DECK.a, bottom: DECK.a - 0.3 };
      colliders.push(col);

      lifts.push({ x: lx, z: lz, r: R, car, col, ring,
                   y: DECK.a, target: DECK.a, deck: 0, moving: false, hold: 0 });
      light(lx, DECK.a + 2.4, lz, 0x5eeaff, 1.1, 9);
    }

    /* ---------- terminals ---------- */
    /* The three things you actually came here to do, each a lit kiosk you
       stand at. They are placed apart on purpose: a hub you cross is a hub. */
    const terminals = [
      { id: 'armoury',  name: 'ARMOURY',        line: 'Salvage, parts and the bench.',
        x: -37, y: DECK.b, z: 2,  colour: 0xffb454 },
      { id: 'campaign', name: 'FLIGHT DECK',    line: 'The ark, and the ground below it.',
        x: 0,   y: DECK.a, z: 34, colour: 0x5eeaff },
      { id: 'coop',     name: 'FIRETEAM',       line: 'Fly with someone.',
        x: 37,  y: DECK.b, z: 2,  colour: 0x7dff9b }
    ];
    for (const t of terminals) {
      const body = box(t.x - 1.1, t.x + 1.1, t.y, t.y + 1.0, t.z - 0.7, t.z + 0.7, dark);
      void body;
      const screen = new THREE.Mesh(new THREE.BoxGeometry(1.9, 1.2, 0.12),
        M.emissive(t.colour, 1.6));
      screen.position.set(t.x, t.y + 1.7, t.z);
      screen.rotation.x = -0.32;
      group.add(screen);
      const stem = new THREE.Mesh(new THREE.CylinderGeometry(0.09, 0.09, 0.8, 8), pipe);
      stem.position.set(t.x, t.y + 1.3, t.z);
      group.add(stem);
      light(t.x, t.y + 2.0, t.z, t.colour, 1.5, 10);
      t.screen = screen;
    }

    /* ---------- the view ---------- */
    // windows in the concourse walls, and the cupola's own dome
    for (const z of [-24, -8, 8, 24]) {
      for (const side of [-1, 1]) {
        const w = new THREE.Mesh(new THREE.BoxGeometry(0.2, 4.2, 7), glass);
        w.position.set(side * 30, DECK.b + 2.4, z);
        group.add(w);
      }
    }
    /* The dome caps the bay from above, so the room is glass top and bottom
       and metal only at the edges. */
    const dome = new THREE.Mesh(new THREE.SphereGeometry(8.2, 28, 18, 0, Math.PI * 2, 0, 1.15),
      M.get('stnPane'));
    dome.position.set(-36.7, DECK.c + 0.4, -19);
    group.add(dome);

    /* ---------- dressing ---------- */
    // crates and pipe runs so the volumes read as lived in
    const crate = M.get('stnPaint');
    const spots = [[-6, DECK.a, -34], [5, DECK.a, -38], [-8, DECK.a, 30], [26, DECK.a, 4],
                   [30, DECK.a, -4], [-34, DECK.b, 8], [-40, DECK.b, -2], [34, DECK.b, 8],
                   [16, DECK.a, 18], [-17, DECK.a, -20], [20, DECK.c, 20], [-19, DECK.c, -22]];
    for (const [x, y, z] of spots) {
      const h = 0.7 + Math.random() * 0.8;
      box(x - 0.7, x + 0.7, y, y + h, z - 0.7, z + 0.7, crate);
    }
    for (const [x, y, z, len] of [[-29, DECK.a + 5.4, 0, 60], [29, DECK.a + 5.4, 0, 60],
                                  [-29, DECK.b + 5.2, 0, 50], [29, DECK.b + 5.2, 0, 50]]) {
      const m = new THREE.Mesh(new THREE.CylinderGeometry(0.22, 0.22, len, 10), pipe);
      m.rotation.x = Math.PI / 2;
      m.position.set(x, y, z);
      group.add(m);
    }

    /* ---------- lighting the volume ----------
       A room this size cannot be lit by point lights: the pool is a dozen
       slots for the whole mission. So the station lights itself the way a
       real one does — with strips. Emissive geometry costs no light slot and
       reads as the source; the hemisphere and the environment map do the
       actual illuminating. */
    const strip = (colour, x0, x1, y, z0, z1, intensity) => {
      const m = new THREE.Mesh(new THREE.BoxGeometry(x1 - x0, 0.14, z1 - z0),
                               M.emissive(colour, intensity == null ? 2.4 : intensity));
      m.position.set((x0 + x1) / 2, y, (z0 + z1) / 2);
      group.add(m);
      return m;
    };

    // ceiling coves down both sides of the concourse, the length of the room
    for (const side of [-1, 1]) {
      strip(0xdfefff, side * 29 - 0.6, side * 29 + 0.6, CEIL - 1.2, -34, 34, 3.0);
      strip(0xdfefff, side * 29 - 0.5, side * 29 + 0.5, DECK.b + 5.4, -32, 32, 1.9);
      strip(0xdfefff, side * 29 - 0.5, side * 29 + 0.5, DECK.a + 5.6, -24, 24, 1.9);
    }
    // deck-edge nosing, so every drop reads before you reach it
    for (const y of [DECK.b, DECK.c]) {
      strip(0x5eeaff, -13, 13, y + 0.02, -19.3, -18.9, 2.2);
      strip(0x5eeaff, -13, 13, y + 0.02, 18.9, 19.3, 2.2);
      strip(0x5eeaff, -13.3, -12.9, y + 0.02, -19, 19, 2.2);
      strip(0x5eeaff, 12.9, 13.3, y + 0.02, -19, 19, 2.2);
    }
    // the tube runs
    strip(0xdfefff, -0.6, 0.6, 6.7, -53, -27, 2.6);
    strip(0xdfefff, -0.6, 0.6, 6.2, 27, 45, 2.6);
    // the lab arms
    strip(0xdfefff, -45, -29, DECK.b + 5.4, -0.5, 0.5, 2.4);
    strip(0xdfefff, 29, 45, DECK.b + 5.4, -0.5, 0.5, 2.4);
    strip(0xdfefff, 21, 41, DECK.a + 5.9, -0.5, 0.5, 2.4);

    light(0, CEIL - 3, 0, 0xbfe9ff, 2.2, 70);
    light(0, DECK.b + 4, 0, 0xbfe9ff, 1.4, 46);
    light(0, DECK.a + 4, -38, 0x9ec4dd, 1.4, 30);
    light(0, DECK.a + 4, 36, 0x9ec4dd, 1.4, 30);
    // over the neck, not over the glass — the cupola is lit by the Earth
    light(-27, DECK.c + 3, -17, 0x8fd9ff, 1.3, 18);

    outside(group);

    const zones = {
      hub: { x0: -30, x1: 30, z0: -36, z1: 36, cx: 0, cz: 0, name: 'THE CRADLE' }
    };

    return {
      group, colliders, emitters, zones, props: [],
      lifts, terminals, decks: DECK, modules: MODULES,
      space: SF.spatial.create(colliders, 14),
      playerStart: new THREE.Vector3(0, 0, -48),
      bounds: { minX: -48, maxX: 48, minZ: -56, maxZ: 48 },
      flights
    };
  }

  /* ---------- what is out the window ----------
     Stars, the trusses and arrays that make the silhouette unmistakable,
     and the Earth underneath — which is the whole reason the cupola is
     worth the walk. The globe is drawn rather than loaded, so this needs
     no art to look like somewhere. */
  function outside(group) {
    const M = SF.materials;

    // starfield: a sphere of points around everything
    const N = 2600, pos = new Float32Array(N * 3);
    for (let i = 0; i < N; i++) {
      const u = Math.random() * 2 - 1, a = Math.random() * Math.PI * 2;
      const r = 900, s = Math.sqrt(1 - u * u);
      pos[i * 3] = r * s * Math.cos(a);
      pos[i * 3 + 1] = r * u;
      pos[i * 3 + 2] = r * s * Math.sin(a);
    }
    const stars = new THREE.Points(
      new THREE.BufferGeometry().setAttribute('position', new THREE.BufferAttribute(pos, 3)),
      new THREE.PointsMaterial({ color: 0xdfeaff, size: 2.4, sizeAttenuation: false, fog: false }));
    group.add(stars);

    // the Earth, drawn: ocean, a few continents, ice caps, a cloud band
    const S = 1024, cv = document.createElement('canvas');
    cv.width = S; cv.height = S / 2;
    const g = cv.getContext('2d');
    /* Painted dark on purpose. The globe is self-lit — the station's lamps
       have no business reaching it — and it goes through ACES tone mapping
       and a bloom pass on the way to the screen, both of which lift it. Paint
       it at the brightness of a photograph and it arrives as a white disc. */
    const sea = g.createLinearGradient(0, 0, 0, S / 2);
    sea.addColorStop(0, '#07223f'); sea.addColorStop(0.5, '#12507f'); sea.addColorStop(1, '#07223f');
    g.fillStyle = sea; g.fillRect(0, 0, S, S / 2);
    const blob = (cx, cy, rx, ry, fill) => {
      g.fillStyle = fill;
      g.beginPath();
      for (let a = 0; a <= Math.PI * 2 + 0.01; a += 0.24) {
        const w = 0.68 + Math.sin(a * 3.1 + cx) * 0.2 + Math.cos(a * 5.3) * 0.12;
        const x = cx + Math.cos(a) * rx * w, y = cy + Math.sin(a) * ry * w;
        a === 0 ? g.moveTo(x, y) : g.lineTo(x, y);
      }
      g.closePath(); g.fill();
    };
    for (const [cx, cy, rx, ry] of [[150, 150, 90, 70], [300, 300, 70, 90], [470, 170, 130, 80],
                                    [560, 320, 60, 55], [760, 200, 110, 90], [880, 340, 70, 60]]) {
      blob(cx, cy, rx, ry, '#275a31');
      blob(cx + rx * 0.3, cy + ry * 0.2, rx * 0.4, ry * 0.35, '#665a33');
    }
    g.fillStyle = '#cddeea';
    g.fillRect(0, 0, S, 26); g.fillRect(0, S / 2 - 26, S, 26);
    g.globalAlpha = 0.3;
    g.fillStyle = '#dbe8f2';
    for (let i = 0; i < 90; i++) {
      const x = Math.random() * S, y = Math.random() * S / 2;
      g.beginPath(); g.ellipse(x, y, 30 + Math.random() * 70, 9 + Math.random() * 14, 0, 0, 6.3); g.fill();
    }
    g.globalAlpha = 1;

    /* The terminator: one edge of the globe is in shadow, with a thin band of
       city light along it. Without this the sphere reads as a flat disc. */
    const night = g.createLinearGradient(0, 0, S, 0);
    night.addColorStop(0, 'rgba(0,0,0,0.80)');
    night.addColorStop(0.13, 'rgba(0,0,0,0.30)');
    night.addColorStop(0.24, 'rgba(0,0,0,0)');
    night.addColorStop(0.86, 'rgba(0,0,0,0)');
    night.addColorStop(1, 'rgba(0,0,0,0.72)');
    g.fillStyle = night;
    g.fillRect(0, 0, S, S / 2);
    g.globalAlpha = 0.5;
    g.fillStyle = '#ffcf80';
    for (let i = 0; i < 120; i++) {
      const x = Math.random() * S * 0.2, y = 40 + Math.random() * (S / 2 - 80);
      g.fillRect(x, y, 1 + Math.random() * 2, 1 + Math.random() * 2);
    }
    g.globalAlpha = 1;
    const tex = new THREE.CanvasTexture(cv);
    if (THREE.sRGBEncoding !== undefined) tex.encoding = THREE.sRGBEncoding;

    /* Two hundred metres down and nine hundred across, which is nothing like
       the real numbers and exactly like the real view: a curved horizon that
       fills the window and runs off both edges. Fog is off on all of it —
       interior haze has no business out there. */
    const earth = new THREE.Mesh(new THREE.SphereGeometry(900, 64, 40),
      new THREE.MeshBasicMaterial({ map: tex, fog: false }));
    earth.position.set(-90, -1080, -60);
    earth.rotation.y = 1.1;
    group.add(earth);

    // the atmosphere on the limb, brightest where it is edge-on
    const air = new THREE.Mesh(new THREE.SphereGeometry(936, 48, 28),
      new THREE.MeshBasicMaterial({ color: 0x2e6ea8, transparent: true, opacity: 0.13,
                                    side: THREE.BackSide, depthWrite: false, fog: false }));
    air.position.copy(earth.position);
    group.add(air);

    // the sun, low and off to one side, so the arrays have something to face
    const sun = new THREE.Mesh(new THREE.SphereGeometry(26, 20, 14),
      new THREE.MeshBasicMaterial({ color: 0xfff6e2, fog: false }));
    sun.position.set(-420, 240, -560);
    group.add(sun);

    /* Trusses and solar arrays. Drawn outside the pressure hull and never
       collided against — you are never out there. */
    const truss = M.get('pipe');
    const panel = new THREE.MeshStandardMaterial({
      color: 0x2b2f52, metalness: 0.5, roughness: 0.35,
      emissive: new THREE.Color(0x101830), emissiveIntensity: 1
    });
    const beam = new THREE.Mesh(new THREE.BoxGeometry(150, 1.6, 1.6), truss);
    beam.position.set(0, DECK.b + 12, -6);
    group.add(beam);
    for (const side of [-1, 1]) {
      for (let i = 0; i < 4; i++) {
        const wing = new THREE.Mesh(new THREE.BoxGeometry(34, 0.25, 12), panel);
        wing.position.set(side * (40 + i * 22), DECK.b + 12, -6 + (i % 2 ? 16 : -16));
        wing.rotation.z = side * 0.06;
        group.add(wing);
      }
      const radiator = new THREE.Mesh(new THREE.BoxGeometry(20, 0.2, 8),
        new THREE.MeshStandardMaterial({ color: 0xc9d2dc, metalness: 0.3, roughness: 0.6 }));
      radiator.position.set(side * 34, DECK.b + 11, 22);
      group.add(radiator);
    }
  }

  /* The hub runs through the same mission pipeline as everything else, so
     it describes itself the same way — with no waves and nothing to do. */
  function asMission() {
    return {
      id: 'cradle', n: 'THE CRADLE', zone: 'hub', from: null,
      name: 'THE CRADLE', objective: 'Nothing is shooting at you. Take your time.',
      brief: '', waves: [], beats: [
        [1.0, 'DIVISION', 'Cradle control has you. Welcome back.'],
        [9.0, 'VOSS', 'Cupola is two decks up and out to port. Worth the stairs.']
      ],
      hub: true, patrol: false
    };
  }

  SF.station = { build, asMission, DECK, MODULES };
})(window.SF);
