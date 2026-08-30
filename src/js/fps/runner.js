/* ============================================================
   Runners — the transit frames you ride across open ground.

   Two families that handle nothing alike. A COURSER is a single-rail
   speed frame: enormous top end, turns like an argument. A SKIFF is a
   flat plate that hovers a hand off the deck: slower flat out, but it
   goes where you point it and it stops when you ask.

   Mounting drops the camera back behind you, because at this speed you
   need to see the frame and what it is about to hit. You keep your
   weapon — firing from the saddle is wide, but it is allowed. Hold
   shift to burn the boost cell; it empties fast and fills back slowly.
   A solid hit still throws you off.

   Parts fitted at the bench land here through gear.runnerStats, so a
   frame you have worked on rides differently from the one you found.
   ============================================================ */
(function (SF) {
  'use strict';

  const SUMMON_TIME = 0.9;         // it has to arrive before you can ride it
  const CHASE_DIST = 4.6;          // how far back the camera sits, riding
  const CHASE_LIFT = 0.8;          // and how far above the rider's head
  const BOOST_DRAIN = 0.42;        // a full cell is a little over two seconds
  const BOOST_FILL = 0.22;         // and about four and a half to refill
  const BOOST_RESET = 0.85;        // burn it dry and it wants most of a charge back
  const CRASH_SPEED = 11;          // how fast you have to be going for a wall to count

  function create(ctx) {
    const { scene, camera, player, hud, lights, character } = ctx;

    const stats = SF.gear.runnerStats(character);
    const state = {
      mounted: false, summoning: 0, speed: 0, last: 0, lean: 0,
      cell: 1, boosting: false, wantBoost: false, locked: false, available: !!stats
    };
    if (!stats) {
      return { update() {}, toggle() {}, destroy() {}, onHit() {}, dismount() {},
               setBoost() {}, shotOrigin() { return null; },
               get mounted() { return false; }, get available() { return false; },
               get stats() { return null; } };
    }

    const isSkiff = stats.family === 'skiff';
    const rarity = SF.gear.rarityOf(character.equipped.runner.rarity);
    const tint = new THREE.Color(rarity.colour);

    /* ---------- the frame and its rider, in the world ---------- */
    /* Third person means both are real objects standing on the ground, not
       a viewmodel hung off the camera. The group carries the player's yaw;
       the rider leans with it. */
    const group = new THREE.Group();
    group.visible = false;
    scene.add(group);

    /* Read at four metres in daylight, not just under a muzzle flash: a
       near-black chrome frame is a silhouette from behind and nothing else. */
    const shell = new THREE.MeshStandardMaterial({
      color: 0x6d7684, metalness: 0.55, roughness: 0.42
    });
    const trim = new THREE.MeshStandardMaterial({
      color: 0x0a0d12, emissive: tint, emissiveIntensity: 1.9,
      metalness: 0.5, roughness: 0.4
    });

    const chassis = new THREE.Group();
    group.add(chassis);

    if (isSkiff) {
      // a plate, wide and low, with a lip at the nose and lit rails
      const deck = new THREE.Mesh(new THREE.BoxGeometry(1.05, 0.12, 2.6), shell);
      deck.position.y = 0.42;
      const lip = new THREE.Mesh(new THREE.BoxGeometry(1.0, 0.07, 0.3), trim);
      lip.position.set(0, 0.5, -1.36);
      chassis.add(deck, lip);
      for (const side of [-1, 1]) {
        const rail = new THREE.Mesh(new THREE.BoxGeometry(0.07, 0.06, 2.4), trim);
        rail.position.set(side * 0.5, 0.49, 0);
        chassis.add(rail);
      }
    } else {
      // a single rail: spine, cowl, bars, and a lit strip down the flank
      const spine = new THREE.Mesh(new THREE.BoxGeometry(0.34, 0.3, 2.9), shell);
      spine.position.y = 0.6;
      const cowl = new THREE.Mesh(new THREE.ConeGeometry(0.28, 1.0, 8), shell);
      cowl.rotation.x = -Math.PI / 2;
      cowl.position.set(0, 0.62, -1.75);
      const bars = new THREE.Mesh(new THREE.BoxGeometry(0.95, 0.08, 0.08), shell);
      bars.position.set(0, 0.92, -1.0);
      const strip = new THREE.Mesh(new THREE.BoxGeometry(0.2, 0.08, 2.1), trim);
      strip.position.set(0, 0.78, 0.1);
      chassis.add(spine, cowl, bars, strip);
      // outriggers, so the frame has a width to read from directly behind
      for (const side of [-1, 1]) {
        const fin = new THREE.Mesh(new THREE.BoxGeometry(0.1, 0.06, 1.1), trim);
        fin.position.set(side * 0.42, 0.5, 0.45);
        fin.rotation.z = side * 0.25;
        const stay = new THREE.Mesh(new THREE.BoxGeometry(0.5, 0.07, 0.09), shell);
        stay.position.set(side * 0.26, 0.52, 0.9);
        chassis.add(fin, stay);
      }
    }

    /* The thrust plume out the back, scaled by speed and lit by the boost.
       It sits between the rider and a chase camera, so it stays small and
       thin — an additive cone pointed down the lens washes out the whole
       lower half of the screen. */
    const plume = new THREE.Mesh(
      new THREE.ConeGeometry(isSkiff ? 0.22 : 0.15, 0.9, 8, 1, true),
      new THREE.MeshBasicMaterial({ color: tint, transparent: true, opacity: 0.3,
                                    blending: THREE.AdditiveBlending, depthWrite: false }));
    plume.rotation.x = Math.PI / 2;          // apex trailing, mouth at the vents
    plume.position.set(0, isSkiff ? 0.42 : 0.58, 1.55);
    plume.scale.set(1, 0.25, 1);
    chassis.add(plume);

    /* ---------- the rider ---------- */
    /* Same build as a squadmate's body, so your own operative reads the way
       theirs does — and so your skin, hair and class colour show at last. */
    const rider = new THREE.Group();
    group.add(rider);
    {
      const G = SF.gear;
      const cls = SF.classes.CLASSES[character.cls] || SF.classes.CLASSES.bulwark;
      const look = character.look || G.defaultLook();
      const suit = new THREE.MeshStandardMaterial({
        color: new THREE.Color(cls.accent).multiplyScalar(0.34), roughness: 0.6, metalness: 0.4 });
      const skin = new THREE.MeshStandardMaterial({
        color: G.SKINS[look.skin % G.SKINS.length], roughness: 0.75 });
      const hair = new THREE.MeshStandardMaterial({
        color: G.HAIR_COLOURS[look.hairColour % G.HAIR_COLOURS.length], roughness: 0.85 });

      const seatY = isSkiff ? 0.5 : 0.78;
      const torso = new THREE.Mesh(new THREE.CapsuleGeometry(0.24, 0.4, 4, 10), suit);
      torso.position.set(0, seatY + 0.62, isSkiff ? 0 : 0.1);
      const head = new THREE.Mesh(new THREE.SphereGeometry(0.2, 14, 12), skin);
      head.position.set(0, seatY + 1.06, isSkiff ? 0 : 0.06);
      const cap = new THREE.Mesh(new THREE.SphereGeometry(0.212, 14, 10,
        0, Math.PI * 2, 0, Math.PI * 0.5), hair);
      cap.position.copy(head.position);
      rider.add(torso, head, cap);

      for (const side of [-1, 1]) {
        /* A courser rider is stretched out with both hands on the bars, so
           the arm lies almost flat along -Z; a skiff rider has nothing to
           hold and just drops their arms for balance. */
        const arm = new THREE.Mesh(new THREE.CapsuleGeometry(0.075, 0.42, 3, 6), suit);
        if (isSkiff) {
          arm.position.set(side * 0.3, seatY + 0.6, -0.05);
          arm.rotation.z = side * -0.28;
        } else {
          arm.position.set(side * 0.25, seatY + 0.5, -0.42);
          arm.rotation.x = -2.0;
        }
        const leg = new THREE.Mesh(new THREE.CapsuleGeometry(0.095, 0.4, 3, 6), suit);
        leg.position.set(side * (isSkiff ? 0.24 : 0.2), seatY + 0.02, isSkiff ? 0.4 : 0.46);
        leg.rotation.x = isSkiff ? 0.25 : 0.9;
        rider.add(arm, leg);
      }
      // riding stance: folded over the frame, more so on a courser
      rider.rotation.x = isSkiff ? 0.14 : 0.3;
      for (const c of rider.children) c.castShadow = true;
    }
    for (const c of chassis.children) c.castShadow = true;

    const thruster = lights.attach(tint.getHex(), 0, 14);

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
      state.last = 0;
      group.visible = true;
      player.setSpeedScale(stats.speed);
      player.setTurnScale(isSkiff ? 1 : 0.62);   // a courser fights the corner
      player.setChase(CHASE_DIST, CHASE_LIFT);
      if (ctx.onMount) ctx.onMount(true);
      hud.runner(stats.name, stats.kind);
      hud.runnerCell(state.cell, false);
      SF.audio.sfx.open();
    }

    function dismount(why) {
      if (!state.mounted) return;
      state.mounted = false;
      state.boosting = false;
      group.visible = false;
      thruster.setIntensity(0);
      player.setSpeedScale(ctx.baseSpeedScale || 1);
      player.setTurnScale(1);
      player.setChase(0, 0);
      if (ctx.onMount) ctx.onMount(false);
      hud.runner(null);
      if (why) hud.pickup(why);
      SF.audio.sfx.back();
    }

    /* A hit hard enough throws you off. */
    function onHit(amount) {
      if (state.mounted && amount > 8) dismount('THROWN');
    }

    /* Shift asks for boost; the cell decides whether it gets it. Running the
       cell to empty locks it out until it has recovered a little, so mashing
       shift is worse than spending the charge in one run. */
    function setBoost(on) { state.wantBoost = !!on; }

    /* The muzzle sits on the camera's centre line so the crosshair stays
       honest from the saddle — the camera is behind the rider, but the shot
       still leaves through the middle of the screen. */
    const originV = new THREE.Vector3();
    const fwdV = new THREE.Vector3();
    function shotOrigin() {
      if (!state.mounted) return null;
      camera.getWorldPosition(originV);
      camera.getWorldDirection(fwdV);
      return originV.addScaledVector(fwdV, player.chaseDistance + 0.9);
    }

    function update(dt) {
      if (state.summoning > 0) {
        state.summoning -= dt;
        if (state.summoning <= 0) mount();
        return;
      }
      if (!state.mounted) {
        // the cell recovers whether you are on it or not
        state.cell = Math.min(1, state.cell + BOOST_FILL * dt);
        return;
      }
      if (!canRide()) { dismount('STOWED'); return; }

      /* ---- boost ----
         Running the cell to empty locks it out. Letting go of the key clears
         the lock straight away; holding it down does not, or a held key would
         stutter the burn on and off every time the charge crept over the line. */
      const wants = !!state.wantBoost;
      if (!wants) state.locked = false;
      if (state.boosting) {
        state.cell -= BOOST_DRAIN * dt;
        if (!wants || state.cell <= 0) {
          state.boosting = false;
          state.cell = Math.max(0, state.cell);
          if (state.cell <= 0) { state.locked = true; hud.pickup('CELL DRY'); }
        }
      } else {
        state.cell = Math.min(1, state.cell + BOOST_FILL * dt);
        if (state.locked && state.cell >= BOOST_RESET) state.locked = false;
        if (wants && !state.locked && state.cell > 0.05) {
          state.boosting = true;
          SF.audio.sfx.phase();
        }
      }
      player.setSpeedScale(stats.speed * (state.boosting ? stats.boost : 1));
      hud.runnerCell(state.cell, state.boosting);

      /* ---- the frame follows the player, one step behind ---- */
      const moving = Math.hypot(player.state.vel.x, player.state.vel.z);

      /* Hitting something at speed throws you over the bars. Without this a
         rock just pins you in place with the throttle open, which reads as
         the controls having stopped working.

         It has to be an impact and not a stop: the test is a large drop
         between one frame and the next while the throttle is still open.
         Letting go of the key sheds speed just as fast, and that is braking,
         not a crash. */
      if (state.last > CRASH_SPEED && moving < state.last * 0.45 &&
          player.state.moveInput > 0) {
        const force = state.last;
        state.last = 0;
        dismount('CRASHED');
        player.state.shake = Math.max(player.state.shake, 2.4);
        if (ctx.onCrash) ctx.onCrash(Math.min(26, force * 0.9));
        return;
      }
      state.last = moving;
      state.speed += (moving - state.speed) * Math.min(1, stats.accel * 0.4 * dt);

      group.position.set(player.position.x, player.position.y, player.position.z);
      group.rotation.y = player.state.yaw;   // both the model and the player face -Z

      // lean into the turn and squat under the burn
      const turnRate = player.state.turnRate || 0;
      state.lean += (-turnRate * (isSkiff ? 0.5 : 0.32) - state.lean) * Math.min(1, 8 * dt);
      chassis.rotation.z = state.lean;
      rider.rotation.z = state.lean * 0.7;
      const hover = Math.sin(performance.now() / 260) * 0.02;
      chassis.position.y = hover - (state.boosting ? 0.05 : 0);
      rider.position.y = hover;
      rider.rotation.x = (isSkiff ? 0.14 : 0.3) + (state.boosting ? 0.16 : 0)
                       + Math.min(0.12, state.speed * 0.004);

      // the plume grows with speed and flares on the boost
      const heat = Math.min(1, state.speed / 16) + (state.boosting ? 0.7 : 0);
      plume.scale.set(1 + heat * 0.25, 0.25 + heat * 0.85, 1 + heat * 0.25);
      plume.material.opacity = 0.16 + heat * 0.2;

      thruster.set(player.position.x, player.position.y + 0.6, player.position.z);
      thruster.setIntensity(0.9 + heat * 2.6);

      hud.runnerSpeed(Math.round(moving * 3.6));   // km/h reads better than m/s
    }

    function destroy() {
      dismount(null);
      thruster.release();
      scene.remove(group);
      group.traverse((o) => {
        if (o.geometry) o.geometry.dispose();
        if (o.material) o.material.dispose();
      });
    }

    return {
      update, toggle, destroy, onHit, dismount, setBoost, shotOrigin,
      get mounted() { return state.mounted; },
      get boosting() { return state.boosting; },
      get cell() { return state.cell; },
      get available() { return true; },
      get stats() { return stats; }
    };
  }

  SF.runner = { create };
})(window.SF);
