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
    /* ---- the ark's garrison: what the Pale made of the crew ---- */
    drone: {
      name: 'SEEKER MOTE', hp: 60, speed: 3.2, damage: 9, range: 2.2, rate: 1.1,
      colour: 0x8a7a4a, glow: 0xffd27a, height: 1.5, radius: 0.45, flying: true, xp: 20
    },
    thrall: {
      name: 'ACOLYTE OF THE PALE', hp: 85, speed: 2.7, damage: 13, range: 2.0, rate: 1.4,
      colour: 0x6a6558, glow: 0xfff0c0, height: 1.85, radius: 0.42, xp: 26
    },
    warden: {
      name: 'SUNGUARD FRAME', hp: 240, speed: 2.1, damage: 20, range: 26, rate: 2.1,
      colour: 0x5a5648, glow: 0xffc04a, height: 2.5, radius: 0.75, ranged: true, xp: 90
    },

    /* ---- the blessed dead, and the thing that keeps rebuilding them ----
       A palebearer dropped to zero does not die: it goes down, its spark
       comes out, and it is back on its feet in REVIVE_TIME unless you kill
       the spark first. See damage() and the downed branch in step(). */
    palebearer: {
      name: 'PALEBEARER', sub: 'BLESSED / WILL NOT STAY DOWN', hp: 190, speed: 2.8, damage: 18,
      range: 24, rate: 1.7, colour: 0x7a6a48, glow: 0xfff4d8, height: 1.95, radius: 0.5,
      ranged: true, bearer: true, xp: 110
    },
    spark: {
      name: 'SPARK', sub: 'FRAGMENT OF THE FIRST LIGHT', hp: 15, speed: 7.5, damage: 0,
      range: 0, rate: 9, colour: 0xf0e4c0, glow: 0xffffff, height: 0.5, radius: 0.22,
      flying: true, spark: true, xp: 45
    },

    /* ---- Thresher's Reach: the Pale Order, still holding a dead station ---- */
    scarab: {
      name: 'CENSER', sub: 'PALE ORDER / DRONE', hp: 46, speed: 3.7, damage: 8, range: 2.0,
      rate: 0.95, colour: 0xa8763a, glow: 0xffc46a, height: 1.3, radius: 0.4, flying: true, xp: 18
    },
    marauder: {
      name: 'ORDINAL', sub: 'PALE ORDER / SWORN', hp: 115, speed: 2.5, damage: 15,
      range: 22, rate: 1.9, colour: 0x8a6a44, glow: 0xffb454, height: 1.95, radius: 0.5,
      ranged: true, xp: 40
    },
    colossus: {
      name: 'RELIQUARY FRAME', sub: 'PALE ORDER / ELITE', hp: 430, speed: 1.8, damage: 26,
      range: 26, rate: 2.0, colour: 0x6a5236, glow: 0xffd27a, height: 2.9, radius: 0.95,
      ranged: true, elite: true, xp: 190
    },

    /* ---- Cold Lantern: the Lamplit, keeping a relay warm for nobody ---- */
    mote: {
      name: 'EMBER', sub: 'THE LAMPLIT / DRONE', hp: 40, speed: 3.9, damage: 7, range: 1.9,
      rate: 0.9, colour: 0xbba87f, glow: 0xffeec2, height: 1.25, radius: 0.38, flying: true, xp: 17
    },
    revenant: {
      name: 'LAMPLIGHTER', sub: 'THE LAMPLIT / SWORN', hp: 125, speed: 2.6, damage: 16, range: 2.2,
      rate: 1.3, colour: 0x8c8060, glow: 0xfff4d8, height: 1.9, radius: 0.45, xp: 38
    },
    hoarfrost: {
      name: 'BEACON WARDEN', sub: 'THE LAMPLIT / ELITE', hp: 470, speed: 1.9, damage: 24,
      range: 24, rate: 1.9, colour: 0x807456, glow: 0xffe08a, height: 2.9, radius: 0.95,
      ranged: true, elite: true, xp: 200
    },

    conductor: {
      name: 'THE FIRST LIGHT', hp: 900, speed: 2.4, damage: 26, range: 30, rate: 1.6,
      colour: 0x6b5a35, glow: 0xfff0b0, height: 3.0, radius: 0.9, ranged: true, boss: true, xp: 320
    }
  };

  /* How long a downed palebearer takes to come back if you leave its spark alone. */
  const REVIVE_TIME = 5.0;
  /* A hit on a frozen target shatters it instead of merely hurting it. */
  const SHATTER_MULT = 2.4;
  const SHATTER_RADIUS = 4.5;
  const SHATTER_SPLASH = 34;
  const FROZEN_TINT = 0x9d7bff;

  let uidSeq = 0;

  function create(ctx) {
    const { scene, level, lights } = ctx;
    const enemies = [];
    const projectiles = [];
    const tmp = new THREE.Vector3();
    /* In co-op only the host simulates hostiles; everyone else renders the
       host's snapshots. Proxies keep full hit geometry so shooting them works
       exactly the same — the damage is just reported to the host to apply. */
    let remote = false;

    /* ---------- construction ---------- */
    function buildRig(spec) {
      const g = new THREE.Group();
      const skin = new THREE.MeshStandardMaterial({
        color: spec.colour, metalness: 0.7, roughness: 0.55,
        emissive: new THREE.Color(0xff2222), emissiveIntensity: 0
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

      for (const c of g.children) { c.castShadow = true; c.receiveShadow = true; }
      // the halo is a pooled light, not a child light: adding one per enemy
      // would change the scene light count and recompile every shader
      const halo = lights.attach(spec.glow, 1.1, 8);
      return { group: g, limbs, head, halo, materials: [skin, glow] };
    }

    function spawn(typeId, x, z) {
      const spec = TYPES[typeId];
      const rig = buildRig(spec);
      rig.group.position.set(x, 0, z);
      scene.add(rig.group);

      const e = {
        /* Every hostile needs a stable identity: co-op snapshots and the
           damage a client reports back are both keyed on it. */
        uid: 'e' + (++uidSeq),
        type: typeId, spec, rig,
        pos: new THREE.Vector3(x, 0, z),
        vel: new THREE.Vector3(),
        hp: spec.hp, maxHp: spec.hp,
        cool: Math.random() * spec.rate,
        stun: 0, dead: false, deathT: 0,
        hitFlash: 0, bob: Math.random() * 6.28,
        alerted: false,
        /* Cipher status: held in place, and shatters on the next hit. */
        frozen: 0,
        /* Palebearer status: seconds left before it gets back up, its spark's
           uid while it is down, and whether its one resurrection is spent. */
        downed: 0, sparkUid: null, revived: false,
        /* Sparks carry a back-reference to the body they are rebuilding. */
        bearer: null,
        baseColour: spec.colour
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
      const list = (level.space && maxDist > 0)
        ? level.space.alongRay(origin.x, origin.z, dir.x, dir.z, maxDist)
        : colliders;
      for (const c of list) {
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
        if (e.dead || e.downed > 0) continue;
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

    /* Frozen bodies shatter rather than absorb, and take their neighbours
       with them. The guard stops one shatter cascading through a whole
       frozen group and back into itself. */
    let shattering = false;
    function shatterBurst(from) {
      if (shattering) return;
      shattering = true;
      for (const o of enemies) {
        if (o === from || o.dead || o.downed > 0) continue;
        if (o.pos.distanceTo(from.pos) > SHATTER_RADIUS) continue;
        damage(o, SHATTER_SPLASH, null);
      }
      shattering = false;
    }

    function thaw(e) {
      e.frozen = 0;
      e.rig.materials[0].color.set(e.baseColour);
    }

    /* Hold everything in a radius still. The Hollow ability calls this after
       pulse(), so the damage and the freeze land together. */
    function freeze(centre, radius, duration) {
      let n = 0;
      for (const e of enemies) {
        if (e.dead || e.downed > 0 || e.spec.boss) continue;
        if (e.pos.distanceTo(centre) > radius) continue;
        e.frozen = duration;
        e.vel.set(0, 0, 0);
        e.rig.materials[0].color.set(FROZEN_TINT);
        n++;
      }
      return n;
    }

    /* The spark is dead, so the body it was rebuilding is finally dead too. */
    function finaliseBearer(spark) {
      const b = spark.bearer ? byUid(spark.bearer) : null;
      if (!b || b.dead) return;
      b.downed = 0;
      b.sparkUid = null;
      b.dead = true;
      b.deathT = 0;
      b.rig.halo.setIntensity(0);
      ctx.onKill(b);
    }

    function spawnSpark(bearer) {
      const s = spawn('spark', bearer.pos.x, bearer.pos.z);
      s.pos.y = bearer.spec.height * 0.9;
      s.bearer = bearer.uid;
      s.alerted = true;
      bearer.sparkUid = s.uid;
      return s;
    }

    function damage(e, amount, dir) {
      if (e.dead || e.downed > 0) return false;
      /* A shielded target takes nothing. The boss's shield comes down by
         solving its phrase, not by shooting it. */
      if (e.shielded) {
        e.hitFlash = 0.5;
        SF.audio.sfx.dryfire();
        return false;
      }
      let dealt = amount;
      let shattered = false;
      if (e.frozen > 0) {
        dealt *= SHATTER_MULT;
        shattered = true;
        thaw(e);
      }

      e.hp -= dealt;
      e.hitFlash = 1;
      e.alerted = true;
      if (dir) { e.vel.addScaledVector(dir, 1.4 / (e.spec.radius * 3)); }
      SF.audio.sfx.impact();
      if (shattered) {
        SF.audio.sfx.shatter();
        shatterBurst(e);
        if (ctx.onShatter) ctx.onShatter(e);
      }

      if (e.hp <= 0) {
        /* A palebearer's first death is not one. It goes down and its spark
           comes out; kill that inside REVIVE_TIME or it stands back up. */
        if (e.spec.bearer && !e.revived) {
          e.downed = REVIVE_TIME;
          e.hp = 0;
          e.vel.set(0, 0, 0);
          e.deathT = 0;
          e.rig.halo.setIntensity(0.5);
          spawnSpark(e);
          SF.audio.sfx.enemyDown();
          return false;
        }
        e.dead = true;
        e.deathT = 0;
        e.rig.halo.setIntensity(0);
        SF.audio.sfx.enemyDown();
        ctx.onKill(e);
        /* Killing the spark finishes whatever it was holding together. */
        if (e.spec.spark) finaliseBearer(e);
        return true;
      }
      return false;
    }

    /* Hollow's cipher pulse. */
    function pulse(centre, radius, dmg) {
      let n = 0;
      for (const e of enemies) {
        if (e.dead || e.downed > 0) continue;
        if (e.pos.distanceTo(centre) > radius) continue;
        e.stun = 3.2;
        n++;
        damage(e, dmg, null);
      }
      return n;
    }

    /* ---------- steering ---------- */
    function blocked(x, z, radius) {
      const candidates = level.space ? level.space.near(x, z, radius + 1) : level.colliders;
      for (const c of candidates) {
        if (c.top < 0.6) continue;
        if (x > c.min.x - radius && x < c.max.x + radius &&
            z > c.min.z - radius && z < c.max.z + radius) return true;
      }
      return false;
    }

    /* The spark finished its work: the body gets up, the spark is spent. */
    function reviveBearer(e) {
      e.downed = 0;
      e.revived = true;
      e.hp = Math.round(e.maxHp * 0.6);
      e.alerted = true;
      e.rig.group.rotation.x = 0;
      e.rig.halo.setIntensity(1.1);
      const s = e.sparkUid ? byUid(e.sparkUid) : null;
      if (s) retire(s);                     // consumed, not killed: no credit
      e.sparkUid = null;
      SF.audio.sfx.revive();
      if (ctx.onRevive) ctx.onRevive(e);
    }

    /* Sparks do not fight. They orbit the body they are rebuilding, fast and
       erratically, and they are small — that is the whole defence. */
    function stepSpark(e, dt, playerPos) {
      /* A free spark has no body to rebuild — it is loose in the room and
         running. The First Light's last phase uses one; see fps/boss.js. */
      if (e.sparkFree) { stepFreeSpark(e, dt, playerPos); return; }

      const b = e.bearer ? byUid(e.bearer) : null;
      if (!b || b.dead) { retire(e); return; }

      e.bob += dt * 3.2;
      const r = 1.1 + Math.sin(e.bob * 1.7) * 0.35;
      const ang = e.bob * 1.9;
      const tx = b.pos.x + Math.cos(ang) * r;
      const tz = b.pos.z + Math.sin(ang) * r;
      const ty = b.spec.height * 0.85 + Math.sin(e.bob * 2.3) * 0.22;

      e.pos.x += (tx - e.pos.x) * Math.min(1, 9 * dt);
      e.pos.z += (tz - e.pos.z) * Math.min(1, 9 * dt);
      e.pos.y += (ty - e.pos.y) * Math.min(1, 9 * dt);

      e.rig.group.position.set(e.pos.x, e.pos.y, e.pos.z);
      e.rig.group.rotation.y = Math.atan2(playerPos.x - e.pos.x, playerPos.z - e.pos.z) + Math.PI;
      if (e.rig.limbs[0] && e.rig.limbs[0].ring) e.rig.limbs[0].ring.rotation.z += dt * 9;
      e.rig.halo.set(e.pos.x, e.pos.y, e.pos.z);
      e.rig.materials[0].emissiveIntensity = e.hitFlash * 1.8;
    }

    /* Loose and evasive: a wide, drifting arc around its anchor that speeds up
       as it is hurt. Small, fast and unarmed — the difficulty is hitting it. */
    function stepFreeSpark(e, dt, playerPos) {
      const a = e.anchor || { x: e.pos.x, z: e.pos.z };
      const hurt = 1 - (e.hp / e.maxHp);
      e.bob += dt * (1.5 + hurt * 1.6);
      const r = 7 + Math.sin(e.bob * 0.8) * 3.5;
      const tx = a.x + Math.cos(e.bob) * r;
      const tz = a.z + Math.sin(e.bob * 1.31) * r;
      const ty = 1.5 + Math.sin(e.bob * 2.7) * 0.8;

      const k = Math.min(1, (2.6 + hurt * 2.2) * dt);
      e.pos.x += (tx - e.pos.x) * k;
      e.pos.z += (tz - e.pos.z) * k;
      e.pos.y += (ty - e.pos.y) * k;

      e.rig.group.position.set(e.pos.x, e.pos.y, e.pos.z);
      e.rig.group.rotation.y = Math.atan2(playerPos.x - e.pos.x, playerPos.z - e.pos.z) + Math.PI;
      if (e.rig.limbs[0] && e.rig.limbs[0].ring) e.rig.limbs[0].ring.rotation.z += dt * 12;
      e.rig.halo.set(e.pos.x, e.pos.y, e.pos.z);
      e.rig.materials[0].emissiveIntensity = e.hitFlash * 1.8;
    }

    function step(dt, playerPos, playerVisible) {
      if (remote) { stepProxies(dt); return; }
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

        /* Down but not out: the body lies there while its spark works. If the
           clock runs out it gets back up, once, at a fraction of its health. */
        if (e.downed > 0) {
          e.downed -= dt;
          const t = Math.min(1, (REVIVE_TIME - e.downed) / 0.6);
          e.rig.group.rotation.x = -t * 1.4;
          e.rig.group.position.set(e.pos.x, e.pos.y - t * 0.35, e.pos.z);
          if (e.downed <= 0) reviveBearer(e);
          continue;
        }

        /* Frozen: held in place, tinted, and primed to shatter. */
        if (e.frozen > 0) {
          e.frozen -= dt;
          if (e.frozen <= 0) {
            thaw(e);
          } else {
            e.vel.set(0, 0, 0);
            e.rig.group.position.set(e.pos.x, e.pos.y, e.pos.z);
            e.rig.halo.set(e.pos.x, e.pos.y + e.spec.height * 0.8, e.pos.z);
            e.rig.materials[0].emissiveIntensity = e.hitFlash * 1.8;
            continue;
          }
        }

        if (e.spec.spark) { stepSpark(e, dt, playerPos); continue; }

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

          const stopAt = e.spec.ranged ? Math.min(e.spec.range * 0.55, 14) : e.spec.range * 0.95;
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

        /* Never let a body reach the camera: at contact range its geometry
           clips through the near plane and fills the screen. Hold them at
           arm's length instead. */
        const sepX = e.pos.x - playerPos.x, sepZ = e.pos.z - playerPos.z;
        const sep = Math.hypot(sepX, sepZ);
        const minSep = e.spec.radius + 1.25;
        if (sep > 0.001 && sep < minSep) {
          const px = playerPos.x + (sepX / sep) * minSep;
          const pz = playerPos.z + (sepZ / sep) * minSep;
          if (!blocked(px, pz, e.spec.radius)) { e.pos.x = px; e.pos.z = pz; }
          e.vel.x = 0; e.vel.z = 0;
        }

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
        e.rig.halo.set(e.pos.x, e.pos.y + e.spec.height * 0.8, e.pos.z);

        // hit flash rides the emissive intensity; the colour is set once at build
        e.rig.materials[0].emissiveIntensity = e.hitFlash * 1.8;
      }

      stepProjectiles(dt, playerPos);

      // sweep out finished corpses
      for (let i = enemies.length - 1; i >= 0; i--) {
        const e = enemies[i];
        if (e.dead && e.deathT > 1.4) {
          e.rig.halo.release();
          scene.remove(e.rig.group);
          enemies.splice(i, 1);
        }
      }
    }

    /* ---------- enemy projectiles, pre-allocated ---------- */
    const PROJECTILES = 24;
    const projGeo = new THREE.SphereGeometry(0.13, 8, 8);
    const projFrom = new THREE.Vector3();
    const projAim = new THREE.Vector3();

    for (let i = 0; i < PROJECTILES; i++) {
      const mesh = new THREE.Mesh(projGeo, new THREE.MeshBasicMaterial({ color: 0xffffff }));
      mesh.visible = false;
      mesh.frustumCulled = false;
      scene.add(mesh);
      projectiles.push({
        mesh: mesh, dir: new THREE.Vector3(), speed: 24, life: 0, damage: 0,
        light: lights.attach(0xffffff, 0, 7)
      });
    }

    function fireProjectile(e, playerPos) {
      const p = projectiles.find((q) => q.life <= 0);
      if (!p) return;                       // every slot in flight; skip this shot

      projFrom.set(e.pos.x, e.pos.y + e.spec.height * 0.62, e.pos.z);
      p.mesh.position.copy(projFrom);
      p.mesh.material.color.set(e.spec.glow);
      p.mesh.visible = true;

      projAim.copy(playerPos);
      projAim.y += 1.2;
      projAim.x += (Math.random() - 0.5) * 1.4;
      projAim.y += (Math.random() - 0.5) * 0.7;
      p.dir.copy(projAim).sub(projFrom).normalize();
      p.life = 3;
      p.damage = e.spec.damage;
      p.light.setIntensity(1.6);
      SF.audio.sfx.enemyShot();
    }

    function stepProjectiles(dt, playerPos) {
      for (const p of projectiles) {
        if (p.life <= 0) continue;
        p.life -= dt;
        p.mesh.position.addScaledVector(p.dir, p.speed * dt);
        p.light.set(p.mesh.position.x, p.mesh.position.y, p.mesh.position.z);

        const dx = p.mesh.position.x - playerPos.x;
        const dy = p.mesh.position.y - (playerPos.y + 1.2);
        const dz = p.mesh.position.z - playerPos.z;
        const hitPlayer = (dx * dx + dy * dy + dz * dz) < 0.42;
        const hitWall = p.mesh.position.y < 4 &&
                        blocked(p.mesh.position.x, p.mesh.position.z, 0.1);

        if (hitPlayer) ctx.onPlayerHit(p.damage, p.mesh.position);
        if (hitPlayer || hitWall || p.life <= 0) {
          p.life = 0;
          p.mesh.visible = false;
          p.light.setIntensity(0);
        }
      }
    }

    /* ---------- co-op: snapshot in, snapshot out ---------- */

    /* What the host puts on the wire, kept small: id, type, position,
       facing, health and death. */
    function snapshot() {
      const out = [];
      for (const e of enemies) {
        out.push([e.uid, e.type,
                  +e.pos.x.toFixed(2), +e.pos.y.toFixed(2), +e.pos.z.toFixed(2),
                  +e.rig.group.rotation.y.toFixed(2),
                  Math.round(e.hp), e.dead ? 1 : 0,
                  e.frozen > 0 ? 1 : 0, e.downed > 0 ? 1 : 0]);
      }
      return out;
    }

    /* What a client does with it: spawn what is new, retire what is gone,
       and steer the rest toward where the host says they are. */
    function applySnapshot(list) {
      const seen = new Set();
      for (const row of list) {
        const [uid, type, x, y, z, ry, hp, dead, frozen, downed] = row;
        seen.add(uid);
        let e = enemies.find((q) => q.uid === uid);
        if (!e) {
          e = spawn(type, x, z);
          e.uid = uid;
          e.proxy = { x: x, y: y, z: z, ry: ry };
        }
        e.hp = hp;
        e.proxy.x = x; e.proxy.y = y; e.proxy.z = z; e.proxy.ry = ry;
        /* Status is display-only on a client: the host owns the timers, we
           just have to draw the tint and the collapse. */
        const wasFrozen = e.frozen > 0;
        e.frozen = frozen ? 1 : 0;
        if (frozen && !wasFrozen) e.rig.materials[0].color.set(FROZEN_TINT);
        if (!frozen && wasFrozen) e.rig.materials[0].color.set(e.baseColour);
        e.downed = downed ? 1 : 0;
        if (dead && !e.dead) { e.dead = true; e.deathT = 0; e.rig.halo.setIntensity(0); }
      }
      for (let i = enemies.length - 1; i >= 0; i--) {
        const e = enemies[i];
        if (seen.has(e.uid)) continue;
        e.rig.halo.release();
        scene.remove(e.rig.group);
        enemies.splice(i, 1);
      }
    }

    function stepProxies(dt) {
      for (let i = enemies.length - 1; i >= 0; i--) {
        const e = enemies[i];
        e.hitFlash = Math.max(0, e.hitFlash - dt * 4);
        e.rig.materials[0].emissiveIntensity = e.hitFlash * 1.8;

        if (e.dead) {
          e.deathT += dt;
          const t = Math.min(1, e.deathT / 0.9);
          e.rig.group.rotation.x = -t * 1.5;
          e.rig.group.position.y = -t * 0.5;
          for (const m of e.rig.materials) { m.transparent = true; m.opacity = 1 - t; }
          if (e.deathT > 1.4) { e.rig.halo.release(); scene.remove(e.rig.group); enemies.splice(i, 1); }
          continue;
        }

        if (e.downed > 0) {
          e.rig.group.rotation.x = -1.4;
          e.rig.group.position.set(e.pos.x, e.pos.y - 0.35, e.pos.z);
          continue;
        }
        e.rig.group.rotation.x = 0;

        const p = e.proxy;
        if (!p) continue;
        // ease toward the host's position rather than snapping to each packet
        e.pos.x += (p.x - e.pos.x) * Math.min(1, 11 * dt);
        e.pos.y += (p.y - e.pos.y) * Math.min(1, 11 * dt);
        e.pos.z += (p.z - e.pos.z) * Math.min(1, 11 * dt);
        e.rig.group.position.set(e.pos.x, e.pos.y, e.pos.z);

        let d = p.ry - e.rig.group.rotation.y;
        while (d > Math.PI) d -= Math.PI * 2;
        while (d < -Math.PI) d += Math.PI * 2;
        e.rig.group.rotation.y += d * Math.min(1, 12 * dt);

        e.bob += dt * 2.4;
        if (e.spec.flying && e.rig.limbs[0] && e.rig.limbs[0].ring) {
          e.rig.limbs[0].ring.rotation.z += dt * 3.4;
        }
        e.rig.halo.set(e.pos.x, e.pos.y + e.spec.height * 0.8, e.pos.z);
      }
    }

    const byUid = (uid) => enemies.find((e) => e.uid === uid) || null;

    /* Remove a hostile without a death: used when streaming retires one that
       has been left far behind. */
    function retire(e) {
      const i = enemies.indexOf(e);
      if (i === -1) return;
      e.rig.halo.release();
      scene.remove(e.rig.group);
      enemies.splice(i, 1);
    }

    /* Line of sight from the player's eye to an enemy's chest. */
    function visible(from, e) {
      const to = bodyCentre(e);
      const dir = to.clone().sub(from);
      const dist = dir.length();
      dir.normalize();
      return rayWalls(from, dir, dist, level.colliders).dist >= dist - 0.4;
    }

    /* Same reason as the weapon pools: pay for the projectile shader and
       geometry upload during loading, not on the first incoming shot. */
    function prewarm(renderer, sceneRef, cameraRef) {
      for (const p of projectiles) { p.mesh.position.set(0, -0.6, 0); p.mesh.visible = true; }
      renderer.compile(sceneRef, cameraRef);
      renderer.render(sceneRef, cameraRef);
      for (const p of projectiles) p.mesh.visible = false;
    }

    return {
      enemies, spawn, step, damage, pulse, freeze, raycast, visible, prewarm, TYPES,
      snapshot, applySnapshot, byUid, retire,
      setRemote(v) { remote = !!v; },
      get isRemote() { return remote; },
      get alive() { return enemies.filter((e) => !e.dead).length; },
      clear() {
        for (const e of enemies) { e.rig.halo.release(); scene.remove(e.rig.group); }
        enemies.length = 0;
        for (const p of projectiles) {
          p.life = 0; p.mesh.visible = false; p.light.release();
          scene.remove(p.mesh);
        }
        projectiles.length = 0;
      }
    };
  }

  SF.ai = { create, TYPES };
})(window.SF);
