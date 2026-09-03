/* ============================================================
   Hostile rigs.

   The enemies used to be a capsule, a sphere and two more capsules for
   limbs — readable at a glance, which is what a shooter needs, but they
   read as placeholders because a real machine has a silhouette made of
   parts: plates that overlap, joints that bend one way, a head sunk
   into a collar, something venting at the back.

   So each archetype is assembled here out of a few dozen primitives
   arranged in a joint hierarchy, wearing the worn armour material from
   fps/materials.js. Three things keep that affordable with thirty of
   them on screen:

     * geometry is built once per type and shared by every instance;
     * the armour material is built once per type and *cloned* per
       instance, so the maps are shared but the hit flash is not;
     * joints are plain Object3Ds, so posing is a handful of Euler
       assignments rather than skinning.

   The rig keeps the shape ai.js expects — group, head, limbs, materials
   — and adds pose(), which does the walking.
   ============================================================ */
(function (SF) {
  'use strict';

  const geoCache = new Map();     // typeId -> { parts }
  const matCache = new Map();     // typeId -> { armour, glow, visor }
  const junk = [];                // everything to dispose between missions

  function keep(x) { junk.push(x); return x; }

  /* ---------- texel density ----------
     Every primitive's UVs span 0..1, so an armour texture mapped straight
     onto a twenty-centimetre elbow shows the same four plates as the whole
     torso does, and the enemy ends up dressed in tweed. Each part therefore
     has its UVs scaled by its real size, so one armour plate is the same
     twenty-eight centimetres across wherever it appears. */
  const TILE = 1.1;                       // metres covered by one texture tile

  function scaleUV(geo, su, sv) {
    const uv = geo.attributes.uv;
    for (let i = 0; i < uv.count; i++) uv.setXY(i, uv.getX(i) * su, uv.getY(i) * sv);
    uv.needsUpdate = true;
    return keep(geo);
  }

  /* Box faces each run along a different pair of the box's dimensions, in
     the order +X, -X, +Y, -Y, +Z, -Z, four vertices apiece. */
  function box(w, h, d) {
    const geo = new THREE.BoxGeometry(w, h, d);
    const uv = geo.attributes.uv;
    const spans = [[d, h], [d, h], [w, d], [w, d], [w, h], [w, h]];
    for (let f = 0; f < 6; f++) {
      const su = spans[f][0] / TILE, sv = spans[f][1] / TILE;
      for (let i = 0; i < 4; i++) {
        const k = f * 4 + i;
        uv.setXY(k, uv.getX(k) * su, uv.getY(k) * sv);
      }
    }
    uv.needsUpdate = true;
    return keep(geo);
  }

  const cyl = (rt, rb, h, seg) => scaleUV(
    new THREE.CylinderGeometry(rt, rb, h, seg || 10),
    (2 * Math.PI * Math.max(rt, rb)) / TILE, h / TILE);

  const sph = (r, w, h) => scaleUV(
    new THREE.SphereGeometry(r, w || 12, h || 10),
    (2 * Math.PI * r) / TILE, (Math.PI * r) / TILE);

  /* A part is placed relative to its parent joint, so the joint can rotate
     around the shoulder or knee rather than around the middle of the limb. */
  function joint(parent, x, y, z) {
    const j = new THREE.Object3D();
    j.position.set(x, y, z);
    parent.add(j);
    return j;
  }

  function part(parent, geo, mat, x, y, z, rx, ry, rz) {
    const m = new THREE.Mesh(geo, mat);
    m.position.set(x || 0, y || 0, z || 0);
    if (rx || ry || rz) m.rotation.set(rx || 0, ry || 0, rz || 0);
    m.castShadow = true;
    m.receiveShadow = true;
    parent.add(m);
    return m;
  }

  /* ---------- materials ----------
     One armour texture set per type, cloned per instance. A clone shares
     every map by reference, so thirty marauders cost one set of canvases
     and thirty cheap uniform blocks. */
  function baseMaterials(typeId, spec) {
    if (matCache.has(typeId)) return matCache.get(typeId);

    const armour = SF.materials.armourPlate(spec.colour, {
      size: 256,
      panel: spec.elite || spec.boss ? 52 : 64,
      wear: spec.elite || spec.boss ? 1.5 : 1,
      rough: 0.5
    });
    SF.shading.anisotropic(armour, 0.4, new THREE.Vector3(0, 1, 0));

    /* The lit parts: an optic, a reactor seam, thruster wash. Nearly black
       when unlit, so they read as light sources rather than paint. */
    const glow = new THREE.MeshStandardMaterial({
      /* Bright enough to bloom a little, not so bright that the tone curve
         clips it to white and throws its colour away. */
      color: 0x07080b, emissive: new THREE.Color(spec.glow), emissiveIntensity: 0.75,
      metalness: 0.2, roughness: 0.35
    });
    glow.userData.pbr = 'skip';

    /* Visor: dark, near-mirror glass over the optic behind it. */
    const visor = new THREE.MeshStandardMaterial({
      color: 0x05070a, metalness: 1.0, roughness: 0.08,
      emissive: new THREE.Color(spec.glow), emissiveIntensity: 0.9
    });
    SF.shading.anisotropic(visor, 0.55, new THREE.Vector3(1, 0, 0));

    const set = { armour, glow, visor };
    matCache.set(typeId, set);
    return set;
  }

  /* ---------- geometry sets ----------
     Sized off the type's own height and radius, so one builder serves a
     one-metre drone and a three-metre elite. */
  function walkerGeo(h, r, heavy) {
    const w = heavy ? 1.25 : 1;
    return {
      pelvis:   box(r * 1.5 * w, h * 0.10, r * 1.0),
      abdomen:  box(r * 1.30 * w, h * 0.13, r * 0.92),
      chest:    box(r * 1.95 * w, h * 0.20, r * 1.15),
      chestTop: box(r * 1.70 * w, h * 0.05, r * 1.05),
      collar:   cyl(r * 0.56, r * 0.72, h * 0.06, 10),
      neck:     cyl(r * 0.34, r * 0.42, h * 0.085, 8),
      skull:    box(r * 0.94, r * 0.86, r * 0.98),
      brow:     box(r * 1.00, r * 0.18, r * 0.34),
      trap:     box(r * 0.60, r * 0.30, r * 0.66),
      visor:    box(r * 0.58, r * 0.11, r * 0.06),
      jaw:      box(r * 0.62, r * 0.24, r * 0.66),
      pauldron: sph(r * 0.52, 10, 8),
      upperArm: cyl(r * 0.17, r * 0.15, h * 0.19, 8),
      elbow:    sph(r * 0.17, 8, 6),
      foreArm:  cyl(r * 0.15, r * 0.12, h * 0.18, 8),
      hand:     box(r * 0.22, r * 0.26, r * 0.20),
      hip:      sph(r * 0.26, 8, 6),
      thigh:    cyl(r * 0.24, r * 0.20, h * 0.23, 8),
      knee:     sph(r * 0.21, 8, 6),
      shin:     cyl(r * 0.20, r * 0.15, h * 0.23, 8),
      foot:     box(r * 0.34, h * 0.045, r * 0.72),
      pack:     box(r * 1.30 * w, h * 0.17, r * 0.34),
      vent:     cyl(r * 0.13, r * 0.16, h * 0.09, 7),
      core:     cyl(r * 0.13, r * 0.13, r * 0.05, 10),
      cannon:   box(r * 0.46, r * 0.42, h * 0.30),
      barrel:   cyl(r * 0.11, r * 0.13, h * 0.24, 8),
      rib:      box(r * 1.86 * w, h * 0.022, r * 1.18)
    };
  }

  function flyerGeo(h, r) {
    return {
      hull:     sph(r * 0.62, 14, 10),
      cowlTop:  cyl(r * 0.50, r * 0.66, r * 0.30, 12),
      cowlLow:  cyl(r * 0.60, r * 0.30, r * 0.34, 12),
      ringSeg:  box(r * 0.30, r * 0.11, r * 0.16),
      lensRim:  cyl(r * 0.28, r * 0.24, r * 0.16, 12),
      lens:     cyl(r * 0.19, r * 0.19, r * 0.05, 12),
      fin:      box(r * 0.09, r * 0.44, r * 0.62),
      thruster: cyl(r * 0.20, r * 0.28, r * 0.26, 10),
      wash:     cyl(r * 0.17, r * 0.05, r * 0.14, 10),
      armSeg:   cyl(r * 0.06, r * 0.05, r * 0.42, 6),
      claw:     box(r * 0.08, r * 0.20, r * 0.08)
    };
  }

  function geometryFor(typeId, spec) {
    if (geoCache.has(typeId)) return geoCache.get(typeId);
    const g = spec.flying ? flyerGeo(spec.height, spec.radius)
                          : walkerGeo(spec.height, spec.radius, !!(spec.elite || spec.boss));
    geoCache.set(typeId, g);
    return g;
  }

  /* ---------- the walker ----------
     Head centre sits at 93% of height and the torso straddles 58%, because
     that is where ai.js puts the head and body hit spheres. The silhouette
     is built around those two points rather than the other way round. */
  function buildWalker(spec, G, M) {
    const h = spec.height, r = spec.radius;
    const heavy = !!(spec.elite || spec.boss);
    const group = new THREE.Group();

    const A = M.armour, GL = M.glow;

    // spine
    const hips = joint(group, 0, h * 0.46, 0);
    part(hips, G.pelvis, A, 0, 0, 0);
    const spine = joint(hips, 0, h * 0.05, 0);
    part(spine, G.abdomen, A, 0, h * 0.055, 0);
    const chestJ = joint(spine, 0, h * 0.14, 0);
    part(chestJ, G.chest, A, 0, 0, 0);
    part(chestJ, G.chestTop, A, 0, h * 0.11, heavy ? -r * 0.05 : 0);
    // overlapping ribs down the front, so the torso is not one slab
    for (let i = 0; i < 3; i++) part(chestJ, G.rib, A, 0, -h * 0.055 + i * h * 0.045, 0);
    // reactor seam
    part(chestJ, G.core, GL, 0, h * 0.005, -r * 0.60, Math.PI / 2, 0, 0);

    // back pack and its vents
    part(chestJ, G.pack, A, 0, h * 0.01, r * 0.72);
    for (const s of [-1, 1]) {
      part(chestJ, G.vent, A, s * r * 0.48, h * 0.10, r * 0.74);
      part(chestJ, G.vent, GL, s * r * 0.48, h * 0.155, r * 0.74).scale.setScalar(0.55);
    }

    // head: skull, brow, a lit visor slit, and a jaw sunk into the collar
    part(chestJ, G.collar, A, 0, h * 0.13, 0);
    /* Plates sloping up from each shoulder to the collar. Without them the
       head reads as a box parked on a shelf; with them the shoulders lead
       the eye up to it. */
    for (const s2 of [-1, 1]) {
      const trap = part(chestJ, G.trap, A, s2 * r * 0.52, h * 0.115, 0);
      trap.rotation.z = s2 * 0.42;
    }
    // a neck, so the head is attached to the body rather than hovering there
    part(chestJ, G.neck, A, 0, h * 0.170, 0);
    const neck = joint(chestJ, 0, h * 0.155, 0);
    const headJ = joint(neck, 0, h * 0.93 - h * 0.46 - h * 0.05 - h * 0.14 - h * 0.155, 0);
    const skull = part(headJ, G.skull, A, 0, 0, 0);
    part(headJ, G.brow, A, 0, r * 0.30, -r * 0.34);
    part(headJ, G.visor, M.visor, 0, r * 0.02, -r * 0.45);
    part(headJ, G.jaw, A, 0, -r * 0.30, -r * 0.12);

    // shoulders, arms
    const limbs = [];
    for (const side of [-1, 1]) {
      const shoulder = joint(chestJ, side * r * (heavy ? 0.94 : 0.84), h * 0.055, 0);
      const pauldron = part(shoulder, G.pauldron, A, side * r * 0.10, h * 0.012, 0);
      pauldron.scale.set(1.02, 0.78, 1.05);
      part(shoulder, G.upperArm, A, 0, -h * 0.095, 0);
      const elbow = joint(shoulder, 0, -h * 0.19, 0);
      part(elbow, G.elbow, A, 0, 0, 0);
      part(elbow, G.foreArm, A, 0, -h * 0.09, 0);
      const hand = part(elbow, G.hand, A, 0, -h * 0.195, -r * 0.04);
      limbs.push({ side, shoulder, elbow, hand, arm: shoulder });
    }

    // hips, legs
    for (let i = 0; i < 2; i++) {
      const side = i ? 1 : -1;
      const hipJ = joint(hips, side * r * 0.45, -h * 0.02, 0);
      part(hipJ, G.hip, A, 0, 0, 0);
      part(hipJ, G.thigh, A, 0, -h * 0.115, 0);
      const knee = joint(hipJ, 0, -h * 0.23, 0);
      part(knee, G.knee, A, 0, 0, 0);
      part(knee, G.shin, A, 0, -h * 0.115, 0);
      part(knee, G.foot, A, 0, -h * 0.195, -r * 0.14);
      limbs[i].knee = knee;
      limbs[i].hip = hipJ;
      limbs[i].leg = hipJ;                       // ai.js still knows this name
    }

    /* Ranged types carry the weapon rather than mime it: a shoulder mount
       for the heavies, a forearm cannon for everyone else. */
    let muzzle = null;
    if (spec.ranged) {
      if (heavy) {
        const mount = joint(chestJ, r * 1.05, h * 0.10, 0);
        part(mount, G.cannon, A, 0, r * 0.30, -h * 0.02);
        muzzle = part(mount, G.barrel, A, 0, r * 0.30, -h * 0.17, Math.PI / 2, 0, 0);
        part(mount, G.lensRim || G.core, GL, 0, r * 0.30, -h * 0.29, Math.PI / 2, 0, 0);
      } else {
        const fore = limbs[1].elbow;
        part(fore, G.cannon, A, 0, -h * 0.10, -r * 0.28).scale.set(0.8, 0.8, 0.8);
        muzzle = part(fore, G.barrel, A, 0, -h * 0.13, -r * 0.55, Math.PI / 2, 0, 0);
        part(fore, G.core, GL, 0, -h * 0.13, -r * 0.72, Math.PI / 2, 0, 0).scale.setScalar(0.7);
      }
    }

    return { group, limbs, head: skull, headJ, chestJ, hips, spine, muzzle, flying: false };
  }

  /* ---------- the flyer ----------
     A core inside a split cowl, a ring of segments that spins around it, one
     big optic, three fins and a thruster underneath. Everything a walker
     uses to read as a body, replaced with something that reads as a machine
     with nothing inside it. */
  function buildFlyer(spec, G, M) {
    const r = spec.radius;
    const group = new THREE.Group();
    const A = M.armour, GL = M.glow;

    const core = joint(group, 0, spec.height * 0.62, 0);
    part(core, G.hull, A, 0, 0, 0).scale.set(1, 0.86, 1.12);
    part(core, G.cowlTop, A, 0, r * 0.34, 0);
    part(core, G.cowlLow, A, 0, -r * 0.36, 0);

    // the optic, recessed in a rim so it reads as a lens rather than a dot
    part(core, G.lensRim, A, 0, 0, -r * 0.62, Math.PI / 2, 0, 0);
    const eye = part(core, G.lens, GL, 0, 0, -r * 0.70, Math.PI / 2, 0, 0);

    // fins
    for (let i = 0; i < 3; i++) {
      const a = (i / 3) * Math.PI * 2;
      const fin = part(core, G.fin, A, Math.sin(a) * r * 0.66, -r * 0.05, Math.cos(a) * r * 0.66);
      fin.rotation.y = a;
      fin.rotation.z = 0.22;
    }

    // the ring: separate segments, so it reads as machinery when it turns
    const ring = new THREE.Object3D();
    core.add(ring);
    for (let i = 0; i < 8; i++) {
      const a = (i / 8) * Math.PI * 2;
      const seg = part(ring, G.ringSeg, i % 2 ? GL : A,
                       Math.sin(a) * r * 1.02, 0, Math.cos(a) * r * 1.02);
      seg.rotation.y = a;
    }

    // thruster and its wash
    part(core, G.thruster, A, 0, -r * 0.62, 0);
    part(core, G.wash, GL, 0, -r * 0.82, 0);

    // two manipulator arms hanging below, which give it a sense of scale
    const claws = [];
    for (const side of [-1, 1]) {
      const shoulder = joint(core, side * r * 0.34, -r * 0.5, -r * 0.1);
      shoulder.rotation.x = 0.5;
      part(shoulder, G.armSeg, A, 0, -r * 0.21, 0);
      const elbow = joint(shoulder, 0, -r * 0.42, 0);
      elbow.rotation.x = -0.9;
      part(elbow, G.claw, A, 0, -r * 0.10, 0);
      claws.push({ shoulder, elbow, side });
    }

    return { group, limbs: [{ ring }], head: eye, headJ: core, chestJ: core,
             ring, claws, muzzle: eye, flying: true };
  }

  /* ---------- posing ----------
     Two gaits and an idle. The knee only bends one way, the torso counter-
     rotates against the legs, and the whole body rises and falls on each
     step — the three things that separate walking from two capsules
     swinging past each other. */
  function poseWalker(rig, phase, speed, dt) {
    const swing = Math.sin(phase) * Math.min(0.62, 0.16 + speed * 0.20);
    const stride = Math.min(1, speed / 2.6);

    for (const l of rig.limbs) {
      const s = l.side;
      const leg = swing * s;
      l.hip.rotation.x = leg;
      // a knee that bends backwards is the single clearest tell of a fake walk
      l.knee.rotation.x = Math.max(0, -leg) * 1.7 + 0.06;
      l.shoulder.rotation.x = -leg * 0.72;
      l.shoulder.rotation.z = s * (0.06 + stride * 0.05);
      l.elbow.rotation.x = -0.32 - Math.max(0, leg) * 0.55;
    }

    // rise and fall twice per stride, and lean into the movement
    rig.hips.position.y = rig.baseHipY + Math.abs(Math.sin(phase)) * 0.035 * stride;
    rig.chestJ.rotation.y = -swing * 0.16;
    rig.hips.rotation.y = swing * 0.10;
    rig.spine.rotation.x = 0.07 + stride * 0.11;

    // breathing, so a standing enemy is not a statue
    if (stride < 0.05) {
      rig.spine.rotation.x = 0.07 + Math.sin(phase * 0.6) * 0.012;
    }
    void dt;
  }

  function poseFlyer(rig, phase, speed, dt) {
    rig.ring.rotation.y += dt * (2.2 + speed * 1.1);
    rig.chestJ.rotation.z = Math.sin(phase * 0.7) * 0.10;
    rig.chestJ.rotation.x = 0.06 + Math.min(0.3, speed * 0.08);
    for (const c of rig.claws) {
      c.shoulder.rotation.x = 0.5 + Math.sin(phase * 0.9 + c.side) * 0.10;
      c.elbow.rotation.x = -0.9 + Math.sin(phase * 1.3 + c.side) * 0.12;
    }
  }

  /* ---------- entry point ---------- */
  function build(typeId, spec) {
    const G = geometryFor(typeId, spec);
    const base = baseMaterials(typeId, spec);

    /* Per-instance clones: the maps are shared by reference, so this is a
       few small objects, but the hit flash and the death fade belong to one
       enemy rather than to every enemy of that type. */
    const M = {
      armour: base.armour.clone(),
      glow: base.glow.clone(),
      visor: base.visor.clone()
    };
    /* The hit flash rides the armour's emissive, so the clone needs a colour
       to raise. ai.js drives the intensity; this only says what colour it
       flashes when something lands. */
    M.armour.emissive = new THREE.Color(0xff2222);
    M.armour.emissiveIntensity = 0;
    M.armour.userData = Object.assign({}, base.armour.userData);
    M.visor.userData = Object.assign({}, base.visor.userData);
    M.glow.userData = Object.assign({}, base.glow.userData);

    const rig = spec.flying ? buildFlyer(spec, G, M) : buildWalker(spec, G, M);
    rig.baseHipY = rig.hips ? rig.hips.position.y : 0;
    rig.materials = [M.armour, M.glow, M.visor];

    rig.pose = function (phase, speed, dt) {
      if (rig.flying) poseFlyer(rig, phase, speed, dt);
      else poseWalker(rig, phase, speed, dt);
    };

    /* The head lags the turn slightly and tips towards whatever it is
       looking at, which reads as attention. */
    rig.look = function (pitch, yawOffset) {
      if (!rig.headJ || rig.flying) return;
      rig.headJ.rotation.x = THREE.MathUtils.clamp(pitch, -0.5, 0.5);
      rig.headJ.rotation.y = THREE.MathUtils.clamp(yawOffset, -0.7, 0.7);
    };

    return rig;
  }

  /* Geometry and the base materials outlive any one enemy, so they are not
     caught by the scene traversal that disposes a finished mission. */
  function reset() {
    for (const g of junk) g.dispose();
    junk.length = 0;
    for (const set of matCache.values()) {
      for (const m of [set.armour, set.glow, set.visor]) {
        for (const k of ['map', 'normalMap', 'roughnessMap', 'metalnessMap']) {
          if (m[k]) m[k].dispose();
        }
        m.dispose();
      }
    }
    geoCache.clear();
    matCache.clear();
  }

  SF.hostiles = { build, reset };
})(window.SF);
