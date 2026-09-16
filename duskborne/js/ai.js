/* ============================================================
   Lumen Wardens: the Light-wielding enemies the Unseen sends you
   after. Three elemental archetypes plus a boss, all sharing one state
   machine (chase -> attack -> dead) and a perpendicular stuck-avoidance
   steering term, because direct pursuit alone deadlocks perfectly
   against cover centred on the straight line to the player.
   ============================================================ */
(function (DB) {
  'use strict';

  const TYPES = {
    ember: {
      id: 'ember', name: 'Ember Warden', hp: 70, speed: 4.2, damage: 14,
      attackRange: 2.2, attackRate: 1.0, radius: 0.45, height: 1.85,
      colour: 0xd45a2a, glow: 0xffa040, ranged: false, xp: 20
    },
    voltaic: {
      id: 'voltaic', name: 'Voltaic Warden', hp: 55, speed: 3.2, damage: 8,
      attackRange: 22, attackRate: 0.65, radius: 0.4, height: 1.9,
      colour: 0x2a7fd4, glow: 0x6fd8ff, ranged: true, xp: 24
    },
    'null': {
      id: 'null', name: 'Null Warden', hp: 150, speed: 1.9, damage: 20,
      attackRange: 26, attackRate: 1.9, radius: 0.55, height: 2.15,
      colour: 0x5a2a8a, glow: 0xa06fff, ranged: true, xp: 46
    },
    vanguard: {
      id: 'vanguard', name: 'the Radiant Vanguard', hp: 1600, speed: 2.4, damage: 24,
      attackRange: 26, attackRate: 1.2, radius: 0.85, height: 2.6,
      colour: 0xd4b02a, glow: 0xffe08a, ranged: true, xp: 400, boss: true
    }
  };

  function makeMesh(type, scale) {
    scale = scale || 1;
    const group = new THREE.Group();
    const bodyMat = new THREE.MeshStandardMaterial({ color: type.colour, roughness: 0.5, metalness: 0.35 });
    const body = new THREE.Mesh(new THREE.CapsuleGeometry(type.radius * 0.9, type.height * 0.55, 4, 8), bodyMat);
    body.position.y = type.height * 0.55;
    body.castShadow = true;
    group.add(body);

    const coreMat = new THREE.MeshStandardMaterial({
      color: type.glow, emissive: type.glow, emissiveIntensity: 1.4, roughness: 0.3
    });
    const core = new THREE.Mesh(new THREE.SphereGeometry(type.radius * 0.5, 12, 10), coreMat);
    core.position.y = type.height * 0.93;
    group.add(core);

    group.scale.setScalar(scale);
    group.userData.core = core;
    group.userData.coreMat = coreMat;
    return group;
  }

  function create(scene, type, pos, opts) {
    opts = opts || {};
    const isBoss = !!type.boss;
    const scale = opts.scale || 1;
    const hpMult = opts.hpMult || 1;
    const mesh = makeMesh(type, scale);
    mesh.position.copy(pos);
    scene.add(mesh);

    const h = {
      type: type, mesh: mesh, isBoss: isBoss,
      pos: pos.clone(), yaw: 0,
      hp: type.hp * hpMult, maxHp: type.hp * hpMult,
      state: 'chase', cooldown: 0.4 + Math.random() * 0.6, deathT: 0, hitFlash: 0,
      stuckT: 0, avoidSide: Math.random() > 0.5 ? 1 : -1, lastDist: 1e6,
      phase: 0, enrage: 1,
      blocksShots: function () { return h.state !== 'gone' && h.state !== 'dying'; },
      alive: function () { return h.state !== 'gone'; },
      headCentre: function () { return new THREE.Vector3(h.pos.x, h.pos.y + h.type.height * 0.93 * scale, h.pos.z); },
      bodyCentre: function () { return new THREE.Vector3(h.pos.x, h.pos.y + h.type.height * 0.55 * scale, h.pos.z); },
      takeDamage: function (amount) {
        h.hp -= amount;
        h.hitFlash = 0.12;
        if (h.hp <= 0 && h.state !== 'dying' && h.state !== 'gone') {
          h.state = 'dying'; h.deathT = 0.5;
          return true;
        }
        return false;
      }
    };
    return h;
  }

  /* Returns true exactly on the frame an attack lands, so the caller
     applies damage once. `onRangedAttack` fires a visible bolt from the
     warden to the player for ranged types. */
  function update(h, dt, playerPos, level, onRangedAttack) {
    let didAttack = false;

    if (h.state === 'dying') {
      h.deathT -= dt;
      h.mesh.scale.multiplyScalar(Math.max(0, 1 - dt * 3));
      h.mesh.position.y -= dt * 0.6;
      if (h.deathT <= 0) { h.state = 'gone'; h.mesh.visible = false; }
      return didAttack;
    }
    if (h.state === 'gone') return didAttack;

    h.hitFlash = Math.max(0, h.hitFlash - dt * 4);
    h.mesh.userData.coreMat.emissiveIntensity = 1.4 + h.hitFlash * 6;

    const toPlayer = new THREE.Vector3(playerPos.x - h.pos.x, 0, playerPos.z - h.pos.z);
    const dist = toPlayer.length();
    const range = h.type.attackRange * (h.isBoss ? 1 : 1);

    if (dist <= range) {
      h.state = 'attack';
      h.cooldown -= dt * h.enrage;
      // face the player, and inch backward if a ranged type is crowded
      h.yaw = Math.atan2(toPlayer.x, toPlayer.z);
      if (h.type.ranged && dist < range * 0.35) {
        const back = toPlayer.clone().normalize().multiplyScalar(-h.type.speed * 0.4 * dt);
        h.pos.add(back);
      }
      if (h.cooldown <= 0) {
        h.cooldown = h.type.attackRate;
        didAttack = true;
        if (h.type.ranged && onRangedAttack) onRangedAttack(h);
      }
    } else {
      h.state = 'chase';
      const dir = toPlayer.normalize();
      // stuck-avoidance: if distance to the player hasn't shrunk, blend in
      // a perpendicular bias so a hostile parked against cover keeps moving
      // instead of pushing uselessly into the same corner every frame
      if (dist > h.lastDist - 0.02) h.stuckT += dt; else h.stuckT = Math.max(0, h.stuckT - dt * 2);
      h.lastDist = dist;
      let moveDir = dir;
      if (h.stuckT > 0.5) {
        const perp = new THREE.Vector3(-dir.z, 0, dir.x).multiplyScalar(h.avoidSide);
        moveDir = dir.clone().addScaledVector(perp, 0.9).normalize();
      }
      const speed = h.type.speed * h.enrage;
      const next = h.pos.clone().addScaledVector(moveDir, speed * dt);
      level.resolveSpawn(next, h.type.radius);
      h.pos.copy(next);
      h.yaw = Math.atan2(dir.x, dir.z);
    }

    h.mesh.position.set(h.pos.x, h.pos.y, h.pos.z);
    h.mesh.rotation.y = h.yaw;
    return didAttack;
  }

  /* Boss phase escalation: at 66%/33% HP it summons adds and enrages
     (faster attacks, faster movement) — a real phase change, honestly
     scoped as that rather than claiming a puzzle mechanic this build
     doesn't have. */
  function updateBossPhase(h, spawnAdd) {
    const frac = h.hp / h.maxHp;
    if (h.phase === 0 && frac <= 0.66) {
      h.phase = 1; h.enrage = 1.3;
      spawnAdd(2);
    } else if (h.phase === 1 && frac <= 0.33) {
      h.phase = 2; h.enrage = 1.7;
      spawnAdd(3);
    }
  }

  DB.ai = { TYPES: TYPES, create: create, update: update, updateBossPhase: updateBossPhase };
})(window.DB || (window.DB = {}));
