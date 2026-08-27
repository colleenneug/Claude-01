/* ============================================================
   Hostiles.

   Each one is a small procedural rig (body, head, limbs) with a head
   sphere that takes multiplied damage. Steering is direct pursuit with
   wall avoidance sampled off the level colliders — enough to make them
   round a corner without a full navmesh.
   ============================================================ */
(function (SF) {
  'use strict';

  const TYPES = {
    drone: {
      name: 'HUSK DRONE', hp: 60, speed: 3.2, damage: 9, range: 2.2, rate: 1.1,
      colour: 0x8a3b3b, glow: 0xff5a4a, height: 1.5, radius: 0.45, flying: true, xp: 20
    },
    thrall: {
      name: 'CHOIR THRALL', hp: 85, speed: 2.7, damage: 13, range: 2.0, rate: 1.4,
      colour: 0x5c5a68, glow: 0xff7ab0, height: 1.85, radius: 0.42, xp: 26
    },
    warden: {
      name: 'WARDEN FRAME', hp: 240, speed: 2.1, damage: 20, range: 26, rate: 2.1,
      colour: 0x4a5260, glow: 0xffb454, height: 2.5, radius: 0.75, ranged: true, xp: 90
    },
    conductor: {
      name: 'THE CONDUCTOR', hp: 900, speed: 2.4, damage: 26, range: 30, rate: 1.6,
      colour: 0x6b3550, glow: 0xff3fa0, height: 3.0, radius: 0.9, ranged: true, boss: true, xp: 320
    }
  };

  function create(ctx) {
    const { scene, level } = ctx;
    const enemies = [];
    const projectiles = [];
    const tmp = new THREE.Vector3();

    /* ---------- construction ---------- */
    function buildRig(spec) {
      const g = new THREE.Group();
      const skin = new THREE.MeshStandardMaterial({
        color: spec.colour, metalness: 0.7, roughness: 0.55
      });
      const glow = new THREE.MeshStandardMaterial({
        color: 0x0a0a0c, emissive: new THREE.Color(spec.glow), emissiveIntensity: 2.4,
        metalness: 0.4, roughness: 0.4
      });

      const h = spec.height, r = spec.radius;
      const torso = new THREE.Mesh(new THREE.CapsuleGeometry(r * 0.8, h * 0.42, 4, 10), skin);
      torso.position.y = h * 0.58;
      const head = new THREE.Mesh(new THREE.SphereGeometry(r * 0.52, 14, 12), skin);
      head.position.y = h * 0.93;
      const eye = new THREE.Mesh(new THREE.SphereGeometry(r * 0.2, 10, 8), glow);
      eye.position.set(0, h * 0.93, -r * 0.44);
      g.add(torso, head, eye);

      const limbs = [];
      if (!spec.flying) {
        for (const side of [-1, 1]) {
          const arm = new THREE.Mesh(new THREE.CapsuleGeometry(r * 0.2, h * 0.34, 3, 6), skin);
          arm.position.set(side * r * 0.95, h * 0.6, 0);
          const leg = new THREE.Mesh(new THREE.CapsuleGeometry(r * 0.26, h * 0.34, 3, 6), skin);
          leg.position.set(side * r * 0.42, h * 0.2, 0);
          g.add(arm, leg);
          limbs.push({ arm, leg, side });
        }
      } else {
        const ring = new THREE.Mesh(new THREE.TorusGeometry(r * 1.1, r * 0.14, 8, 20), glow);
        ring.rotation.x = Math.PI / 2;
        ring.position.y = h * 0.55;
        g.add(ring);
        limbs.push({ ring });
      }

      const halo = new THREE.PointLight(spec.glow, 0.9, 7, 2);
      halo.position.y = h * 0.8;
      g.add(halo);

      for (const c of g.children) { c.castShadow = true; c.receiveShadow = true; }
      return { group: g, limbs, head, halo, materials: [skin, glow] };
    }

    function spawn(typeId, x, z) {
      const spec = TYPES[typeId];
      const rig = buildRig(spec);
      rig.group.position.set(x, 0, z);
      scene.add(rig.group);

      const e = {
        type: typeId, spec, rig,
        pos: new THREE.Vector3(x, 0, z),
        vel: new THREE.Vector3(),
        hp: spec.hp, maxHp: spec.hp,
        cool: Math.random() * spec.rate,
        stun: 0, dead: false, deathT: 0,
        hitFlash: 0, bob: Math.random() * 6.28,
        alerted: false
      };
      enemies.push(e);
      return e;
    }

    /* ---------- combat maths ---------- */
    const headCentre = (e) => tmp.set(e.pos.x, e.pos.y + e.spec.height * 0.93, e.pos.z).clone();
    const bodyCentre = (e) => new THREE.Vector3(e.pos.x, e.pos.y + e.spec.height * 0.58, e.pos.z);

    /* Ray vs sphere; returns distance along the ray or -1. */
    function raySphere(origin, dir, centre, radius) {
      const oc = centre.clone().sub(origin);
      const t = oc.dot(dir);
      if (t < 0) return -1;
      const d2 = oc.lengthSq() - t * t;
      const r2 = radius * radius;
      if (d2 > r2) return -1;
      return t - Math.sqrt(r2 - d2);
    }

    /* Ray vs the level's AABBs; returns the nearest wall distance. */
    function rayWalls(origin, dir, maxDist, colliders) {
      let best = maxDist, normal = null;
      for (const c of colliders) {
        const inv = { x: 1 / (dir.x || 1e-6), z: 1 / (dir.z || 1e-6) };
        let t0 = (c.min.x - origin.x) * inv.x, t1 = (c.max.x - origin.x) * inv.x;
        if (t0 > t1) { const s = t0; t0 = t1; t1 = s; }
        let u0 = (c.min.z - origin.z) * inv.z, u1 = (c.max.z - origin.z) * inv.z;
        if (u0 > u1) { const s = u0; u0 = u1; u1 = s; }
        const enter = Math.max(t0, u0), exit = Math.min(t1, u1);
        if (enter > exit || exit < 0 || enter > best) continue;
        const y = origin.y + dir.y * enter;
        if (y < c.bottom || y > c.top) continue;
        best = enter;
        normal = Math.abs(enter - t0) < Math.abs(enter - u0)
          ? new THREE.Vector3(-Math.sign(dir.x), 0, 0)
          : new THREE.Vector3(0, 0, -Math.sign(dir.z));
      }
      return { dist: best, normal };
    }

    /* Used by the weapon: nearest wall, and every enemy in front of it. */
    function raycast(origin, dir, range, colliders, pierce) {
      const wall = rayWalls(origin, dir, range, colliders);
      const hits = [];
      for (const e of enemies) {
        if (e.dead) continue;
        const hd = raySphere(origin, dir, headCentre(e), e.spec.radius * 0.62);
        const bd = raySphere(origin, dir, bodyCentre(e), e.spec.radius * 1.05);
        const isHead = hd >= 0 && (bd < 0 || hd <= bd);
        const d = isHead ? hd : bd;
        if (d < 0 || d > wall.dist) continue;
        hits.push({ enemy: e, head: isHead, dist: d, point: origin.clone().addScaledVector(dir, d) });
      }
      hits.sort((a, b) => a.dist - b.dist);
      const taken = pierce ? hits : hits.slice(0, 1);
      return {
        enemies: taken,
        point: taken.length ? taken[taken.length - 1].point
             : (wall.dist < range ? origin.clone().addScaledVector(dir, wall.dist) : null),
        normal: wall.normal
      };
    }

    function damage(e, amount, dir) {
      if (e.dead) return false;
      e.hp -= amount;
      e.hitFlash = 1;
      e.alerted = true;
      if (dir) { e.vel.addScaledVector(dir, 1.4 / (e.spec.radius * 3)); }
      SF.audio.sfx.impact();
      if (e.hp <= 0) {
        e.dead = true;
        e.deathT = 0;
        e.rig.halo.intensity = 0;
        SF.audio.sfx.enemyDown();
        ctx.onKill(e);
        return true;
      }
      return false;
    }

    /* Oracle's EMP. */
    function pulse(centre, radius, dmg) {
      let n = 0;
      for (const e of enemies) {
        if (e.dead) continue;
        if (e.pos.distanceTo(centre) > radius) continue;
        e.stun = 3.2;
        n++;
        damage(e, dmg, null);
      }
      return n;
    }

    /* ---------- steering ---------- */
    function blocked(x, z, radius) {
      for (const c of level.colliders) {
        if (c.top < 0.6) continue;
        if (x > c.min.x - radius && x < c.max.x + radius &&
            z > c.min.z - radius && z < c.max.z + radius) return true;
      }
      return false;
    }

    function step(dt, playerPos, playerVisible) {
      for (const e of enemies) {
        if (e.dead) {
          e.deathT += dt;
          const t = Math.min(1, e.deathT / 0.9);
          e.rig.group.rotation.x = -t * 1.5;
          e.rig.group.position.y = -t * 0.5;
          for (const m of e.rig.materials) { m.transparent = true; m.opacity = 1 - t; }
          continue;
        }

        e.hitFlash = Math.max(0, e.hitFlash - dt * 4);
        e.stun = Math.max(0, e.stun - dt);
        e.cool -= dt;

        const toPlayer = new THREE.Vector3().subVectors(playerPos, e.pos);
        const dist = toPlayer.length();
        if (dist < 26 && playerVisible) e.alerted = true;

        if (e.stun > 0) {
          e.rig.group.rotation.z = Math.sin(e.stun * 40) * 0.12;
          continue;
        }
        e.rig.group.rotation.z = 0;

        if (e.alerted) {
          toPlayer.y = 0;
          const dir = toPlayer.clone().normalize();

          const stopAt = e.spec.ranged ? Math.min(e.spec.range * 0.55, 14) : e.spec.range * 0.75;
          if (dist > stopAt) {
            // avoidance: if the direct step is blocked, try fanned-out angles
            let move = dir.clone();
            const probe = e.pos.clone().addScaledVector(dir, e.spec.radius + 0.9);
            if (blocked(probe.x, probe.z, e.spec.radius)) {
              for (const ang of [0.6, -0.6, 1.2, -1.2, 1.9, -1.9]) {
                const alt = dir.clone().applyAxisAngle(new THREE.Vector3(0, 1, 0), ang);
                const p2 = e.pos.clone().addScaledVector(alt, e.spec.radius + 0.9);
                if (!blocked(p2.x, p2.z, e.spec.radius)) { move = alt; break; }
              }
            }
            e.vel.x += (move.x * e.spec.speed - e.vel.x) * Math.min(1, 6 * dt);
            e.vel.z += (move.z * e.spec.speed - e.vel.z) * Math.min(1, 6 * dt);
          } else {
            e.vel.multiplyScalar(Math.max(0, 1 - 7 * dt));
          }

          // attack
          if (e.cool <= 0 && dist <= e.spec.range && playerVisible) {
            e.cool = e.spec.rate;
            if (e.spec.ranged) fireProjectile(e, playerPos);
            else if (dist <= e.spec.range) { ctx.onPlayerHit(e.spec.damage, e.pos); SF.audio.sfx.melee(); }
          }

          e.rig.group.rotation.y = Math.atan2(toPlayer.x, toPlayer.z) + Math.PI;
        } else {
          e.vel.multiplyScalar(Math.max(0, 1 - 4 * dt));
          e.rig.group.rotation.y += dt * 0.35;
        }

        // integrate with wall sliding
        const nx = e.pos.x + e.vel.x * dt, nz = e.pos.z + e.vel.z * dt;
        if (!blocked(nx, e.pos.z, e.spec.radius)) e.pos.x = nx; else e.vel.x = 0;
        if (!blocked(e.pos.x, nz, e.spec.radius)) e.pos.z = nz; else e.vel.z = 0;

        // pose
        e.bob += dt * (1.5 + e.vel.length() * 1.4);
        if (e.spec.flying) {
          e.pos.y = 0.85 + Math.sin(e.bob) * 0.18;
          if (e.rig.limbs[0] && e.rig.limbs[0].ring) e.rig.limbs[0].ring.rotation.z += dt * 3.4;
        } else {
          e.pos.y = 0;
          const swing = Math.sin(e.bob * 2.4) * Math.min(0.6, e.vel.length() * 0.22);
          for (const l of e.rig.limbs) {
            if (!l.arm) continue;
            l.arm.rotation.x = swing * l.side;
            l.leg.rotation.x = -swing * l.side;
          }
        }
        e.rig.group.position.set(e.pos.x, e.pos.y, e.pos.z);

        // hit flash tints the emissive
        e.rig.materials[0].emissive = new THREE.Color(0xff2222);
        e.rig.materials[0].emissiveIntensity = e.hitFlash * 1.6;
      }

      stepProjectiles(dt, playerPos);

      // sweep out finished corpses
      for (let i = enemies.length - 1; i >= 0; i--) {
        const e = enemies[i];
        if (e.dead && e.deathT > 1.4) {
          scene.remove(e.rig.group);
          enemies.splice(i, 1);
        }
      }
    }

    /* ---------- enemy projectiles ---------- */
    const projGeo = new THREE.SphereGeometry(0.13, 8, 8);

    function fireProjectile(e, playerPos) {
      const mat = new THREE.MeshBasicMaterial({ color: e.spec.glow });
      const m = new THREE.Mesh(projGeo, mat);
      const from = new THREE.Vector3(e.pos.x, e.pos.y + e.spec.height * 0.62, e.pos.z);
      m.position.copy(from);
      scene.add(m);
      const l = new THREE.PointLight(e.spec.glow, 1.1, 6, 2);
      m.add(l);

      const aim = playerPos.clone();
      aim.y += 1.2;
      aim.x += (Math.random() - 0.5) * 1.4;
      aim.y += (Math.random() - 0.5) * 0.7;
      const dir = aim.sub(from).normalize();
      projectiles.push({ mesh: m, dir, speed: 24, life: 3, damage: e.spec.damage });
      SF.audio.sfx.enemyShot();
    }

    function stepProjectiles(dt, playerPos) {
      for (let i = projectiles.length - 1; i >= 0; i--) {
        const p = projectiles[i];
        p.life -= dt;
        p.mesh.position.addScaledVector(p.dir, p.speed * dt);

        const hitPlayer = p.mesh.position.distanceTo(
          new THREE.Vector3(playerPos.x, playerPos.y + 1.2, playerPos.z)) < 0.65;
        const hitWall = blocked(p.mesh.position.x, p.mesh.position.z, 0.1) &&
                        p.mesh.position.y < 4;

        if (hitPlayer) ctx.onPlayerHit(p.damage, p.mesh.position);
        if (hitPlayer || hitWall || p.life <= 0) {
          scene.remove(p.mesh);
          projectiles.splice(i, 1);
        }
      }
    }

    /* Line of sight from the player's eye to an enemy's chest. */
    function visible(from, e) {
      const to = bodyCentre(e);
      const dir = to.clone().sub(from);
      const dist = dir.length();
      dir.normalize();
      return rayWalls(from, dir, dist, level.colliders).dist >= dist - 0.4;
    }

    return {
      enemies, spawn, step, damage, pulse, raycast, visible, TYPES,
      get alive() { return enemies.filter((e) => !e.dead).length; },
      clear() {
        for (const e of enemies) scene.remove(e.rig.group);
        enemies.length = 0;
        for (const p of projectiles) scene.remove(p.mesh);
        projectiles.length = 0;
      }
    };
  }

  SF.ai = { create, TYPES };
})(window.SF);
