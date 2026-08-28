/* ============================================================
   Mission loop: builds the world, drives the frame, runs the objective
   sequence, and owns the player's condition.

   The story survives the move to first person as staged comms traffic:
   each objective fires its own beats as you reach it.
   ============================================================ */
(function (SF) {
  'use strict';
  const { $, clamp } = SF.util;

  function create(character, missionIndex, onExit) {
    /* A mission reference is either a campaign index or a destination id. */
    const isPlanet = typeof missionIndex === 'string';
    const mission = isPlanet ? SF.planets.asMission(SF.planets.byId(missionIndex))
                             : SF.campaign.byIndex(missionIndex);
    // destinations scale with the wave reached, not the mission number
    let scale = SF.campaign.scaleFor(isPlanet ? 5 : missionIndex - 1);

    const canvas = $('#gl');
    const eng = SF.engine.create(canvas);
    const { scene, camera } = eng;

    const level = isPlanet ? SF.planets.buildArena(scene, mission.planet)
                           : SF.level.build(scene);
    const missionSpec = mission;

    /* One fixed set of point lights for the whole mission. See fps/lights.js:
       changing the scene's light count recompiles every shader, so nothing
       ever adds or removes one after this. The boss arena is far larger than
       a corridor, so it gets a few more slots. */
    const lights = SF.lights.create(scene, missionSpec.boss ? 12 : 8);
    for (const e of level.emitters) lights.addStatic(e.x, e.y, e.z, e.colour, e.intensity, e.distance);
    const lightSpec = isPlanet ? mission.planet.light
                               : { key: 0x9ec4dd, keyI: 0.6, hemiSky: 0x4a6479,
                                   hemiGround: 0x141a22, hemiI: 0.95 };
    scene.add(new THREE.HemisphereLight(lightSpec.hemiSky, lightSpec.hemiGround, lightSpec.hemiI));
    const key = new THREE.DirectionalLight(lightSpec.key, lightSpec.keyI);
    key.position.set(8, 20, -12);
    key.castShadow = true;
    key.shadow.mapSize.set(1024, 1024);
    key.shadow.camera.near = 1; key.shadow.camera.far = 90;
    key.shadow.camera.left = -45; key.shadow.camera.right = 45;
    key.shadow.camera.top = 45; key.shadow.camera.bottom = -45;
    scene.add(key);

    const player = SF.player.create(camera, level);
    /* Each mission opens where the last one closed: at the near edge of its
       own sector, facing down the ship. */
    (function placeStart() {
      if (isPlanet) {
        player.state.pos.copy(level.playerStart);
        player.state.yaw = Math.PI;
        return;
      }
      const zone = level.zones[mission.zone];
      player.state.pos.set(zone.cx, 0, zone.z0 + 2.5);
      player.state.yaw = Math.PI;
    })();
    const hud = SF.hud.create();

    SF.gear.ensure(character);
    const armour = SF.gear.armourStats(character);

    const state = {
      hp: 100 + armour.hp, maxHp: 100 + armour.hp, shield: 0, overshield: 0,
      damageFlash: 0, phase: 0, regenT: 0,
      running: false, paused: false, over: false,
      xp: 0, kills: 0, time: 0, beatIdx: 0, stepT: 0, drops: null,
      wave: 0, onPad: false, extractT: 0,
      spawned: false, bossBeaten: false,
      respawns: 0, maxRespawns: 0, deaths: 0, dying: false, respawnT: 0
    };
    state.maxRespawns = SF.campaign.respawnsFor(SF.campaign.byIndex(missionIndex));
    state.respawns = state.maxRespawns;
    let boss = null;

    const ai = SF.ai.create({
      scene, level, lights,
      // hostile damage scales with the mission; the boss scales its own
      onPlayerHit(amount, from) { hurtPlayer(amount * scale.damage, from); },
      onKill(e) {
        state.kills++;
        // a destination's reference is an id, not a number — use its tier instead
        const tier = isPlanet ? 6 + state.wave : missionIndex;
        state.xp += Math.round(e.spec.xp * (1 + 0.1 * (tier - 1)));
      }
    });

    const pickups = SF.pickups.create({ scene, level, lights, hud });

    const weapon = SF.weapons.create({
      scene, camera, player, ai, hud, level, lights,
      classId: character.cls,
      mods: SF.gear.weaponMods(character),
      itemName: character.equipped.weapon ? character.equipped.weapon.name : null,
      /* Shots can press the boss's resonance nodes as well as hit enemies. */
      rayNode: (o, d, r) => (boss ? boss.rayNode(o, d, r) : null),
      onNode: (i) => { if (boss) boss.hitNode(i); },
      onOvershield(n) { state.overshield = n; hud.refreshVitals(state.hp, state.maxHp, state.overshield); },
      onPhase(t) { state.phase = t; }
    });

    /* ---------- input ---------- */
    let firing = false;
    let extractHeld = false;

    function onMouseDown(e) {
      if (!state.running || state.paused) return;
      if (e.button === 0) firing = true;
      if (e.button === 2) weapon.setAds(true);
    }
    function onKeyUp(e) { if (e.code === 'KeyF') extractHeld = false; }
    window.addEventListener('keyup', onKeyUp);

    function onMouseUp(e) {
      if (e.button === 0) firing = false;
      if (e.button === 2) weapon.setAds(false);
    }
    function onKeyDown(e) {
      if (!state.running) return;
      if (e.code === 'KeyF') extractHeld = true;
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
      if (locked) { hadLock = true; applyLookMode(); return; }
      // losing a lock we actually held means the player pressed Escape
      if (hadLock && state.running && !state.over) pause(true);
      else applyLookMode();
    }

    /* Locked when we hold the pointer, free-look (with edge steering) when
       lock is unavailable, off while paused or before engaging. */
    function applyLookMode() {
      if (!state.running || state.paused || state.over) { player.setMode('off'); return; }
      player.setMode(document.pointerLockElement === canvas ? 'locked' : 'free');
    }
    document.addEventListener('pointerlockchange', onLockChange);

    let lockAvailable = !!canvas.requestPointerLock;
    function onLockError() {
      lockAvailable = false;
      applyLookMode();
      hud.say('SYSTEM', 'Pointer lock unavailable here — move the mouse to look, hold it near an ' +
                        'edge to keep turning, or turn with the arrow keys.', 8000);
    }
    document.addEventListener('pointerlockerror', onLockError);

    function requestLock() {
      if (!lockAvailable) { applyLookMode(); return; }
      try {
        const p = canvas.requestPointerLock();
        if (p && p.catch) p.catch(onLockError);
      } catch (err) { onLockError(); }
      // if the request is ignored rather than refused, fall back anyway
      setTimeout(() => { if (document.pointerLockElement !== canvas) applyLookMode(); }, 400);
    }

    /* ---------- mission flow ---------- */

    /* Hostiles appear ahead of the player and at a distance, never beside or
       behind, so a sector never opens with a free hit. */
    function spawnPointIn(zone) {
      const p = player.position;
      let best = null, bestScore = -Infinity;
      for (let i = 0; i < 40; i++) {
        const x = zone.x0 + 2 + Math.random() * (zone.x1 - zone.x0 - 4);
        const z = zone.z0 + 2 + Math.random() * (zone.z1 - zone.z0 - 4);
        const d = Math.hypot(x - p.x, z - p.z);
        if (d < 7) continue;
        const ahead = z - p.z;
        const score = d + Math.max(0, ahead) * 2.5 + (ahead > 2 ? 12 : 0);
        if (score > bestScore) { bestScore = score; best = { x, z }; }
      }
      return best || { x: zone.cx, z: zone.z1 - 2 };
    }

    function beginMission() {
      const zone = level.zones[mission.zone];
      if (mission.survival) { state.wave = 0; nextWave(); state.spawned = true; SF.audio.sfx.objective(); return; }
      hud.objective(`<b>${mission.n}/${SF.campaign.LAST}</b> ${mission.objective}`);

      for (const [type, count] of mission.waves) {
        for (let n = 0; n < count; n++) {
          const at = spawnPointIn(zone);
          const e = ai.spawn(type, at.x, at.z);
          e.maxHp = Math.round(e.maxHp * scale.hp);        // later missions are tougher
          e.hp = e.maxHp;
        }
      }

      if (mission.boss) {
        boss = SF.boss.create({
          scene, level, lights, ai, hud, player,
          hpScale: scale.hp, dmgScale: scale.damage,
          onPlayerHit: (amt, from) => hurtPlayer(amt, from),
          onDefeated: () => { if (!state.bossBeaten) { state.bossBeaten = true; complete(); } }
        });
        hud.bossShow('THE CONDUCTOR', 'FIRST VOICE / TWO HUNDRED THOUSAND STRONG');
        hud.bossNodes([0x5eeaff, 0xffb454, 0x7dff9b, 0xff5ea8]);
      }

      state.spawned = true;
      SF.audio.sfx.objective();
    }

    function hurtPlayer(amount, from) {
      if (state.phase > 0 || state.over) return;
      let dmg = amount * (1 - armour.resist / 100);       // armour resistance
      if (character.cls === 'bulwark') dmg *= 0.78;
      if (state.overshield > 0) {
        const absorbed = Math.min(state.overshield, dmg);
        state.overshield -= absorbed; dmg -= absorbed;
      }
      state.hp -= dmg;
      state.damageFlash = Math.min(0.8, state.damageFlash + dmg / 60);
      state.regenT = 0;
      player.state.shake = Math.max(player.state.shake, 1.4);
      if (from) {
        const ang = Math.atan2(from.x - player.position.x, from.z - player.position.z) - player.state.yaw;
        hud.damageFrom(-ang);
      }
      SF.audio.sfx.hurt();
      hud.refreshVitals(state.hp, state.maxHp, state.overshield);
      if (state.hp <= 0) die();
    }

    /* ---------- death and respawn ---------- */

    /* Ordinary sectors let the harness bring you back where you came in.
       The boss fight does not: there, losing your vitals ends the mission. */
    function die() {
      if (state.over || state.dying) return;
      state.deaths++;
      firing = false;
      weapon.setAds(false);

      if (state.respawns <= 0) {
        hud.deathOverlay(true, 0, mission.boss
          ? 'NO HARNESS CHARGE — THE CONDUCTOR TAKES THE FIELD'
          : 'HARNESS SPENT');
        return fail();
      }

      state.respawns--;
      state.dying = true;
      state.respawnT = 2.4;
      state.hp = 0;
      hud.refreshVitals(0, state.maxHp, 0);
      hud.deathOverlay(true, state.respawns, 'TRAUMA HARNESS ENGAGING');
      hud.refreshHarness(state.respawns, state.maxRespawns);
      SF.audio.sfx.lose();
      player.state.shake = 3;
    }

    function respawn() {
      const zone = level.zones[mission.zone];
      state.dying = false;
      state.hp = state.maxHp;
      state.overshield = 0;
      state.damageFlash = 0;
      state.regenT = 0;
      state.phase = 2.6;                      // brief grace period on the way back in
      player.state.pos.set(zone.cx, 0, zone.z0 + 2.5);
      player.state.vel.set(0, 0, 0);
      player.state.yaw = Math.PI;
      player.state.pitch = 0;
      // come back with something in the magazine rather than dry
      weapon.state.ammo = weapon.spec.mag;
      weapon.state.reloading = false;
      weapon.addReserve(weapon.spec.mag * 2);
      hud.deathOverlay(false);
      hud.refreshVitals(state.hp, state.maxHp, 0);
      hud.refreshAmmo(weapon.state);
      hud.banner('BACK ON YOUR FEET');
      SF.audio.sfx.objective();
    }

    /* Destinations run escalating waves rather than a fixed roster. */
    function nextWave() {
      state.wave++;
      scale = SF.campaign.scaleFor(2 + state.wave * 1.35);
      const budget = 3 + Math.round(state.wave * 1.7);
      const mix = mission.planet.mix;
      const zone = level.zones.arena;

      for (let i = 0; i < budget; i++) {
        let roll = Math.random(), type = mix[0][0];
        for (const [t, w] of mix) { roll -= w; if (roll <= 0) { type = t; break; } }
        // ring spawn, well clear of the extraction pad
        const ang = Math.random() * Math.PI * 2;
        const dist = 26 + Math.random() * 26;
        const e = ai.spawn(type, Math.cos(ang) * dist, Math.sin(ang) * dist);
        e.maxHp = Math.round(e.maxHp * scale.hp);
        e.hp = e.maxHp;
        e.alerted = true;
        void zone;
      }
      hud.objective(`<b>WAVE ${state.wave}</b> ${state.wave >= 5
        ? 'Stand on the beacon to extract.' : 'Survive. Extraction opens at wave 5.'}`);
      hud.banner('WAVE ' + state.wave);
      SF.audio.sfx.alarm();
    }

    function survivalTick(dt) {
      if (ai.alive === 0 && state.stepT > 1.2) {
        state.xp += 40 * state.wave;
        nextWave();
        state.stepT = 0;
      }
      const onPad = state.wave >= 5 &&
        Math.hypot(player.position.x, player.position.z) < 3.6;
      if (onPad !== state.onPad) {
        state.onPad = onPad;
        hud.pickup(onPad ? 'HOLD F TO EXTRACT' : '');
      }
      if (onPad && extractHeld) {
        state.extractT += dt;
        hud.pickup('EXTRACTING ' + Math.max(0, (1.6 - state.extractT)).toFixed(1) + 's');
        if (state.extractT >= 1.6) complete();
      } else {
        state.extractT = 0;
      }
    }

    function runBeats(dt) {
      state.stepT += dt;
      while (state.beatIdx < mission.beats.length && state.stepT >= mission.beats[state.beatIdx][0]) {
        const [, who, line] = mission.beats[state.beatIdx++];
        hud.say(who, line);
        SF.audio.sfx.comms();
      }
    }

    function checkCleared() {
      if (mission.boss) return;                 // the boss ends its own mission
      if (mission.survival) return;             // destinations end on extraction
      if (ai.alive === 0 && state.stepT > 2) complete();
    }

    function complete() {
      if (state.over) return;
      state.over = true;
      state.running = false;
      document.exitPointerLock();
      const notes = SF.classes.grantXp(character, state.xp);
      if (!isPlanet) SF.campaign.markCleared(character, missionIndex, state.time);
      else {
        character.expeditions = character.expeditions || {};
        const best = character.expeditions[missionIndex] || 0;
        character.expeditions[missionIndex] = Math.max(best, state.wave);
      }
      state.drops = SF.gear.rollDrops(character,
        isPlanet ? Math.min(16, 4 + state.wave * 1.6) : mission.n, !!mission.boss);
      SF.gear.grant(character, state.drops);
      SF.storage.save(character.slot, character);
      SF.audio.sfx.win();
      showEnd(true, notes);
    }

    function fail() {
      if (state.over) return;
      state.over = true;
      state.dying = false;
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
      const finale = won && mission.boss;
      $('#end-title').textContent = finale ? 'THE ARK IS QUIET'
                                  : won && mission.survival ? 'EXTRACTED'
                                  : won ? 'SECTOR CLEAR' : 'ASSET LOST';
      $('#end-title').className = won ? 'win' : 'lose';
      $('#end-sub').textContent = finale
        ? 'Two hundred thousand held notes finally allowed to fall. It will stay quiet.'
        : won && mission.survival ? `Cutter clear of ${mission.name} at wave ${state.wave}.`
        : won ? `${mission.name} secured. The route aft is open.`
        : mission.boss ? 'No harness charge is issued for Deck Zero. Take it again from the top.'
              : 'Every harness charge spent. Recovery Division budgets a replacement.';
      $('#end-stats').innerHTML = [
        [mission.survival ? 'DESTINATION' : 'MISSION',
         mission.survival ? mission.name + ' — WAVE ' + state.wave
                          : mission.n + ' / ' + SF.campaign.LAST + ' — ' + mission.name],
        ['HOSTILES DOWN', state.kills],
        ['ACCURACY', acc + '%'],
        ['HEAD SHOTS', weapon.state.headshots],
        ['TIME', Math.floor(state.time / 60) + 'm ' + Math.floor(state.time % 60) + 's'],
        ['HARNESS USED', state.deaths + (state.maxRespawns ? ' / ' + state.maxRespawns : ' — NONE ISSUED')],
        ['XP EARNED', won ? state.xp : Math.round(state.xp * 0.5)],
        ['RANK', character.level]
      ].map(([k, v]) => `<div class="es-row"><span>${k}</span><b>${v}</b></div>`).join('') +
        notes.map((n) => `<div class="es-up">${n}</div>`).join('') +
        (state.drops && state.drops.length
          ? '<div class="es-up">SALVAGE RECOVERED</div>' + state.drops.map((d) => {
              const r = SF.gear.rarityOf(d.rarity);
              return `<div class="es-drop" style="--rc:${r.colour}">` +
                     `<div class="d-name">${d.name}</div>` +
                     `<div class="d-meta">${r.name} · POWER ${d.power}</div></div>`;
            }).join('')
          : '');
      $('#screen-end').classList.add('active');
      hud.setVisible(false);
    }

    /* ---------- pause ---------- */
    function pause(on) {
      state.paused = on;
      $('#pause-menu').hidden = !on;
      firing = false;
      weapon.setAds(false);
      applyLookMode();
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

      if (state.dying) {
        // the world holds its breath while the harness works
        state.respawnT -= dt;
        if (state.respawnT <= 0) respawn();
        lights.update(dt, camera.position);
        eng.render(now / 1000, Math.max(state.damageFlash, 0.75));
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
        state.hp = Math.min(state.maxHp, state.hp + 14 * (1 + armour.regen / 100) * dt);
        hud.refreshVitals(state.hp, state.maxHp, state.overshield);
      }

      pickups.update(dt, player.position, weapon);
      runBeats(dt);
      if (mission.survival) survivalTick(dt);
      if (boss) boss.update(dt, player.position);
      checkCleared();

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
      hud.refreshHarness(state.respawns, state.maxRespawns);
      hud.objective(mission.survival
        ? '<b>' + mission.name + '</b> CLICK TO ENGAGE'
        : `<b>${mission.n}/${SF.campaign.LAST}</b> CLICK TO ENGAGE`);
      last = performance.now();
      if (!raf) raf = requestAnimationFrame(frame);
    }

    function engage() {
      if (state.running) return;
      state.running = true;
      state.paused = false;
      beginMission();
      requestLock();
      applyLookMode();
      last = performance.now();
    }

    function destroy() {
      cancelAnimationFrame(raf);
      raf = 0;
      canvas.removeEventListener('mousedown', onMouseDown);
      window.removeEventListener('mouseup', onMouseUp);
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
      canvas.removeEventListener('contextmenu', onContext);
      document.removeEventListener('pointerlockchange', onLockChange);
      pickups.destroy();
      if (boss) boss.destroy();
      hud.bossHide();
      hud.hideNodes();
      ai.clear();
      scene.traverse((o) => {
        if (o.geometry) o.geometry.dispose();
        if (o.material) (Array.isArray(o.material) ? o.material : [o.material]).forEach((m) => m.dispose());
      });
      eng.renderer.dispose();
      hud.setVisible(false);
      onExit();
    }

    return { start, engage, destroy, pause, requestLock, state, player, weapon, ai, level,
             engine: eng, hurt: hurtPlayer, pickupsRef: pickups,
             get bossRef() { return boss; } };
  }

  SF.game = { create };
})(window.SF);
