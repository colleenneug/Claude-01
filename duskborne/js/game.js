/* ============================================================
   The mission loop: builds a level from a wave/boss spec, spawns
   Lumen Wardens, drives the player's weapon and Hollow ability, and
   tracks win (everything Gone) / loss (vitals at zero) conditions.
   Both the tutorial and the Sunken Throne questline are the same
   Mission with a different spec — a scripted, gentle one and a real
   fight — which is also how the native C++ build treats missions as
   one engine over swappable content.
   ============================================================ */
(function (DB) {
  'use strict';

  const PLAYER_MAX_HP = 100;

  function createMission(threeCtx, character, spec, hud, callbacks) {
    const level = DB.level.build(threeCtx.scene, {
      half: spec.half || 40, theme: spec.theme || 'throne', crates: spec.crates
    });
    const player = DB.player.create(threeCtx.camera, level);
    player.reset(level.playerStart);

    const species = DB.classes.species(character.speciesId);
    const klass = DB.classes.klass(character.classId);
    const weapon = DB.weapons.createWeapon(klass.weapon);
    const abilityDef = klass.ability;
    const abilityTimer = DB.weapons.createAbilityTimer(abilityDef);
    const abilityRegenMult = (species.statMod.abilityRegen || 1);
    const abilityDamageMult = (species.statMod.abilityDamage || 1) * (1 + (character.disciplineBonus || 0));
    const maxHp = Math.round(PLAYER_MAX_HP * (species.statMod.health || 1));
    const damageResist = klass.damageResist || 0;

    let hp = maxHp, shield = 0, invulnT = 0;
    let hostiles = [];
    let waveTotal = 0;
    let bossPending = !!spec.boss;
    let boss = null;
    let missionState = 'active';
    let killCount = 0;
    const tracers = [];

    function spawnType(id) { return DB.ai.TYPES[id]; }

    function spawnWaves() {
      (spec.waves || []).forEach(function (w) {
        const type = spawnType(w.typeId);
        for (let i = 0; i < w.count; i++) {
          const p = level.spawnPoint(hostiles.length, w.count, w.radius || 20);
          hostiles.push(DB.ai.create(threeCtx.scene, type, p));
        }
      });
      waveTotal = hostiles.length;
    }
    spawnWaves();

    function spawnBossAdds(count) {
      const pool = ['ember', 'voltaic', 'null'];
      for (let i = 0; i < count; i++) {
        const type = spawnType(pool[i % pool.length]);
        const p = level.spawnPoint(hostiles.length, count, 14);
        hostiles.push(DB.ai.create(threeCtx.scene, type, p));
      }
    }

    function spawnBossIfReady() {
      if (!bossPending) return;
      const clear = hostiles.slice(0, waveTotal).every(function (h) { return h.state === 'gone'; });
      if (!clear) return;
      const type = spawnType(spec.boss.typeId);
      boss = DB.ai.create(threeCtx.scene, type, new THREE.Vector3(0, 0, -level.half * 0.5),
                           { hpMult: spec.boss.hpMult || 1, scale: 1.6 });
      hostiles.push(boss);
      bossPending = false;
      if (callbacks.onBossIntro) callbacks.onBossIntro(type.name);
    }

    function tracer(from, to, colour) {
      const geo = new THREE.BufferGeometry().setFromPoints([from, to]);
      const mat = new THREE.LineBasicMaterial({ color: colour, transparent: true, opacity: 0.85 });
      const line = new THREE.Line(geo, mat);
      threeCtx.scene.add(line);
      tracers.push({ line: line, t: 0.12 });
    }

    function damagePlayer(amount) {
      if (invulnT > 0 || hp <= 0) return;
      let dmg = amount * (1 - damageResist);
      if (shield > 0) {
        const absorbed = Math.min(shield, dmg);
        shield -= absorbed;
        dmg -= absorbed;
      }
      if (dmg <= 0) return;
      hp = Math.max(0, hp - dmg);
      hud.damageFlash(0.4);
    }

    function onRangedAttack(h) {
      const from = h.headCentre();
      const to = player.eyePosition;
      tracer(from, to, h.type.glow);
      damagePlayer(h.type.damage);
    }

    function fireWeapon() {
      const origin = player.eyePosition;
      const dir = new THREE.Vector3();
      threeCtx.camera.getWorldDirection(dir);
      const result = weapon.fire(origin, dir, level, hostiles);
      if (!result) return;
      if (result.hitHostile) {
        const h = hostiles[result.hostileIndex];
        const killed = h.takeDamage(result.damage);
        hud.hitMarker(result.headshot);
        DB.audio.sfx[result.headshot ? 'headshot' : 'hitMarker']();
        if (killed) {
          killCount++;
          hud.killFeed((result.headshot ? 'HEADSHOT — ' : '') + h.type.name + ' erased');
          DB.audio.sfx.kill();
        }
      }
      player.addRecoil(0.012 + Math.random() * 0.006, (Math.random() - 0.5) * 0.01);
      DB.audio.sfx[klass.id === 'bastion' ? 'shootHeavy' : klass.id === 'wraithblade' ? 'shootSmg' : 'shootLight']();
    }

    function facing() {
      const dir = new THREE.Vector3();
      threeCtx.camera.getWorldDirection(dir);
      dir.y = 0; dir.normalize();
      return dir;
    }

    function useAbility() {
      abilityTimer.trigger();
      DB.audio.sfx.ability();
      const pos = player.position;
      const dir = facing();

      if (abilityDef.discipline === 'rime') {
        shield = Math.min(abilityDef.shield, shield + abilityDef.shield);
        hostiles.forEach(function (h) {
          if (!h.alive()) return;
          const d = h.pos.distanceTo(pos);
          if (d <= abilityDef.radius) {
            const enrageBefore = h.enrage;
            const killed = h.takeDamage(abilityDef.damage * abilityDamageMult);
            h.enrage = Math.max(0.35, h.enrage * (1 - abilityDef.slow));
            setTimeout(function () { if (h.alive()) h.enrage = enrageBefore; }, abilityDef.slowTime * 1000);
            if (killed) { killCount++; hud.killFeed(h.type.name + ' erased'); }
          }
        });
      } else if (abilityDef.discipline === 'umbral') {
        hostiles.forEach(function (h) {
          if (!h.alive()) return;
          const to = new THREE.Vector3(h.pos.x - pos.x, 0, h.pos.z - pos.z);
          const d = to.length();
          if (d > abilityDef.range || d < 0.01) return;
          to.normalize();
          const ang = THREE.MathUtils.radToDeg(Math.acos(THREE.MathUtils.clamp(dir.dot(to), -1, 1)));
          if (ang > abilityDef.coneDegrees / 2) return;
          const killed = h.takeDamage(abilityDef.damage * abilityDamageMult);
          h.dotDps = abilityDef.dot * abilityDamageMult;
          h.dotT = abilityDef.dotTime;
          if (killed) { killCount++; hud.killFeed(h.type.name + ' erased'); }
        });
      } else if (abilityDef.discipline === 'weave') {
        const start = pos.clone();
        const end = pos.clone().addScaledVector(dir, abilityDef.distance);
        level.resolveSpawn(end, 0.4);
        hostiles.forEach(function (h) {
          if (!h.alive()) return;
          const t = THREE.MathUtils.clamp((h.pos.clone().sub(start)).dot(dir) / abilityDef.distance, 0, 1);
          const closest = start.clone().addScaledVector(dir, t * abilityDef.distance);
          if (closest.distanceTo(h.pos) < 1.4) {
            const killed = h.takeDamage(abilityDef.damage * abilityDamageMult);
            if (killed) { killCount++; hud.killFeed(h.type.name + ' erased'); }
          }
        });
        player.state.pos.copy(end);
        invulnT = abilityDef.invulnTime;
      }
    }

    function update(dt, input) {
      if (missionState !== 'active') return;

      player.update(dt);
      weapon.update(dt);
      if (input.reloadHeld) weapon.startReload();
      if (input.firePressed && weapon.canFire()) fireWeapon();

      abilityTimer.cooldownT = Math.max(0, abilityTimer.cooldownT - dt * abilityRegenMult);
      if (input.abilityPressed && abilityTimer.ready() && hp > 0) useAbility();

      invulnT = Math.max(0, invulnT - dt);
      shield = Math.max(0, shield - dt * 2.5); // a shield fades, it doesn't last forever

      for (let i = 0; i < hostiles.length; i++) {
        const h = hostiles[i];
        if (!h.alive()) continue;
        if (h.dotT > 0) {
          h.dotT -= dt;
          const killed = h.takeDamage(h.dotDps * dt);
          if (killed) { killCount++; hud.killFeed(h.type.name + ' erased'); }
        }
        DB.ai.update(h, dt, player.position, level, onRangedAttack);
      }
      if (boss && boss.alive()) {
        DB.ai.updateBossPhase(boss, spawnBossAdds);
        hud.setBoss(true, boss.hp / boss.maxHp, boss.type.name);
      } else if (boss) {
        hud.setBoss(false, 0, '');
      }
      spawnBossIfReady();

      for (let i = tracers.length - 1; i >= 0; i--) {
        tracers[i].t -= dt;
        tracers[i].line.material.opacity = Math.max(0, tracers[i].t / 0.12) * 0.85;
        if (tracers[i].t <= 0) { threeCtx.scene.remove(tracers[i].line); tracers[i].line.geometry.dispose(); tracers.splice(i, 1); }
      }

      hud.setVitals(hp / maxHp, shield / (abilityDef.shield || 1));
      hud.setAbility(abilityTimer.fraction, abilityDef.name, klass.icon);
      hud.setAmmo(weapon.ammoInMag, weapon.reserveAmmo, weapon.reloading);
      hud.tick(dt);

      if (hp <= 0) {
        missionState = 'failed';
        if (callbacks.onFail) callbacks.onFail({ kills: killCount });
      } else {
        const wavesClear = hostiles.slice(0, waveTotal).every(function (h) { return h.state === 'gone'; });
        const bossClear = !spec.boss || (boss && boss.state === 'gone');
        if (wavesClear && !bossPending && bossClear && (spec.objective ? spec.objective.done(hostiles, killCount) : true)) {
          missionState = 'complete';
          if (callbacks.onComplete) callbacks.onComplete({ kills: killCount });
        }
      }
    }

    return {
      player: player, level: level,
      get state() { return missionState; },
      get hp() { return hp; }, get maxHp() { return maxHp; },
      get killCount() { return killCount; },
      update: update,
      requestLock: function (canvas) {
        canvas.requestPointerLock = canvas.requestPointerLock || canvas.mozRequestPointerLock;
        canvas.requestPointerLock();
      },
      dispose: function () {
        level.dispose();
        hostiles.forEach(function (h) { threeCtx.scene.remove(h.mesh); });
        tracers.forEach(function (t) { threeCtx.scene.remove(t.line); t.line.geometry.dispose(); });
      }
    };
  }

  /* ---------- gear / power ---------- */
  const RARITY = [
    { id: 'common', label: 'Common', chance: 0.5, power: 5, colour: '#9aa0ab' },
    { id: 'rare', label: 'Rare', chance: 0.3, power: 12, colour: '#5ee6a8' },
    { id: 'legendary', label: 'Legendary', chance: 0.16, power: 22, colour: '#9a5ef0' },
    { id: 'exotic', label: 'Exotic', chance: 0.04, power: 40, colour: '#e6c25e' }
  ];
  function rollGear() {
    let r = Math.random(), acc = 0;
    for (const tier of RARITY) { acc += tier.chance; if (r <= acc) return tier; }
    return RARITY[0];
  }

  DB.game = { createMission: createMission, RARITY: RARITY, rollGear: rollGear, PLAYER_MAX_HP: PLAYER_MAX_HP };
})(window.DB || (window.DB = {}));
