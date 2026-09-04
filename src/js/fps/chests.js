/* ============================================================
   Cached salvage.

   Crates left across a patrol zone by whoever was here before. Most
   are grave caches, buried and never collected; a few are sealed
   survey vaults worth going out of your way for. Walk up and hold F.

   Opened crates stay open for the session — this is loot you find, so
   finding it again would be worthless.
   ============================================================ */
(function (SF) {
  'use strict';

  const REACH = 3.2;
  const OPEN_TIME = 1.1;

  const KINDS = {
    cache: { name: 'GRAVE CACHE', colour: 0x7de3a8, tierBonus: 0, drops: 1, weight: 0.78 },
    vault: { name: 'SURVEY VAULT',   colour: 0xb98cff, tierBonus: 5, drops: 2, weight: 0.22 }
  };

  function create(ctx) {
    const { scene, level, lights, hud, player } = ctx;
    if (!level.radius) return { update() {}, destroy() {}, get count() { return 0; } };

    const R = level.radius;

    function blocked(x, z, pad) {
      const list = level.space ? level.space.near(x, z, pad + 1) : level.colliders;
      for (const c of list) {
        if (c.top < 0.5) continue;
        if (x > c.min.x - pad && x < c.max.x + pad &&
            z > c.min.z - pad && z < c.max.z + pad) return true;
      }
      return false;
    }

    /* ---------- placement ---------- */
    const bodyGeo = new THREE.BoxGeometry(1.15, 0.78, 0.85);
    const lidGeo = new THREE.BoxGeometry(1.2, 0.14, 0.9);
    const shell = new THREE.MeshStandardMaterial({ color: 0x39424e, metalness: 0.8, roughness: 0.45 });

    const chests = [];
    const want = Math.min(40, Math.round((Math.PI * R * R) / 42000));

    for (let i = 0, tries = 0; i < want && tries < want * 60; tries++) {
      const ang = Math.random() * Math.PI * 2;
      const dist = 40 + Math.sqrt(Math.random()) * (R - 60);
      const x = Math.cos(ang) * dist, z = Math.sin(ang) * dist;
      if (blocked(x, z, 1.6)) continue;
      if (chests.some((c) => Math.hypot(c.x - x, c.z - z) < 70)) continue;

      const kind = Math.random() < KINDS.vault.weight ? KINDS.vault : KINDS.cache;
      const g = new THREE.Group();
      const body = new THREE.Mesh(bodyGeo, shell);
      body.position.y = 0.39;
      const lid = new THREE.Mesh(lidGeo, new THREE.MeshStandardMaterial({
        color: 0x0a0d12, emissive: new THREE.Color(kind.colour), emissiveIntensity: 2.2,
        metalness: 0.4, roughness: 0.4
      }));
      lid.position.y = 0.82;
      body.castShadow = lid.castShadow = true;
      g.add(body, lid);
      g.position.set(x, 0, z);
      g.rotation.y = Math.random() * 6.28;
      scene.add(g);

      /* A beam so a crate reads from a distance — this is a big zone and a
         crate is a small thing in it. */
      const beam = new THREE.Mesh(
        new THREE.CylinderGeometry(0.5, 0.5, 26, 8, 1, true),
        new THREE.MeshBasicMaterial({ color: kind.colour, transparent: true, opacity: 0.14,
                                      blending: THREE.AdditiveBlending, depthWrite: false,
                                      side: THREE.DoubleSide }));
      beam.position.set(x, 13, z);
      scene.add(beam);

      const halo = lights.attach(kind.colour, 1.1, 10, { dynamic: false });
      halo.set(x, 1.2, z);

      chests.push({ group: g, lid, beam, halo, x, z, kind, opened: false, charge: 0 });
      i++;
    }

    /* ---------- interaction ---------- */
    let prompting = false;

    function update(dt, holding) {
      let nearest = null, nd = REACH;
      for (const c of chests) {
        if (c.opened) continue;
        const d = Math.hypot(c.x - player.position.x, c.z - player.position.z);
        if (d < nd) { nd = d; nearest = c; }
        c.beam.material.opacity = 0.10 + Math.sin(performance.now() / 700 + c.x) * 0.04;
      }

      if (!nearest) {
        if (prompting) { hud.pickup(''); prompting = false; }
        for (const c of chests) if (!c.opened) c.charge = 0;
        return;
      }

      prompting = true;
      if (holding) {
        nearest.charge += dt;
        hud.pickup('OPENING ' + nearest.kind.name + ' ' +
                   Math.max(0, OPEN_TIME - nearest.charge).toFixed(1) + 's');
        if (nearest.charge >= OPEN_TIME) open(nearest);
      } else {
        nearest.charge = 0;
        hud.pickup('HOLD F — ' + nearest.kind.name);
      }
    }

    function open(c) {
      c.opened = true;
      c.lid.rotation.x = -1.1;
      c.lid.position.z = -0.42;
      c.lid.position.y = 0.92;
      c.lid.material.emissiveIntensity = 0.25;
      c.beam.visible = false;
      c.halo.setIntensity(0);
      hud.pickup('');
      SF.audio.sfx.pickup();
      if (ctx.onOpen) ctx.onOpen(c.kind, c.kind.drops, c.kind.tierBonus);
    }

    function destroy() {
      for (const c of chests) {
        c.halo.release();
        scene.remove(c.group);
        scene.remove(c.beam);
        c.lid.material.dispose();
        c.beam.geometry.dispose();
        c.beam.material.dispose();
      }
      bodyGeo.dispose();
      lidGeo.dispose();
      shell.dispose();
      chests.length = 0;
    }

    return { update, destroy, chests, get count() { return chests.length; } };
  }

  SF.chests = { create, KINDS };
})(window.SF);
