/* ============================================================
   Arena geometry: procedural PBR-ish materials baked to canvas at load
   time (no texture assets, same "nothing to download" approach as the
   rest of the repo, in a smaller form), a walled arena with scattered
   cover, and the AABB collider list the player and AI resolve against.
   ============================================================ */
(function (DB) {
  'use strict';

  const texCache = new Map();

  function noiseCanvas(size, base, variance) {
    const cv = document.createElement('canvas');
    cv.width = cv.height = size;
    const ctx = cv.getContext('2d');
    const img = ctx.createImageData(size, size);
    for (let i = 0; i < img.data.length; i += 4) {
      const n = (Math.random() - 0.5) * variance;
      img.data[i] = DB.util.clamp(base[0] + n, 0, 255);
      img.data[i + 1] = DB.util.clamp(base[1] + n, 0, 255);
      img.data[i + 2] = DB.util.clamp(base[2] + n, 0, 255);
      img.data[i + 3] = 255;
    }
    ctx.putImageData(img, 0, 0);
    // a handful of soft streaks so it doesn't read as flat static
    ctx.globalAlpha = 0.10;
    for (let i = 0; i < 10; i++) {
      const x = Math.random() * size, w = 8 + Math.random() * 30;
      ctx.fillStyle = Math.random() > 0.5 ? '#000' : '#fff';
      ctx.fillRect(x, 0, w, size);
    }
    ctx.globalAlpha = 1;
    return cv;
  }

  function normalFromNoise(cv, strength) {
    const size = cv.width;
    const src = cv.getContext('2d').getImageData(0, 0, size, size).data;
    const out = document.createElement('canvas');
    out.width = out.height = size;
    const img = out.getContext('2d').createImageData(size, size);
    const h = function (x, y) {
      const xi = (x + size) % size, yi = (y + size) % size;
      return src[(yi * size + xi) * 4] / 255;
    };
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        const dx = h(x + 1, y) - h(x - 1, y);
        const dy = h(x, y + 1) - h(x, y - 1);
        const nx = -dx * strength, ny = -dy * strength, nz = 1;
        const len = Math.hypot(nx, ny, nz);
        const i = (y * size + x) * 4;
        img.data[i] = ((nx / len) * 0.5 + 0.5) * 255;
        img.data[i + 1] = ((ny / len) * 0.5 + 0.5) * 255;
        img.data[i + 2] = ((nz / len) * 0.5 + 0.5) * 255;
        img.data[i + 3] = 255;
      }
    }
    out.getContext('2d').putImageData(img, 0, 0);
    return out;
  }

  function proceduralMaterial(key, base, opts) {
    if (texCache.has(key)) return texCache.get(key);
    opts = opts || {};
    const albedoCv = noiseCanvas(128, base, opts.variance || 14);
    const normalCv = normalFromNoise(albedoCv, opts.normalStrength || 1.4);
    const albedo = new THREE.CanvasTexture(albedoCv);
    const normal = new THREE.CanvasTexture(normalCv);
    [albedo, normal].forEach(function (t) {
      t.wrapS = t.wrapT = THREE.RepeatWrapping;
      t.repeat.set(opts.repeat || 4, opts.repeat || 4);
    });
    albedo.encoding = THREE.sRGBEncoding;
    const mat = new THREE.MeshStandardMaterial({
      map: albedo, normalMap: normal,
      roughness: opts.roughness === undefined ? 0.75 : opts.roughness,
      metalness: opts.metalness === undefined ? 0.15 : opts.metalness,
      emissive: opts.emissive ? new THREE.Color(opts.emissive) : undefined,
      emissiveIntensity: opts.emissiveIntensity || 0
    });
    texCache.set(key, mat);
    return mat;
  }

  /* A walled square arena with scattered box cover. `theme` picks the
     palette: 'tutorial' (clean, pale) or 'throne' (violet-black, hostile). */
  function build(scene, opts) {
    opts = opts || {};
    const half = opts.half || 40;
    const theme = opts.theme || 'throne';
    const wallHeight = 7;

    const palette = theme === 'tutorial'
      ? { floor: [58, 62, 70], wall: [40, 44, 52], crate: [70, 74, 82], emissive: 0x3a6ea8 }
      : { floor: [26, 18, 34], wall: [34, 20, 44], crate: [42, 26, 54], emissive: 0x7a2ea8 };

    const group = new THREE.Group();
    const colliders = [];

    const floorMat = proceduralMaterial(theme + ':floor', palette.floor, { repeat: half / 3, roughness: 0.85 });
    const floor = new THREE.Mesh(new THREE.PlaneGeometry(half * 2, half * 2), floorMat);
    floor.rotation.x = -Math.PI / 2;
    floor.receiveShadow = true;
    group.add(floor);

    const wallMat = proceduralMaterial(theme + ':wall', palette.wall, {
      repeat: 6, roughness: 0.6, metalness: 0.3, emissive: palette.emissive, emissiveIntensity: 0.05
    });
    const wallGeo = new THREE.BoxGeometry(1, wallHeight, 1);
    function addWall(cx, cz, sx, sz) {
      const m = new THREE.Mesh(wallGeo, wallMat);
      m.position.set(cx, wallHeight / 2, cz);
      m.scale.set(sx, 1, sz);
      m.castShadow = m.receiveShadow = true;
      group.add(m);
      colliders.push({
        min: new THREE.Vector3(cx - sx / 2, 0, cz - sz / 2),
        max: new THREE.Vector3(cx + sx / 2, wallHeight, cz + sz / 2),
        top: wallHeight, bottom: 0
      });
    }
    const t = 1.2;
    addWall(0, -half, half * 2, t);
    addWall(0, half, half * 2, t);
    addWall(-half, 0, t, half * 2);
    addWall(half, 0, t, half * 2);

    const crateMat = proceduralMaterial(theme + ':crate', palette.crate, { repeat: 2, roughness: 0.7, metalness: 0.2 });
    const crateGeo = new THREE.BoxGeometry(1, 1, 1);
    const crateCount = opts.crates === undefined ? 14 : opts.crates;
    for (let i = 0; i < crateCount; i++) {
      const s = 1.4 + Math.random() * 1.6;
      const h = 1.0 + Math.random() * 1.6;
      let x, z, tries = 0;
      do {
        x = (Math.random() * 2 - 1) * (half - 4);
        z = (Math.random() * 2 - 1) * (half - 4);
        tries++;
      } while (Math.hypot(x, z) < 8 && tries < 20);
      const m = new THREE.Mesh(crateGeo, crateMat);
      m.position.set(x, h / 2, z);
      m.scale.set(s, h, s);
      m.rotation.y = Math.random() * Math.PI;
      m.castShadow = m.receiveShadow = true;
      group.add(m);
      colliders.push({
        min: new THREE.Vector3(x - s / 2, 0, z - s / 2),
        max: new THREE.Vector3(x + s / 2, h, z + s / 2),
        top: h, bottom: 0
      });
    }

    scene.add(group);

    function resolveSpawn(pos, radius) {
      for (const c of colliders) {
        if (c.top <= 0.4) continue;
        const minX = c.min.x - radius, maxX = c.max.x + radius;
        const minZ = c.min.z - radius, maxZ = c.max.z + radius;
        if (pos.x > minX && pos.x < maxX && pos.z > minZ && pos.z < maxZ) {
          const dxLeft = pos.x - minX, dxRight = maxX - pos.x;
          const dzUp = pos.z - minZ, dzDown = maxZ - pos.z;
          const m = Math.min(dxLeft, dxRight, dzUp, dzDown);
          if (m === dxLeft) pos.x = minX; else if (m === dxRight) pos.x = maxX;
          else if (m === dzUp) pos.z = minZ; else pos.z = maxZ;
        }
      }
      return pos;
    }

    return {
      group: group, colliders: colliders,
      half: half, floorY: 0,
      playerStart: new THREE.Vector3(0, 0, half - 6),
      spawnPoint: function (index, total, radius) {
        const angle = (index / Math.max(1, total)) * Math.PI * 2 + Math.PI;
        const p = new THREE.Vector3(Math.sin(angle) * radius, 0, Math.cos(angle) * radius);
        return resolveSpawn(p, 0.6);
      },
      resolveSpawn: resolveSpawn,
      dispose: function () {
        scene.remove(group);
        group.traverse(function (o) { if (o.geometry) o.geometry.dispose(); });
      }
    };
  }

  DB.level = { build: build };
})(window.DB || (window.DB = {}));
