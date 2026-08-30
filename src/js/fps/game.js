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
      wave: 0, onPad: false, extractT: 0, events: 0, parts: 0,
      spawned: false, bossBeaten: false,
      respawns: 0, maxRespawns: 0, deaths: 0, dying: false, respawnT: 0, started: false
    };
    state.maxRespawns = SF.campaign.respawnsFor(SF.campaign.byIndex(missionIndex));
    state.respawns = state.maxRespawns;
    let boss = null;
    let patrol = null;
    let runner = null;                  // the transit frame, on open ground only
    let chests = null;

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

    /* ---------- co-op ---------- */
    const online = SF.net && SF.net.active;
    const hosting = online ? SF.net.isHost : true;
    const remotes = online ? SF.remote.create({ scene, lights }) : null;
    if (online) {
      ai.setRemote(!hosting);           // only the host simulates hostiles
      remotes.setRoster(SF.net.roster());
      SF.net.on('players', (list) => remotes.setRoster(list));
      SF.net.on('enemies', (snap) => { if (!hosting) ai.applySnapshot(snap); });
      SF.net.on('hit', (m) => {
        // a teammate's shot, applied here because this client owns the hostiles
        if (!hosting) return;
        const e = ai.byUid(m.d.uid);
        if (e) ai.damage(e, m.d.dmg, null);
      });
      SF.net.on('event', (m) => {
        if (m.d.kind === 'wave') { state.wave = m.d.wave; hud.banner('WAVE ' + m.d.wave); }
        if (m.d.kind === 'objective') hud.objective(m.d.text);
        if (m.d.kind === 'down') hud.killFeed((m.d.name || 'OPERATIVE') + ' IS DOWN');
        if (m.d.kind === 'complete' && !state.over) complete();
        if (m.d.kind === 'patrol' && patrol) patrol.applyRemote(m.d.ev);
      });
    }
    let netAccum = 0;

    const pickups = SF.pickups.create({ scene, level, lights, hud });

    const weapon = SF.weapons.create({
      scene, camera, player, ai, hud, level, lights,
      classId: character.cls,
      mods: SF.gear.weaponMods(character),
      reportDamage: (enemy, dmg, dir) => {
        if (hosting) return ai.damage(enemy, dmg, dir);
        enemy.hitFlash = 1;                        // local feedback, host is authoritative
        SF.net.send({ t: 'hit', d: { uid: enemy.uid, dmg: dmg } });
        return false;
      },
      itemName: character.equipped.weapon ? character.equipped.weapon.name : null,
      /* Riding, the shot leaves from a point on the camera's centre line
         rather than from the eye, so the crosshair still means something
         with the camera sitting behind the rider. */
      originOverride: () => (runner && runner.mounted ? runner.shotOrigin() : null),
      spreadScale: () => (runner && runner.mounted
        ? (runner.boosting ? 3.2 : 1.8)      // one hand on the bars
        : null),
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
    function onKeyUp(e) {
      if (e.code === 'KeyF') extractHeld = false;
      if (e.code === 'ShiftLeft' && runner) runner.setBoost(false);
    }
    window.addEventListener('keyup', onKeyUp);

    function onMouseUp(e) {
      if (e.button === 0) firing = false;
      if (e.button === 2) weapon.setAds(false);
    }
    function onKeyDown(e) {
      if (!state.running) return;
      if (e.code === 'KeyF') extractHeld = true;
      if (e.code === 'KeyV' && runner) runner.toggle();
      if (e.code === 'ShiftLeft' && runner) runner.setBoost(true);
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
      if (mission.patrol) {
        player.setSpeedScale(2.1);        // a kilometre of ground needs the legs
        patrol = SF.patrol.create({
          scene, level, ai, hud, player, lights, net: SF.net,
          spec: mission.planet, hosting: hosting, hpScale: scale.hp,
          onReward: (tier, name) => {
            const drops = SF.gear.rollDrops(character, Math.min(16, 6 + tier * 3), tier > 1);
            SF.gear.grant(character, drops);
            const parts = SF.gear.rollParts(1 + tier);
            SF.gear.grantParts(character, parts);
            state.parts += Object.keys(parts).reduce((a, k) => a + parts[k], 0);
            state.xp += 120 * tier;
            state.events++;
            SF.storage.save(character.slot, character);
            for (const d of drops) hud.killFeed('SALVAGE — ' + d.name);
            hud.say('DIVISION', name + ' resolved. Salvage is yours.', 4200);
          }
        });
        /* Open ground: a frame to cross it on, and crates worth crossing it for. */
        runner = SF.runner.create({
          scene, camera, player, hud, lights, character,
          isOpenZone: true, baseSpeedScale: 2.1,
          // the first-person weapon has no place in a third-person view
          onMount: (on) => { weapon.view.visible = !on; },
          onCrash: (dmg) => hurtPlayer(dmg, null)
        });
        chests = SF.chests.create({
          scene, level, lights, hud, player,
          onOpen: (kind, count, tierBonus) => {
            const drops = SF.gear.rollDrops(character, Math.min(16, 9 + tierBonus), count > 1);
            SF.gear.grant(character, drops);
            /* Crates are where runner parts come from. That closes the loop:
               ride out to a crate, and the crate pays for a better ride. */
            const parts = SF.gear.rollParts(count > 1 ? 3 : 1 + (Math.random() < 0.4 ? 1 : 0));
            SF.gear.grantParts(character, parts);
            state.parts += Object.keys(parts).reduce((a, k) => a + parts[k], 0);
            state.xp += 60 + tierBonus * 20;
            SF.storage.save(character.slot, character);
            for (const d of drops) hud.killFeed('SALVAGE — ' + d.name);
            for (const k of Object.keys(parts)) {
              if (parts[k]) hud.killFeed('PARTS — ' + parts[k] + '× ' + SF.gear.partOf(k).name);
            }
            hud.say('DIVISION', kind.name + ' cracked. Take what is in it.', 3400);
          }
        });

        hud.objective('<b>PATROL</b> ' + mission.objective);
        state.spawned = true;
        SF.audio.sfx.objective();
        return;
      }
      hud.objective(`<b>${mission.n}/${SF.campaign.LAST}</b> ${mission.objective}`);

      if (hosting) {
        for (const [type, count] of mission.waves) {
          for (let n = 0; n < count; n++) {
            const at = spawnPointIn(zone);
            const e = ai.spawn(type, at.x, at.z);
            e.maxHp = Math.round(e.maxHp * scale.hp);      // later missions are tougher
            e.hp = e.maxHp;
          }
        }
      }

      if (mission.boss && hosting) {
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
      if (runner) runner.onHit(dmg);
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
      if (runner) runner.dismount(null);      // you do not keep the frame through a death

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

    /* On a patrol you can leave whenever you like: stand on the pad and hold F.
       Nothing else ends the trip. */
    function patrolTick(dt) {
      patrol.update(dt);
      const onPad = Math.hypot(player.position.x, player.position.z) < 3.6;
      if (onPad !== state.onPad) {
        state.onPad = onPad;
        hud.pickup(onPad ? 'HOLD F TO LEAVE' : '');
      }
      if (onPad) {
        if (extractHeld) {
          state.extractT += dt;
          hud.pickup('CALLING CUTTER ' + Math.max(0, (1.6 - state.extractT)).toFixed(1) + 's');
          if (state.extractT >= 1.6) complete();
        } else {
          state.extractT = 0;
        }
        return;
      }
      state.extractT = 0;
      if (chests) chests.update(dt, extractHeld);
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
      if (mission.patrol) return;               // a patrol ends when you leave
      if (!hosting) return;                     // the host calls the sector clear
      if (ai.alive === 0 && state.stepT > 2) {
        if (online) SF.net.send({ t: 'event', d: { kind: 'complete' } });
        complete();
      }
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
        character.expeditions[missionIndex] = Math.max(best, state.events);
      }
      state.drops = SF.gear.rollDrops(character,
        isPlanet ? Math.min(16, 5 + state.events * 2) : mission.n, !!mission.boss);
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
                                  : won && mission.patrol ? 'RETURNED TO ORBIT'
                                  : won ? 'SECTOR CLEAR' : 'ASSET LOST';
      $('#end-title').className = won ? 'win' : 'lose';
      $('#end-sub').textContent = finale
        ? 'Two hundred thousand held notes finally allowed to fall. It will stay quiet.'
        : won && mission.patrol
            ? `Cutter clear of ${mission.name}. ${state.events} public event${state.events === 1 ? '' : 's'} resolved.`
        : won ? `${mission.name} secured. The route aft is open.`
        : mission.boss ? 'No harness charge is issued for Deck Zero. Take it again from the top.'
              : 'Every harness charge spent. Recovery Division budgets a replacement.';
      $('#end-stats').innerHTML = [
        [mission.patrol ? 'DESTINATION' : 'MISSION',
         mission.patrol ? mission.name + ' — ' + state.events + ' EVENTS'
                        : mission.n + ' / ' + SF.campaign.LAST + ' — ' + mission.name],
        ['HOSTILES DOWN', state.kills],
        ['ACCURACY', acc + '%'],
        ['HEAD SHOTS', weapon.state.headshots],
        ['TIME', Math.floor(state.time / 60) + 'm ' + Math.floor(state.time % 60) + 's'],
        ['HARNESS USED', state.deaths + (state.maxRespawns ? ' / ' + state.maxRespawns : ' — NONE ISSUED')],
        ['XP EARNED', won ? state.xp : Math.round(state.xp * 0.5)],
        ...(state.parts ? [['RUNNER PARTS', state.parts]] : []),
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

      if (online) {
        const eye = player.eyePosition;
        SF.net.sendState({
          p: [+player.position.x.toFixed(2), +player.position.y.toFixed(2), +player.position.z.toFixed(2)],
          y: +player.state.yaw.toFixed(2), h: Math.round(state.hp), f: firing ? 1 : 0
        });
        remotes.update(dt, SF.net.players, eye);
        hud.squad(SF.net.roster().map((p) => ({
          name: p.name, cls: p.cls, hp: p.state ? p.state.h : 100
        })));
        netAccum += dt;
        if (hosting && netAccum >= 0.08) {          // ~12 Hz world snapshots
          netAccum = 0;
          SF.net.send({ t: 'enemies', d: ai.snapshot() });
        }
      }

      if (runner) runner.update(dt);
      pickups.update(dt, player.position, weapon);
      runBeats(dt);
      if (mission.patrol) patrolTick(dt);
      if (boss) boss.update(dt, player.position);
      checkCleared();

      const base = weapon.state.ads ? 3 : 9;
      hud.updateCrosshair(dt, base + Math.hypot(player.state.vel.x, player.state.vel.z) * 1.6);
      hud.refreshAbility(weapon.state);

      eng.render(now / 1000, state.damageFlash);
    }

    /* ---------- lifecycle ---------- */
    /* The engage overlay is owned by the mission that is waiting on it, not
       by the menu layer: a listener living outside the mission can end up
       holding a stale reference once a second mission is created, and then
       the click quietly does nothing. */
    const engageEl = $('#engage');
    let awaitingEngage = false;

    /* Listen on the document rather than the overlay: whatever is on top,
       a click or a keypress while we are waiting means "start". */
    function onEngageInput(e) {
      if (!awaitingEngage || state.running) return;
      if (e.type === 'keydown' && (e.key === 'Escape' || e.key === 'Tab')) return;
      engageEl.hidden = true;
      SF.audio.unlock();
      engage();
    }

    /* Build and render the world, but hold the mission until the player
       clicks to engage — that click is the gesture pointer lock needs. */
    function start() {
      awaitingEngage = true;
      document.addEventListener('pointerdown', onEngageInput);
      document.addEventListener('keydown', onEngageInput);
      state.started = true;
      weapon.prewarm(eng.renderer, scene, camera);
      ai.prewarm(eng.renderer, scene, camera);
      eng.renderer.compile(scene, camera);
      hud.setVisible(true);
      hud.refreshVitals(state.hp, state.maxHp, state.overshield);
      hud.refreshAmmo(weapon.state);
      hud.refreshAbility(weapon.state);
      hud.refreshHarness(state.respawns, state.maxRespawns);
      hud.objective(mission.patrol
        ? '<b>' + mission.name + '</b> CLICK TO ENGAGE'
        : `<b>${mission.n}/${SF.campaign.LAST}</b> CLICK TO ENGAGE`);
      last = performance.now();
      if (!raf) raf = requestAnimationFrame(frame);
    }

    function engage() {
      if (state.running) return;
      awaitingEngage = false;
      document.removeEventListener('pointerdown', onEngageInput);
      document.removeEventListener('keydown', onEngageInput);
      engageEl.hidden = true;
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
      awaitingEngage = false;
      document.removeEventListener('pointerdown', onEngageInput);
      document.removeEventListener('keydown', onEngageInput);
      document.removeEventListener('pointerlockchange', onLockChange);
      pickups.destroy();
      if (runner) runner.destroy();
      if (chests) chests.destroy();
      hud.runner(null);
      if (patrol) patrol.destroy();
      if (boss) boss.destroy();
      if (remotes) remotes.clear();
      hud.bossHide();
      hud.hideNodes();
      hud.squad([]);
      hud.clearEvent();
      ai.clear();
      scene.traverse((o) => {
        if (o.geometry) o.geometry.dispose();
        if (o.material) (Array.isArray(o.material) ? o.material : [o.material]).forEach((m) => m.dispose());
      });
      eng.renderer.dispose();
      SF.materials.reset();          // the traverse above disposed the shared cache
      hud.setVisible(false);
      onExit();
    }

    return { start, engage, destroy, pause, requestLock, state, player, weapon, ai, level,
             engine: eng, hurt: hurtPlayer, pickupsRef: pickups, remotesRef: remotes,
             get patrolRef() { return patrol; },
             get runnerRef() { return runner; },
             get chestsRef() { return chests; },
             get bossRef() { return boss; } };
  }

  SF.game = { create };
})(window.SF);
