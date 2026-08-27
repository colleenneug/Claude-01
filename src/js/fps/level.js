/* ============================================================
   The ship interior.

   Everything is authored declaratively as rooms and corridors and then
   built into meshes plus a flat list of AABB colliders. Walls are cut
   by "openings" so doorways are real holes rather than trigger volumes.
   ============================================================ */
(function (SF) {
  'use strict';

  const WALL_H = 4.2;
  const T = 0.35;            // wall thickness

  /* ---------- layout ---------- */
  /* Spaces are axis-aligned boxes on the XZ plane. Doorways are declared
     on the shared wall between two spaces and cut from both. */
  const LAYOUT = {
    spaces: [
      { id: 'dock',      x: -8,  z: -46, w: 16, d: 14, h: 4.6, light: 0x9fc4d8, name: 'DOCKING COLLAR 4-A' },
      { id: 'spine1',    x: -3,  z: -32, w: 6,  d: 26, h: 4.2, light: 0x7fd8e8, name: 'MAINTENANCE SPINE' },
      { id: 'junction',  x: -9,  z: -6,  w: 18, d: 14, h: 5.0, light: 0xffb454, name: 'JUNCTION 9' },
      { id: 'spine2',    x: -3,  z: 8,   w: 6,  d: 18, h: 4.2, light: 0x7fd8e8, name: 'SPINE / AFT' },
      { id: 'promenade', x: -20, z: 26,  w: 40, d: 30, h: 7.5, light: 0xff7a9c, name: 'HABITAT RING TWO' },
      { id: 'reactor',   x: -13, z: 56,  w: 26, d: 22, h: 6.5, light: 0xff5a4a, name: 'REACTOR ANTECHAMBER' }
    ],
    /* doorway: [spaceA, spaceB, axis, centre, width] — axis 'x' means the
       shared wall runs along X (a north/south doorway). */
    doors: [
      ['dock', 'spine1', 'x', 0, 3.2],
      ['spine1', 'junction', 'x', 0, 3.2],
      ['junction', 'spine2', 'x', 0, 3.2],
      ['spine2', 'promenade', 'x', 0, 3.6],
      ['promenade', 'reactor', 'x', 0, 4.4]
    ]
  };

  const spaceById = (id) => LAYOUT.spaces.find((s) => s.id === id);

  /* ---------- build ---------- */

  function build(scene) {
    const colliders = [];      // {min:{x,z}, max:{x,z}, top} — the player is blocked in XZ
    const group = new THREE.Group();
    /* Fixtures are recorded as data, not as THREE lights: the light pool
       assigns a fixed set of real lights to whichever are nearest. */
    const emitters = [];
    const emit = (x, y, z, colour, intensity, distance) =>
      emitters.push({ x, y, z, colour, intensity, distance });
    const zones = {};
    const props = [];

    const M = SF.materials;

    /* Doorway openings collected per wall side of each space. */
    const openings = {};
    const addOpening = (id, side, centre, width) => {
      openings[id] = openings[id] || { n: [], s: [], e: [], w: [] };
      openings[id][side].push([centre - width / 2, centre + width / 2]);
    };

    for (const [aId, bId, , centre, width] of LAYOUT.doors) {
      const a = spaceById(aId), b = spaceById(bId);
      // b is always further along +Z in this layout
      addOpening(a.id, 's', centre, width);
      addOpening(b.id, 'n', centre, width);
      void a; void b;
    }

    function box(w, h, d, mat, x, y, z, solid) {
      const geo = new THREE.BoxGeometry(w, h, d);
      const mesh = new THREE.Mesh(geo, mat);
      mesh.position.set(x, y, z);
      mesh.castShadow = solid !== false;
      mesh.receiveShadow = true;
      group.add(mesh);
      if (solid !== false) {
        colliders.push({
          min: { x: x - w / 2, z: z - d / 2 },
          max: { x: x + w / 2, z: z + d / 2 },
          top: y + h / 2, bottom: y - h / 2
        });
      }
      return mesh;
    }

    /* A wall run along one axis, minus its doorway openings. */
    function wallRun(axis, fixed, from, to, height, mat, gaps) {
      const segs = [];
      const sorted = (gaps || []).slice().sort((p, q) => p[0] - q[0]);
      let cursor = from;
      for (const [g0, g1] of sorted) {
        if (g0 > cursor) segs.push([cursor, Math.min(g0, to)]);
        cursor = Math.max(cursor, g1);
      }
      if (cursor < to) segs.push([cursor, to]);

      for (const [s0, s1] of segs) {
        const len = s1 - s0;
        if (len <= 0.01) continue;
        const mid = (s0 + s1) / 2;
        if (axis === 'x') box(len, height, T, mat, mid, height / 2, fixed);
        else box(T, height, len, mat, fixed, height / 2, mid);
      }
      // lintel above each doorway keeps the wall reading as solid
      for (const [g0, g1] of sorted) {
        const len = g1 - g0, mid = (g0 + g1) / 2, lintelH = height - 2.6;
        if (lintelH <= 0) continue;
        if (axis === 'x') box(len, lintelH, T, mat, mid, 2.6 + lintelH / 2, fixed, false);
        else box(T, lintelH, len, mat, fixed, 2.6 + lintelH / 2, mid, false);
      }
    }

    /* ---------- spaces ---------- */
    for (const s of LAYOUT.spaces) {
      const x0 = s.x, x1 = s.x + s.w, z0 = s.z, z1 = s.z + s.d;
      const cx = s.x + s.w / 2, cz = s.z + s.d / 2;
      const op = openings[s.id] || { n: [], s: [], e: [], w: [] };

      zones[s.id] = { x0, x1, z0, z1, cx, cz, name: s.name };

      // floor
      const floorMat = s.id === 'promenade' ? M.get('deck') : M.get('floor');
      const floor = new THREE.Mesh(new THREE.BoxGeometry(s.w, 0.4, s.d), floorMat);
      floor.position.set(cx, -0.2, cz);
      floor.receiveShadow = true;
      group.add(floor);

      // ceiling
      const ceil = new THREE.Mesh(new THREE.BoxGeometry(s.w, 0.3, s.d), M.get('ceiling'));
      ceil.position.set(cx, s.h, cz);
      ceil.receiveShadow = true;
      group.add(ceil);

      const wallMat = s.id === 'reactor' ? M.get('hazard')
                    : s.id === 'promenade' ? M.get('hullDark') : M.get('hull');

      wallRun('x', z0, x0, x1, s.h, wallMat, op.n);
      wallRun('x', z1, x0, x1, s.h, wallMat, op.s);
      wallRun('z', x0, z0, z1, s.h, wallMat, op.w);
      wallRun('z', x1, z0, z1, s.h, wallMat, op.e);

      // light strips down both long walls, and the lights themselves
      const stripMat = SF.materials.emissive(s.light, 2.6);
      const along = s.d >= s.w;
      const count = Math.max(2, Math.floor((along ? s.d : s.w) / 7));
      for (let i = 0; i < count; i++) {
        const t = (i + 0.5) / count;
        const px = along ? cx : x0 + s.w * t;
        const pz = along ? z0 + s.d * t : cz;

        const strip = new THREE.Mesh(
          new THREE.BoxGeometry(along ? s.w * 0.55 : 0.5, 0.12, along ? 0.5 : s.d * 0.55), stripMat);
        strip.position.set(px, s.h - 0.28, pz);
        group.add(strip);

        emit(px, s.h - 0.7, pz, s.light, 3.4, 24);
      }
    }

    /* ---------- props ---------- */
    const crate = (x, z, ry, scale) => {
      const sc = scale || 1;
      const m = box(1.5 * sc, 1.2 * sc, 1.1 * sc, M.get('crate'), x, 0.6 * sc, z);
      m.rotation.y = ry || 0;
      props.push(m);
      return m;
    };
    const barrel = (x, z) => {
      const geo = new THREE.CylinderGeometry(0.42, 0.42, 1.15, 16);
      const m = new THREE.Mesh(geo, M.get('pipe'));
      m.position.set(x, 0.575, z);
      m.castShadow = m.receiveShadow = true;
      group.add(m);
      colliders.push({ min: { x: x - 0.42, z: z - 0.42 }, max: { x: x + 0.42, z: z + 0.42 },
                       top: 1.15, bottom: 0 });
      props.push(m);
    };
    const pipeRun = (x, y, z, len, axis) => {
      const geo = new THREE.CylinderGeometry(0.16, 0.16, len, 10);
      const m = new THREE.Mesh(geo, M.get('pipe'));
      m.position.set(x, y, z);
      if (axis === 'z') m.rotation.x = Math.PI / 2; else m.rotation.z = Math.PI / 2;
      m.castShadow = true;
      group.add(m);
    };
    const console_ = (x, z, ry, colour) => {
      const body = box(1.6, 1.0, 0.7, M.get('crate'), x, 0.5, z);
      body.rotation.y = ry || 0;
      const scr = new THREE.Mesh(new THREE.PlaneGeometry(1.2, 0.55),
        SF.materials.emissive(colour || 0x5eeaff, 1.7));
      scr.position.set(x, 1.02, z);
      scr.rotation.set(-Math.PI / 3, ry || 0, 0);
      group.add(scr);
      emit(x, 1.4, z, colour || 0x5eeaff, 0.9, 6);
    };

    // docking collar
    crate(-5.5, -43, 0.3); crate(-4.2, -41.6, -0.2); crate(-6.2, -41.4, 0.9, 0.8);
    barrel(4.6, -42.5); barrel(5.4, -41.3);
    console_(5.2, -44.6, -0.5, 0x5eeaff);
    for (let z = -45; z < -34; z += 3.5) pipeRun(-7.4, 3.5, z + 1.75, 3.5, 'z');

    // spine
    for (let z = -30; z < -8; z += 4) { pipeRun(-2.4, 3.6, z + 2, 4, 'z'); pipeRun(2.4, 3.4, z + 2, 4, 'z'); }
    crate(-1.6, -20, 0.2, 0.9); barrel(1.7, -14.5);

    // junction
    crate(-7, -2, 0.4); crate(-5.6, -3.2, -0.3); crate(6.2, -2.4, 0.8);
    barrel(-6.4, 1.5); barrel(5.5, 1.2); barrel(6.3, 2.1);
    console_(-8, 3.4, 0.9, 0xffb454);
    console_(7.4, -0.5, -1.2, 0xffb454);

    // promenade — the big open space, with a dead fountain and planters
    const fountain = new THREE.Mesh(new THREE.CylinderGeometry(3.2, 3.6, 0.9, 24), M.get('crate'));
    fountain.position.set(0, 0.45, 41);
    fountain.castShadow = fountain.receiveShadow = true;
    group.add(fountain);
    colliders.push({ min: { x: -3.4, z: 37.6 }, max: { x: 3.4, z: 44.4 }, top: 0.9, bottom: 0 });
    for (let i = 0; i < 6; i++) {
      const ang = (i / 6) * Math.PI * 2;
      crate(Math.cos(ang) * 12, 41 + Math.sin(ang) * 10, ang, 1.2);
    }
    for (let i = 0; i < 5; i++) { barrel(-16 + i * 1.4, 30 + (i % 2) * 1.2); }
    console_(-15, 48, 0.7, 0xff5ea8);
    console_(15, 34, -2.2, 0xff5ea8);

    // reactor antechamber
    crate(-9, 60, 0.2); crate(-7.6, 61.4, 0.7); crate(9, 62, -0.4);
    barrel(-10.5, 66); barrel(9.8, 66.5); barrel(8.9, 67.4);
    console_(0, 74.5, Math.PI, 0xff5a4a);
    for (let x = -10; x <= 10; x += 5) pipeRun(x, 5.6, 67, 22, 'z');

    // the reactor itself: a tall emissive column behind glass
    const core = new THREE.Mesh(new THREE.CylinderGeometry(2.2, 2.2, 6.2, 24),
      SF.materials.emissive(0xff6a4a, 3.0));
    core.position.set(0, 3.1, 72);
    group.add(core);
    emit(0, 3.4, 72, 0xff6a4a, 4.2, 32);
    colliders.push({ min: { x: -2.4, z: 69.6 }, max: { x: 2.4, z: 74.4 }, top: 6.2, bottom: 0 });

    scene.add(group);

    return {
      group, colliders, emitters, zones, props,
      playerStart: new THREE.Vector3(0, 0, -40),
      bounds: { minX: -22, maxX: 22, minZ: -48, maxZ: 80 }
    };
  }

  SF.level = { build, LAYOUT, WALL_H };
})(window.SF);
