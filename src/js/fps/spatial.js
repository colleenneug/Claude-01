/* ============================================================
   Spatial hash for collision candidates.

   The ship has a few hundred colliders and a linear scan was fine. A
   patrol zone has thousands, and every hostile tests them every frame,
   so the scan becomes the frame budget. This buckets colliders into a
   grid once at build time and hands back only the ones near a point.
   ============================================================ */
(function (SF) {
  'use strict';

  function create(colliders, cellSize) {
    const cell = cellSize || 16;
    const buckets = new Map();
    const key = (cx, cz) => cx + ',' + cz;

    for (const c of colliders) {
      const x0 = Math.floor(c.min.x / cell), x1 = Math.floor(c.max.x / cell);
      const z0 = Math.floor(c.min.z / cell), z1 = Math.floor(c.max.z / cell);
      for (let cx = x0; cx <= x1; cx++) {
        for (let cz = z0; cz <= z1; cz++) {
          const k = key(cx, cz);
          let b = buckets.get(k);
          if (!b) { b = []; buckets.set(k, b); }
          b.push(c);
        }
      }
    }

    const scratch = [];

    /* Colliders in the cells overlapping a point, padded by `pad`. */
    function near(x, z, pad) {
      scratch.length = 0;
      const p = pad || 0;
      const x0 = Math.floor((x - p) / cell), x1 = Math.floor((x + p) / cell);
      const z0 = Math.floor((z - p) / cell), z1 = Math.floor((z + p) / cell);
      for (let cx = x0; cx <= x1; cx++) {
        for (let cz = z0; cz <= z1; cz++) {
          const b = buckets.get(key(cx, cz));
          if (!b) continue;
          for (const c of b) if (scratch.indexOf(c) === -1) scratch.push(c);
        }
      }
      return scratch;
    }

    /* Colliders along a segment, for line-of-sight and bullet tests. Walks
       the cells the ray passes through rather than the whole world. */
    function alongRay(ox, oz, dx, dz, dist) {
      const out = [];
      const steps = Math.max(1, Math.ceil(dist / (cell * 0.7)));
      for (let i = 0; i <= steps; i++) {
        const t = (i / steps) * dist;
        const list = near(ox + dx * t, oz + dz * t, cell * 0.5);
        for (const c of list) if (out.indexOf(c) === -1) out.push(c);
      }
      return out;
    }

    return { near, alongRay, cell, get size() { return buckets.size; } };
  }

  SF.spatial = { create };
})(window.SF);
