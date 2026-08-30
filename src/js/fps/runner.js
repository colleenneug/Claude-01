/* ============================================================
   Runners — the transit frames you ride across open ground.

   Two families that handle nothing alike. A COURSER is a single-rail
   speed frame: enormous top end, turns like an argument. A SKIFF is a
   flat plate that hovers a hand off the deck: slower flat out, but it
   goes where you point it and it stops when you ask.

   Mounted you cannot shoot, and a solid hit throws you off. The frame
   itself is drawn in first person, hanging off the camera the way the
   weapon does.
   ============================================================ */
(function (SF) {
  'use strict';

  const SUMMON_TIME = 0.9;         // it has to arrive before you can ride it

  function create(ctx) {
    const { camera, player, hud, lights, character } = ctx;

    const stats = SF.gear.runnerStats(character);
    const state = { mounted: false, summoning: 0, speed: 0, lean: 0, boost: 0, available: !!stats };
    if (!stats) return { update() {}, toggle() {}, destroy() {}, get mounted() { return false; },
                         get available() { return false; } };

    const isSkiff = stats.family === 'skiff';
    const rarity = SF.gear.rarityOf(character.equipped.runner.rarity);

    /* ---------- the frame, drawn in first person ---------- */
    const view = new THREE.Group();
    camera.add(view);

    const shell = new THREE.MeshStandardMaterial({
      color: 0x2b3038, metalness: 0.9, roughness: 0.35
    });
    const trim = new THREE.MeshStandardMaterial({
      color: 0x0a0d12, emissive: new THREE.Color(rarity.colour), emissiveIntensity: 1.9,
      metalness: 0.5, roughness: 0.4
    });

    if (isSkiff) {
      const deck = new THREE.Mesh(new THREE.BoxGeometry(1.5, 0.09, 2.4), shell);
      deck.position.set(0, -1.15, -0.35);
      const lip = new THREE.Mesh(new THREE.BoxGeometry(1.55, 0.05, 0.16), trim);
      lip.position.set(0, -1.1, -1.5);
      const rail = new THREE.Mesh(new THREE.BoxGeometry(0.08, 0.05, 2.3), trim);
      rail.position.set(0.72, -1.1, -0.35);
      const rail2 = rail.clone();
      rail2.position.x = -0.72;
      view.add(deck, lip, rail, rail2);
    } else {
      const spine = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.22, 2.6), shell);
      spine.position.set(0, -0.95, -0.9);
      const nose = new THREE.Mesh(new THREE.ConeGeometry(0.26, 0.9, 8), shell);
      nose.rotation.x = -Math.PI / 2;
      nose.position.set(0, -0.92, -2.3);
      const bars = new THREE.Mesh(new THREE.BoxGeometry(1.15, 0.07, 0.07), shell);
      bars.position.set(0, -0.62, -1.15);
      const glow = new THREE.Mesh(new THREE.BoxGeometry(0.2, 0.07, 1.9), trim);
      glow.position.set(0, -0.84, -0.9);
      view.add(spine, nose, bars, glow);
    }
    view.visible = false;

    const thruster = lights.attach(new THREE.Color(rarity.colour).getHex(), 0, 12);

    /* ---------- mounting ---------- */
    function canRide() {
      return !!ctx.isOpenZone;               // only out on the ground, not in the ship
    }

    function toggle() {
      if (!canRide()) { hud.pickup('NO ROOM TO RIDE HERE'); return; }
      if (state.mounted) { dismount('STOWED'); return; }
      if (state.summoning > 0) return;
      state.summoning = SUMMON_TIME;
      hud.pickup('SUMMONING ' + stats.name);
      SF.audio.sfx.phase();
    }

    function mount() {
      state.mounted = true;
      state.speed = 0;
      view.visible = true;
      player.setSpeedScale(stats.speed);
      player.setTurnScale(isSkiff ? 1 : 0.62);   // a courser fights the corner
      hud.runner(stats.name, stats.kind);
      SF.audio.sfx.open();
    }

    function dismount(why) {
      if (!state.mounted) return;
      state.mounted = false;
      view.visible = false;
      thruster.setIntensity(0);
      player.setSpeedScale(ctx.baseSpeedScale || 1);
      player.setTurnScale(1);
      hud.runner(null);
      if (why) hud.pickup(why);
      SF.audio.sfx.back();
    }

    /* A hit hard enough throws you off. */
    function onHit(amount) {
      if (state.mounted && amount > 8) dismount('THROWN');
    }

    function update(dt) {
      if (state.summoning > 0) {
        state.summoning -= dt;
        if (state.summoning <= 0) mount();
        return;
      }
      if (!state.mounted) return;

      if (!canRide()) { dismount('STOWED'); return; }

      const moving = Math.hypot(player.state.vel.x, player.state.vel.z);
      state.speed += (moving - state.speed) * Math.min(1, stats.accel * 0.4 * dt);

      // the frame leans into the turn and dips under acceleration
      const turnRate = player.state.turnRate || 0;
      state.lean += (-turnRate * (isSkiff ? 0.5 : 0.32) - state.lean) * Math.min(1, 8 * dt);
      view.rotation.z = state.lean;
      view.position.y = Math.sin(performance.now() / 260) * 0.02 - state.speed * 0.002;

      const world = new THREE.Vector3();
      camera.getWorldPosition(world);
      thruster.set(world.x, world.y - 1, world.z);
      thruster.setIntensity(1.2 + Math.min(2.2, state.speed * 0.12));

      hud.runnerSpeed(Math.round(moving * 3.6));   // km/h reads better than m/s
    }

    function destroy() {
      dismount(null);
      thruster.release();
      camera.remove(view);
      view.traverse((o) => {
        if (o.geometry) o.geometry.dispose();
        if (o.material) o.material.dispose();
      });
    }

    return {
      update, toggle, destroy, onHit, dismount,
      get mounted() { return state.mounted; },
      get available() { return true; },
      get stats() { return stats; }
    };
  }

  SF.runner = { create };
})(window.SF);
