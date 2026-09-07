/* ============================================================
   The player's hitscan weapon and Hollow ability. A weapon fires a ray
   against the level's colliders and every live hostile's head/body
   spheres, taking whichever is closer — same technique as the native
   C++ build, in JS. The ability is a cooldown timer only; what it
   actually does (freeze pulse, drain cone, dash) is applied by
   game.js, which is the one place that already has the player, the
   level and the hostiles all in scope together.
   ============================================================ */
(function (DB) {
  'use strict';

  function raySphere(origin, dir, centre, radius) {
    const oc = centre.clone().sub(origin);
    const t = oc.dot(dir);
    if (t < 0) return -1;
    const d2 = oc.lengthSq() - t * t;
    const r2 = radius * radius;
    if (d2 > r2) return -1;
    return t - Math.sqrt(r2 - d2);
  }

  function rayAABB(origin, dir, c) {
    let tmin = 0, tmax = 1e6;
    const axes = ['x', 'y', 'z'];
    for (let i = 0; i < 3; i++) {
      const a = axes[i];
      const o = origin[a], d = dir[a];
      const lo = c.min[a], hi = c.max[a];
      if (Math.abs(d) < 1e-8) {
        if (o < lo || o > hi) return 1e6;
        continue;
      }
      const inv = 1 / d;
      let t0 = (lo - o) * inv, t1 = (hi - o) * inv;
      if (t0 > t1) { const tmp = t0; t0 = t1; t1 = tmp; }
      tmin = Math.max(tmin, t0);
      tmax = Math.min(tmax, t1);
      if (tmin > tmax) return 1e6;
    }
    return tmin;
  }

  function spreadDir(dir, maxDegrees) {
    if (maxDegrees <= 0) return dir.clone();
    const helper = Math.abs(dir.y) < 0.99 ? new THREE.Vector3(0, 1, 0) : new THREE.Vector3(1, 0, 0);
    const right = dir.clone().cross(helper).normalize();
    const up = right.clone().cross(dir).normalize();
    const rMax = THREE.MathUtils.degToRad(maxDegrees);
    const a = (Math.random() * 2 - 1) * rMax;
    const b = (Math.random() * 2 - 1) * rMax;
    return dir.clone().addScaledVector(right, a).addScaledVector(up, b).normalize();
  }

  function createWeapon(def) {
    const w = {
      def: def,
      magSize: def.magSize, ammoInMag: def.magSize, reserveAmmo: def.reserveAmmo,
      damage: def.damage, headshotMultiplier: def.headshotMultiplier,
      fireInterval: def.fireInterval, reloadTime: def.reloadTime,
      spreadDegrees: def.spreadDegrees || 0,
      cooldown: 0, reloadT: 0, reloading: false,
      update: function (dt) {
        w.cooldown = Math.max(0, w.cooldown - dt);
        if (w.reloading) {
          w.reloadT -= dt;
          if (w.reloadT <= 0) {
            w.reloading = false;
            const need = w.magSize - w.ammoInMag;
            const take = Math.min(need, w.reserveAmmo);
            w.ammoInMag += take;
            w.reserveAmmo -= take;
          }
        }
      },
      startReload: function () {
        if (w.reloading || w.ammoInMag === w.magSize || w.reserveAmmo <= 0) return;
        w.reloading = true;
        w.reloadT = w.reloadTime;
      },
      canFire: function () { return !w.reloading && w.cooldown <= 0 && w.ammoInMag > 0; },
      fire: function (origin, dir, level, hostiles) {
        if (!w.canFire()) return null;
        w.cooldown = w.fireInterval;
        w.ammoInMag--;

        const d = w.spreadDegrees > 0 ? spreadDir(dir, w.spreadDegrees) : dir;
        let wallDist = 1e6;
        for (const c of level.colliders) wallDist = Math.min(wallDist, rayAABB(origin, d, c));

        let bestIndex = -1, bestDist = wallDist, bestHead = false;
        for (let i = 0; i < hostiles.length; i++) {
          const h = hostiles[i];
          if (!h.blocksShots()) continue;
          const headC = h.headCentre(), bodyC = h.bodyCentre();
          const hd = raySphere(origin, d, headC, h.type.radius * 0.6);
          const bd = raySphere(origin, d, bodyC, h.type.radius * 1.05);
          const isHead = hd >= 0 && (bd < 0 || hd <= bd);
          const dist = isHead ? hd : bd;
          if (dist < 0 || dist >= bestDist) continue;
          bestDist = dist; bestIndex = i; bestHead = isHead;
        }

        if (bestIndex >= 0) {
          return {
            hitHostile: true, hostileIndex: bestIndex, headshot: bestHead,
            damage: w.damage * (bestHead ? w.headshotMultiplier : 1)
          };
        }
        return { hitHostile: false, hitWall: wallDist < 1e5 };
      }
    };
    return w;
  }

  function createAbilityTimer(def) {
    return {
      def: def, cooldownT: 0,
      update: function (dt) { this.cooldownT = Math.max(0, this.cooldownT - dt); },
      ready: function () { return this.cooldownT <= 0; },
      trigger: function () { this.cooldownT = def.cooldown; },
      get fraction() { return def.cooldown > 0 ? 1 - this.cooldownT / def.cooldown : 1; }
    };
  }

  DB.weapons = { createWeapon: createWeapon, createAbilityTimer: createAbilityTimer, rayAABB: rayAABB, raySphere: raySphere };
})(window.DB || (window.DB = {}));
