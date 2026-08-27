/* ============================================================
   Light pool.

   three keys its shader programs partly on how many lights are in the
   scene, so adding or removing one forces every material in the scene
   to recompile. Doing that per bullet impact — eight times a shotgun
   shell — stalls the frame badly.

   So the scene gets a fixed number of point lights, once, and never
   any more. Emitters (ceiling strips, muzzle impacts, enemy halos)
   are just data; each frame the nearest ones are assigned to the
   pooled lights. Constant light count, no recompiles, and only a
   handful of lights to evaluate per fragment.
   ============================================================ */
(function (SF) {
  'use strict';

  function create(scene, poolSize) {
    const size = poolSize || 8;
    const pool = [];
    for (let i = 0; i < size; i++) {
      const l = new THREE.PointLight(0xffffff, 0, 10, 2);
      l.position.set(0, -1000, 0);
      scene.add(l);
      pool.push(l);
    }

    const emitters = [];      // every light in the world, as plain data

    function make(x, y, z, colour, intensity, distance, opts) {
      const e = Object.assign({
        x: x, y: y, z: z,
        colour: colour, intensity: intensity, distance: distance,
        ttl: 0, life: 0, dynamic: false, active: true, dead: false
      }, opts || {});
      emitters.push(e);
      return e;
    }

    const api = {
      /* Fixtures built into the level. */
      addStatic(x, y, z, colour, intensity, distance) {
        return make(x, y, z, colour, intensity, distance);
      },

      /* A brief flash — impacts, muzzle sparks. Expires on its own. */
      flash(x, y, z, colour, intensity, distance, ttl) {
        const e = make(x, y, z, colour, intensity, distance,
                       { dynamic: true, ttl: ttl, life: ttl });
        return e;
      },

      /* A light that follows something for as long as it lives. Pass
         { dynamic: false } for something that is effectively a fixture, so
         it competes on distance instead of jumping the queue. */
      attach(colour, intensity, distance, opts) {
        const e = make(0, -1000, 0, colour, intensity, distance,
                       { dynamic: !(opts && opts.dynamic === false) });
        return {
          set(x, y, z) { e.x = x; e.y = y; e.z = z; },
          setIntensity(v) { e.intensity = v; },
          release() { e.dead = true; }
        };
      },

      /* Rank every live emitter and hand the best ones a real light. */
      update(dt, viewPos) {
        for (let i = emitters.length - 1; i >= 0; i--) {
          const e = emitters[i];
          if (e.ttl > 0) {
            e.life -= dt;
            if (e.life <= 0) e.dead = true;
          }
          if (e.dead) emitters.splice(i, 1);
        }

        for (const e of emitters) {
          const dx = e.x - viewPos.x, dy = e.y - viewPos.y, dz = e.z - viewPos.z;
          e._d = Math.sqrt(dx * dx + dy * dy + dz * dz);
          // a flash right next to the player matters more than a distant fixture
          e._score = e._d - (e.dynamic ? 22 : 0);
        }
        emitters.sort((a, b) => a._score - b._score);

        for (let i = 0; i < size; i++) {
          const l = pool[i];
          const e = emitters[i];
          if (!e || e._d > e.distance + 6) { l.intensity = 0; continue; }
          l.position.set(e.x, e.y, e.z);
          l.color.set(e.colour);
          l.distance = e.distance;
          // flashes fade out over their lifetime
          l.intensity = e.ttl > 0 ? e.intensity * Math.max(0, e.life / e.ttl) : e.intensity;
        }
      },

      get count() { return emitters.length; },
      get poolSize() { return size; }
    };

    return api;
  }

  SF.lights = { create };
})(window.SF);
