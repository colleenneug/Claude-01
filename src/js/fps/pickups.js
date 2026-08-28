/* ============================================================
   Ammunition kits.

   Scattered through every sector, keyed off the level's own collider
   list so none of them end up inside a wall or a crate. Walk over one
   to take it; they re-form after a while so a long fight cannot dry
   you out completely, and they refuse to be spent when your reserve is
   already full.

   Like everything else in a firefight, they are pooled: the meshes are
   built once and hidden, never created or destroyed mid-mission.
   ============================================================ */
(function (SF) {
  'use strict';

  const RESPAWN = 32;        // seconds before a spent kit re-forms
  const REACH = 1.5;         // metres — generous, this is a walk-over pickup

  function create(ctx) {
    const { scene, level, lights, hud } = ctx;

    /* ---------- where they go ---------- */
    function blocked(x, z, pad) {
      for (const c of level.colliders) {
        if (c.top < 0.5) continue;
        if (x > c.min.x - pad && x < c.max.x + pad &&
            z > c.min.z - pad && z < c.max.z + pad) return true;
      }
      return false;
    }

    /* A few per sector, spread out, tucked away from the centre line so
       they read as supplies left against a wall rather than dropped loot. */
    function placements() {
      const out = [];
      for (const id of Object.keys(level.zones)) {
        const z = level.zones[id];
        const area = (z.x1 - z.x0) * (z.z1 - z.z0);
        const want = Math.max(2, Math.min(5, Math.round(area / 150)));
        let tries = 0;
        let placed = 0;
        while (placed < want && tries < 150) {
          tries++;
          const x = z.x0 + 1.6 + Math.random() * (z.x1 - z.x0 - 3.2);
          const zz = z.z0 + 1.6 + Math.random() * (z.z1 - z.z0 - 3.2);
          if (blocked(x, zz, 0.9)) continue;
          // keep them apart so they do not clump
          if (out.some((p) => Math.hypot(p.x - x, p.z - zz) < 6)) continue;
          out.push({ x: x, z: zz, zone: id });
          placed++;
        }
      }
      return out;
    }

    /* ---------- build ---------- */
    const shell = new THREE.BoxGeometry(0.44, 0.3, 0.32);
    const band = new THREE.BoxGeometry(0.46, 0.075, 0.34);
    const caseMat = new THREE.MeshStandardMaterial({
      color: 0x3c4450, metalness: 0.85, roughness: 0.45
    });
    const glowMat = new THREE.MeshStandardMaterial({
      color: 0x0a0d12, emissive: new THREE.Color(0xffb454), emissiveIntensity: 2.6,
      metalness: 0.3, roughness: 0.4
    });

    const kits = placements().map((p) => {
      const g = new THREE.Group();
      const body = new THREE.Mesh(shell, caseMat);
      const stripe = new THREE.Mesh(band, glowMat);
      stripe.position.y = 0.055;
      body.castShadow = true;
      g.add(body, stripe);
      g.position.set(p.x, 0.75, p.z);
      scene.add(g);

      // low and short-ranged, so a kit only lights its own corner
      const halo = lights.attach(0xffb454, 0.85, 4.5, { dynamic: false });
      halo.set(p.x, 0.9, p.z);

      return { group: g, halo, x: p.x, z: p.z, zone: p.zone, taken: 0, spin: Math.random() * 6.28 };
    });

    /* ---------- per-frame ---------- */
    function update(dt, playerPos, weapon) {
      for (const k of kits) {
        if (k.taken > 0) {
          k.taken -= dt;
          if (k.taken <= 0) {                       // re-form
            k.group.visible = true;
            k.halo.setIntensity(0.85);
            k.group.scale.setScalar(1);
          }
          continue;
        }

        k.spin += dt * 1.1;
        k.group.rotation.y = k.spin;
        k.group.position.y = 0.75 + Math.sin(k.spin * 1.6) * 0.07;
        k.halo.set(k.x, k.group.position.y + 0.15, k.z);

        const dx = playerPos.x - k.x, dz = playerPos.z - k.z;
        const dy = playerPos.y - (k.group.position.y - 0.75);
        if (dx * dx + dz * dz > REACH * REACH || Math.abs(dy) > 2.4) continue;

        // full reserve? leave it where it is for later
        if (weapon.state.reserve >= weapon.spec.reserve) {
          hud.pickup('AMMO FULL');
          continue;
        }

        const rounds = weapon.spec.mag * 2;
        weapon.addReserve(rounds);
        k.taken = RESPAWN;
        k.group.visible = false;
        k.halo.setIntensity(0);
        hud.pickup('+' + rounds + ' ROUNDS');
        SF.audio.sfx.pickup();
      }
    }

    function destroy() {
      for (const k of kits) { k.halo.release(); scene.remove(k.group); }
      kits.length = 0;
    }

    return { kits, update, destroy, get count() { return kits.length; } };
  }

  SF.pickups = { create, RESPAWN };
})(window.SF);
