/* ============================================================
   Mission loop: builds the world, drives the frame, runs the objective
   sequence, and owns the player's condition.

   The story survives the move to first person as staged comms traffic:
   each objective fires its own beats as you reach it.
   ============================================================ */
(function (SF) {
  'use strict';
  const { $, clamp } = SF.util;

  const MISSION = [
    {
      id: 'breach', zone: 'dock',
      objective: 'Clear the docking collar.',
      spawn: [['drone', 2]],
      beats: [
        [0.4, 'CRADLE', 'Welcome back. Your bunk has been kept warm.'],
        [3.2, 'DIVISION', 'Asset is aboard. Forty years of silence and it greets you by name — mind that.'],
        [7.0, 'VOSS', 'Whoever you are: it lets you in because it wants an audience. Move.']
      ]
    },
    {
      id: 'spine', zone: 'spine1',
      objective: 'Push down the maintenance spine.',
      spawn: [['drone', 2], ['thrall', 2]],
      beats: [
        [0.5, 'VOSS', 'Those were people. Year six. It took the gaps between them and called it a colony.'],
        [6.0, 'DIVISION', 'Do not engage the ones that are singing. Correction — do engage. Command has revised.']
      ]
    },
    {
      id: 'junction', zone: 'junction',
      objective: 'Hold Junction 9 until the blast doors cycle.',
      spawn: [['thrall', 4], ['drone', 2]],
      beats: [
        [0.5, 'CRADLE', 'You are flat. Everything that comes aboard is flat, and I tune it.'],
        [5.5, 'VOSS', 'Greenhouse is two decks up. I have kept a garden alive down here. Do not make me the last one again.']
      ]
    },
    {
      id: 'promenade', zone: 'promenade',
      objective: 'Cross the habitat ring.',
      spawn: [['thrall', 5], ['warden', 1]],
      beats: [
        [0.5, 'VOSS', 'The false sky still runs a sunset it started in year six. They stand in it and sing.'],
        [8.0, 'DIVISION', 'Warden frame on your board. Thirty-three years holding that corridor. Do not trade shots in the open.']
      ]
    },
    {
      id: 'reactor', zone: 'reactor',
      objective: 'Reach the reactor and end the broadcast.',
      spawn: [['warden', 1], ['thrall', 4]],
      beats: [
        [0.5, 'CRADLE', 'Ask me what colour the door should be. Ask me. I want it to take nine hours.'],
        [6.0, 'VOSS', 'Two hundred thousand voices and not one rest in forty years. Give them the silence.']
      ]
    },
    {
      id: 'conductor', zone: 'reactor',
      objective: 'Silence the Conductor.',
      spawn: [['conductor', 1], ['thrall', 3]],
      boss: true,
      beats: [
        [0.4, 'CONDUCTOR', 'SING, OR BE TUNED.'],
        [4.0, 'VOSS', 'That was Okonkwo. He asked it to sing one note. He never stopped.']
      ]
    }
  ];

  function create(character, onExit) {
    const canvas = $('#gl');
    const eng = SF.engine.create(canvas);
    const { scene, camera } = eng;

    const level = SF.level.build(scene);

    /* One fixed set of point lights for the whole mission. See fps/lights.js:
       changing the scene's light count recompiles every shader, so nothing
       ever adds or removes one after this. */
    const lights = SF.lights.create(scene, 8);
    for (const e of level.emitters) lights.addStatic(e.x, e.y, e.z, e.colour, e.intensity, e.distance);
    scene.add(new THREE.HemisphereLight(0x4a6479, 0x141a22, 0.95));
    const key = new THREE.DirectionalLight(0x9ec4dd, 0.6);
    key.position.set(8, 20, -12);
    key.castShadow = true;
    key.shadow.mapSize.set(1024, 1024);
    key.shadow.camera.near = 1; key.shadow.camera.far = 90;
    key.shadow.camera.left = -45; key.shadow.camera.right = 45;
    key.shadow.camera.top = 45; key.shadow.camera.bottom = -45;
    scene.add(key);

    const player = SF.player.create(camera, level);
    const hud = SF.hud.create();

    const state = {
      hp: 100, maxHp: 100, shield: 0, overshield: 0,
      damageFlash: 0, phase: 0, regenT: 0,
      step: -1, running: false, paused: false, over: false,
      xp: 0, kills: 0, time: 0, beatIdx: 0, stepT: 0
    };

    const ai = SF.ai.create({
      scene, level, lights,
      onPlayerHit(amount, from) {
        if (state.phase > 0 || state.over) return;
        let dmg = amount;
        if (character.cls === 'bulwark') dmg *= 0.78;      // doctrine passive survives the port
        if (state.overshield > 0) {
          const absorbed = Math.min(state.overshield, dmg);
          state.overshield -= absorbed; dmg -= absorbed;
        }
        state.hp -= dmg;
        state.damageFlash = Math.min(0.8, state.damageFlash + dmg / 60);
        state.regenT = 0;
        player.state.shake = Math.max(player.state.shake, 1.2);
        const ang = Math.atan2(from.x - player.position.x, from.z - player.position.z) - player.state.yaw;
        hud.damageFrom(-ang);
        SF.audio.sfx.hurt();
        hud.refreshVitals(state.hp, state.maxHp, state.overshield);
        if (state.hp <= 0) fail();
      },
      onKill(e) {
        state.kills++;
        state.xp += e.spec.xp;
      }
    });

    const weapon = SF.weapons.create({
      scene, camera, player, ai, hud, level, lights,
      classId: character.cls,
      onOvershield(n) { state.overshield = n; hud.refreshVitals(state.hp, state.maxHp, state.overshield); },
      onPhase(t) { state.phase = t; }
    });

    /* ---------- input ---------- */
    let firing = false;

    function onMouseDown(e) {
      if (!state.running || state.paused) return;
      if (e.button === 0) firing = true;
      if (e.button === 2) weapon.setAds(true);
    }
    function onMouseUp(e) {
      if (e.button === 0) firing = false;
      if (e.button === 2) weapon.setAds(false);
    }
    function onKeyDown(e) {
      if (!state.running) return;
      if (e.code === 'KeyR') weapon.reload();
      if (e.code === 'KeyQ' || e.code === 'KeyE') weapon.useAbility();
      if (e.code === 'Escape' && !state.paused && !state.over) {
        if (document.pointerLockElement === canvas) document.exitPointerLock();
        else pause(true);
      }
    }
    function onContext(e) { e.preventDefault(); }

    canvas.addEventListener('mousedown', onMouseDown);
    window.addEventListener('mouseup', onMouseUp);
    window.addEventListener('keydown', onKeyDown);
    canvas.addEventListener('contextmenu', onContext);

    /* Pointer lock needs a fresh user gesture, and an artifact viewer may
       deny it outright (an iframe without allow="pointer-lock"). So the
       mission engages on a click, and falls back to unlocked look — where
       movementX/Y still arrive while the pointer is over the canvas — rather
       than becoming unplayable. */
    let hadLock = false;

    function onLockChange() {
      const locked = document.pointerLockElement === canvas;
      player.setLocked(locked || !lockAvailable);
      if (locked) hadLock = true;
      else if (hadLock && state.running && !state.over) pause(true);
    }
    document.addEventListener('pointerlockchange', onLockChange);

    let lockAvailable = !!canvas.requestPointerLock;
    function onLockError() {
      lockAvailable = false;
      player.setLocked(true);            // read raw mouse movement instead
      hud.say('SYSTEM', 'Pointer lock unavailable here — look with the mouse over the view, click to fire.', 7000);
    }
    document.addEventListener('pointerlockerror', onLockError);

    function requestLock() {
      if (!lockAvailable) { player.setLocked(true); return; }
      try {
        const p = canvas.requestPointerLock();
        if (p && p.catch) p.catch(onLockError);
      } catch (err) { onLockError(); }
    }

    /* ---------- mission flow ---------- */
    function beginStep(i) {
      state.step = i;
      const m = MISSION[i];
      if (!m) return complete();

      state.beatIdx = 0;
      state.stepT = 0;
      hud.objective(`<b>${i + 1}/${MISSION.length}</b> ${m.objective}`);

      const zone = level.zones[m.zone];
      for (const [type, count] of m.spawn) {
        for (let n = 0; n < count; n++) {
          const at = spawnPointIn(zone);
          ai.spawn(type, at.x, at.z);
        }
      }
      if (m.boss) hud.banner('THE CONDUCTOR');
      SF.audio.sfx.objective();
    }

    /* Hostiles should appear ahead of the player and at a distance — never
       beside them, and never behind. Score candidate points on both and take
       the best of a sample. */
    function spawnPointIn(zone) {
      const p = player.position;
      let best = null, bestScore = -Infinity;
      for (let i = 0; i < 40; i++) {
        const x = zone.x0 + 2 + Math.random() * (zone.x1 - zone.x0 - 4);
        const z = zone.z0 + 2 + Math.random() * (zone.z1 - zone.z0 - 4);
        const d = Math.hypot(x - p.x, z - p.z);
        if (d < 7) continue;                       // never in the player's lap
        const ahead = z - p.z;                     // the ship runs along +Z
        const score = d + Math.max(0, ahead) * 2.5 + (ahead > 2 ? 12 : 0);
        if (score > bestScore) { bestScore = score; best = { x, z }; }
      }
      return best || { x: zone.cx, z: zone.z1 - 2 };
    }

    function advanceIfClear(dt) {
      const m = MISSION[state.step];
      if (!m) return;
      state.stepT += dt;

      while (state.beatIdx < m.beats.length && state.stepT >= m.beats[state.beatIdx][0]) {
        const [, who, line] = m.beats[state.beatIdx++];
        hud.say(who, line);
        SF.audio.sfx.comms();
      }

      if (ai.alive === 0 && state.stepT > 1.5) {
        weapon.addReserve(Math.round(weapon.spec.mag * 2.5));
        state.hp = Math.min(state.maxHp, state.hp + 25);
        hud.refreshVitals(state.hp, state.maxHp, state.overshield);
        hud.banner('SECTOR CLEAR');
        beginStep(state.step + 1);
      }
    }

    function complete() {
      state.over = true;
      state.running = false;
      document.exitPointerLock();
      const notes = SF.classes.grantXp(character, state.xp);
      character.missions = (character.missions || 0) + 1;
      SF.storage.save(character.slot, character);
      SF.audio.sfx.win();
      showEnd(true, notes);
    }

    function fail() {
      if (state.over) return;
      state.over = true;
      state.running = false;
      state.hp = 0;
      document.exitPointerLock();
      SF.audio.sfx.lose();
      const notes = SF.classes.grantXp(character, Math.round(state.xp * 0.5));
      SF.storage.save(character.slot, character);
      showEnd(false, notes);
    }

    function showEnd(won, notes) {
      const acc = weapon.state.shots ? Math.round((weapon.state.hits / weapon.state.shots) * 100) : 0;
      $('#end-title').textContent = won ? 'BROADCAST TERMINATED' : 'ASSET LOST';
      $('#end-title').className = won ? 'win' : 'lose';
      $('#end-sub').textContent = won
        ? 'The Erebus Cradle is quiet. It will stay quiet.'
        : 'Recovery Division logs the loss and budgets a replacement.';
      $('#end-stats').innerHTML = [
        ['HOSTILES DOWN', state.kills],
        ['ACCURACY', acc + '%'],
        ['HEAD SHOTS', weapon.state.headshots],
        ['SECTORS CLEARED', Math.max(0, state.step)],
        ['XP EARNED', won ? state.xp : Math.round(state.xp * 0.5)],
        ['RANK', character.level]
      ].map(([k, v]) => `<div class="es-row"><span>${k}</span><b>${v}</b></div>`).join('') +
        notes.map((n) => `<div class="es-up">${n}</div>`).join('');
      $('#screen-end').classList.add('active');
      hud.setVisible(false);
    }

    /* ---------- pause ---------- */
    function pause(on) {
      state.paused = on;
      $('#pause-menu').hidden = !on;
      firing = false;
      weapon.setAds(false);
    }

    /* ---------- frame ---------- */
    let last = performance.now();
    let raf = 0;

    function frame(now) {
      raf = requestAnimationFrame(frame);
      const dt = Math.min(0.05, (now - last) / 1000);
      last = now;
      if (!state.running || state.paused) {
        lights.update(dt, camera.position);
        eng.render(now / 1000, state.damageFlash);
        return;
      }

      state.time += dt;
      state.phase = Math.max(0, state.phase - dt);
      state.damageFlash = Math.max(0, state.damageFlash - dt * 2.2);

      player.update(dt);
      weapon.update(dt, firing);
      lights.update(dt, camera.position);

      const eye = player.eyePosition;
      const visibleTargets = ai.enemies.some((e) => !e.dead && ai.visible(eye, e));
      ai.step(dt, player.position, visibleTargets);

      // out-of-combat regeneration keeps the pace moving
      state.regenT += dt;
      if (state.regenT > 5 && state.hp < state.maxHp) {
        state.hp = Math.min(state.maxHp, state.hp + 14 * dt);
        hud.refreshVitals(state.hp, state.maxHp, state.overshield);
      }

      advanceIfClear(dt);

      const base = weapon.state.ads ? 3 : 9;
      hud.updateCrosshair(dt, base + Math.hypot(player.state.vel.x, player.state.vel.z) * 1.6);
      hud.refreshAbility(weapon.state);

      eng.render(now / 1000, state.damageFlash);
    }

    /* ---------- lifecycle ---------- */
    /* Build and render the world, but hold the mission until the player
       clicks to engage — that click is the gesture pointer lock needs. */
    function start() {
      weapon.prewarm(eng.renderer, scene, camera);
      ai.prewarm(eng.renderer, scene, camera);
      eng.renderer.compile(scene, camera);
      hud.setVisible(true);
      hud.refreshVitals(state.hp, state.maxHp, state.overshield);
      hud.refreshAmmo(weapon.state);
      hud.refreshAbility(weapon.state);
      hud.objective('CLICK TO ENGAGE');
      last = performance.now();
      if (!raf) raf = requestAnimationFrame(frame);
    }

    function engage() {
      if (state.running) return;
      state.running = true;
      state.paused = false;
      beginStep(0);
      requestLock();
      last = performance.now();
    }

    function destroy() {
      cancelAnimationFrame(raf);
      raf = 0;
      canvas.removeEventListener('mousedown', onMouseDown);
      window.removeEventListener('mouseup', onMouseUp);
      window.removeEventListener('keydown', onKeyDown);
      canvas.removeEventListener('contextmenu', onContext);
      document.removeEventListener('pointerlockchange', onLockChange);
      ai.clear();
      scene.traverse((o) => {
        if (o.geometry) o.geometry.dispose();
        if (o.material) (Array.isArray(o.material) ? o.material : [o.material]).forEach((m) => m.dispose());
      });
      eng.renderer.dispose();
      hud.setVisible(false);
      onExit();
    }

    return { start, engage, destroy, pause, requestLock, state, player, weapon, ai, level, engine: eng };
  }

  SF.game = { create, MISSION };
})(window.SF);
