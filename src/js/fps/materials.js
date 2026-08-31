/* ============================================================
   Procedural PBR materials.

   There are no texture assets in this project, so every surface is
   generated at load time on a 2D canvas: an albedo pass, a height pass
   that is converted to a normal map with a Sobel filter, and a
   roughness pass. That is what keeps the ship reading as metal and
   grating under a moving light instead of flat coloured boxes.
   ============================================================ */
(function (SF) {
  'use strict';

  const cache = new Map();

  function canvas(size) {
    const c = document.createElement('canvas');
    c.width = c.height = size;
    return c;
  }

  function texture(cv, repeat, srgb) {
    const t = new THREE.CanvasTexture(cv);
    t.wrapS = t.wrapT = THREE.RepeatWrapping;
    t.anisotropy = 8;
    if (srgb) t.encoding = THREE.sRGBEncoding;   // albedo only; data maps stay linear
    if (repeat) t.repeat.set(repeat[0], repeat[1]);
    return t;
  }

  /* Height noise has to be smoothed before the Sobel pass — per-pixel noise
     turns into rainbow speckle once it becomes a normal map. */
  function blur(cv, passes) {
    const ctx = cv.getContext('2d');
    ctx.filter = 'blur(1.1px)';
    for (let i = 0; i < (passes || 1); i++) ctx.drawImage(cv, 0, 0);
    ctx.filter = 'none';
    return cv;
  }

  /* Convert a greyscale height canvas into a tangent-space normal map. */
  function heightToNormal(heightCv, strength) {
    const size = heightCv.width;
    const src = heightCv.getContext('2d').getImageData(0, 0, size, size).data;
    const out = canvas(size);
    const img = out.getContext('2d').createImageData(size, size);
    const h = (x, y) => {
      const xi = (x + size) % size, yi = (y + size) % size;
      return src[(yi * size + xi) * 4] / 255;
    };
    for (let y = 0; y < size; y++) {
      for (let x = 0; x < size; x++) {
        // Sobel gradients
        const dx = (h(x + 1, y - 1) + 2 * h(x + 1, y) + h(x + 1, y + 1)) -
                   (h(x - 1, y - 1) + 2 * h(x - 1, y) + h(x - 1, y + 1));
        const dy = (h(x - 1, y + 1) + 2 * h(x, y + 1) + h(x + 1, y + 1)) -
                   (h(x - 1, y - 1) + 2 * h(x, y - 1) + h(x + 1, y - 1));
        const nx = -dx * strength, ny = -dy * strength, nz = 1;
        const len = Math.hypot(nx, ny, nz);
        const i = (y * size + x) * 4;
        img.data[i]     = ((nx / len) * 0.5 + 0.5) * 255;
        img.data[i + 1] = ((ny / len) * 0.5 + 0.5) * 255;
        img.data[i + 2] = ((nz / len) * 0.5 + 0.5) * 255;
        img.data[i + 3] = 255;
      }
    }
    out.getContext('2d').putImageData(img, 0, 0);
    return out;
  }

  function noise(ctx, size, amount, scale) {
    const img = ctx.getImageData(0, 0, size, size);
    for (let i = 0; i < img.data.length; i += 4) {
      const n = (Math.random() - 0.5) * amount;
      img.data[i] += n; img.data[i + 1] += n; img.data[i + 2] += n;
    }
    ctx.putImageData(img, 0, 0);
    void scale;
  }

  /* Streaks of grime running down a vertical surface. */
  function grime(ctx, size, count, alpha) {
    for (let i = 0; i < count; i++) {
      const x = Math.random() * size;
      const w = 2 + Math.random() * 14;
      const top = Math.random() * size * 0.6;
      const len = size * (0.2 + Math.random() * 0.7);
      const g = ctx.createLinearGradient(0, top, 0, top + len);
      g.addColorStop(0, `rgba(12,10,8,${alpha})`);
      g.addColorStop(1, 'rgba(12,10,8,0)');
      ctx.fillStyle = g;
      ctx.fillRect(x, top, w, len);
    }
  }

  /* ---------- hull plating: riveted panels with weld seams ---------- */
  function hullPlate(opts) {
    const o = Object.assign({ size: 512, cell: 128, base: '#3d4552', dark: '#2a303a',
                              rough: 0.62, metal: 0.85, repeat: [1, 1] }, opts);
    const S = o.size;

    const alb = canvas(S), a = alb.getContext('2d');
    a.fillStyle = o.base; a.fillRect(0, 0, S, S);

    // panel tint variation
    for (let y = 0; y < S; y += o.cell) {
      for (let x = 0; x < S; x += o.cell) {
        a.fillStyle = `rgba(255,255,255,${(Math.random() - 0.5) * 0.07})`;
        a.fillRect(x, y, o.cell, o.cell);
      }
    }
    // seams
    a.strokeStyle = o.dark; a.lineWidth = 3;
    for (let i = 0; i <= S; i += o.cell) {
      a.beginPath(); a.moveTo(i, 0); a.lineTo(i, S); a.stroke();
      a.beginPath(); a.moveTo(0, i); a.lineTo(S, i); a.stroke();
    }
    grime(a, S, 26, 0.34);
    noise(a, S, 18);

    const hgt = canvas(S), h = hgt.getContext('2d');
    h.fillStyle = '#8a8a8a'; h.fillRect(0, 0, S, S);
    h.strokeStyle = '#303030'; h.lineWidth = 4;
    for (let i = 0; i <= S; i += o.cell) {
      h.beginPath(); h.moveTo(i, 0); h.lineTo(i, S); h.stroke();
      h.beginPath(); h.moveTo(0, i); h.lineTo(S, i); h.stroke();
    }
    // rivets sit proud of the plate
    h.fillStyle = '#d8d8d8';
    for (let y = 0; y < S; y += o.cell) {
      for (let x = 0; x < S; x += o.cell) {
        for (const [rx, ry] of [[14, 14], [o.cell - 14, 14], [14, o.cell - 14], [o.cell - 14, o.cell - 14]]) {
          h.beginPath(); h.arc(x + rx, y + ry, 3.4, 0, Math.PI * 2); h.fill();
        }
      }
    }
    noise(h, S, 5);

    const rgh = canvas(S), r = rgh.getContext('2d');
    r.fillStyle = `rgb(${o.rough * 255 | 0},${o.rough * 255 | 0},${o.rough * 255 | 0})`;
    r.fillRect(0, 0, S, S);
    grime(r, S, 22, 0.5);
    noise(r, S, 40);

    return new THREE.MeshStandardMaterial({
      map: texture(alb, o.repeat, true),
      normalMap: texture(heightToNormal(blur(hgt, 2), 1.3), o.repeat),
      roughnessMap: texture(rgh, o.repeat),
      metalness: o.metal,
      roughness: 1.0,
      normalScale: new THREE.Vector2(0.8, 0.8)
    });
  }

  /* ---------- deck grating ---------- */
  function grating(opts) {
    const o = Object.assign({ size: 256, repeat: [6, 6] }, opts);
    const S = o.size, bar = 26, gap = 12;

    const alb = canvas(S), a = alb.getContext('2d');
    a.fillStyle = '#20242b'; a.fillRect(0, 0, S, S);
    a.fillStyle = '#5d646f';
    for (let x = 0; x < S; x += bar + gap) a.fillRect(x, 0, bar, S);
    for (let y = 0; y < S; y += (bar + gap) * 2) a.fillRect(0, y, S, 8);
    grime(a, S, 12, 0.4);
    noise(a, S, 22);

    const hgt = canvas(S), h = hgt.getContext('2d');
    h.fillStyle = '#101010'; h.fillRect(0, 0, S, S);
    h.fillStyle = '#e0e0e0';
    for (let x = 0; x < S; x += bar + gap) h.fillRect(x, 0, bar, S);
    for (let y = 0; y < S; y += (bar + gap) * 2) h.fillRect(0, y, S, 8);

    return new THREE.MeshStandardMaterial({
      map: texture(alb, o.repeat, true),
      normalMap: texture(heightToNormal(blur(hgt, 2), 1.8), o.repeat),
      metalness: 0.9, roughness: 0.55,
      normalScale: new THREE.Vector2(1.0, 1.0)
    });
  }

  /* ---------- painted bulkhead with hazard striping ---------- */
  function painted(color, opts) {
    const o = Object.assign({ size: 512, repeat: [1, 1], hazard: false }, opts);
    const S = o.size;
    const alb = canvas(S), a = alb.getContext('2d');
    a.fillStyle = color; a.fillRect(0, 0, S, S);
    if (o.hazard) {
      a.save();
      a.translate(0, S * 0.5); a.rotate(-Math.PI / 4);
      for (let i = -S; i < S * 2; i += 56) {
        a.fillStyle = i % 112 === 0 ? '#e0a63a' : '#1b1b1b';
        a.fillRect(i, -S, 28, S * 3);
      }
      a.restore();
    }
    // chipped paint revealing metal
    for (let i = 0; i < 90; i++) {
      const x = Math.random() * S, y = Math.random() * S, rr = 1 + Math.random() * 6;
      a.fillStyle = `rgba(70,74,82,${0.3 + Math.random() * 0.5})`;
      a.beginPath(); a.arc(x, y, rr, 0, Math.PI * 2); a.fill();
    }
    grime(a, S, 18, 0.32);
    noise(a, S, 16);

    const hgt = canvas(S), h = hgt.getContext('2d');
    h.fillStyle = '#909090'; h.fillRect(0, 0, S, S);
    noise(h, S, 8);

    return new THREE.MeshStandardMaterial({
      map: texture(alb, o.repeat, true),
      normalMap: texture(heightToNormal(blur(hgt, 2), 0.7), o.repeat),
      metalness: 0.25, roughness: 0.78
    });
  }

  /* ---------- glowing strip / screen ---------- */
  function emissive(color, intensity) {
    return new THREE.MeshStandardMaterial({
      color: 0x05070a,
      emissive: new THREE.Color(color),
      emissiveIntensity: intensity == null ? 2.2 : intensity,
      roughness: 0.4, metalness: 0.1
    });
  }

  /* Materials are shared across the whole level; build each one once. */
  function get(name) {
    if (cache.has(name)) return cache.get(name);
    let m;
    switch (name) {
      /* Every one of these repeats exactly once: the geometry sets the tiling
         now (see tiledBox), so a per-material repeat would multiply on top of
         it and the density would depend on which material a wall happened to
         use rather than on how big the wall is. */
      case 'hull':     m = hullPlate({ base: '#59636f', repeat: [1, 1] }); break;
      case 'hullDark': m = hullPlate({ base: '#3d4550', dark: '#252b33', rough: 0.7, repeat: [1, 1] }); break;
      case 'ceiling':  m = hullPlate({ base: '#454c56', cell: 96, repeat: [1, 1] }); break;
      case 'floor':    m = grating({ repeat: [1, 1] }); break;
      case 'deck':     m = hullPlate({ base: '#4d545e', cell: 96, rough: 0.7, repeat: [1, 1] }); break;
      case 'hazard':   m = painted('#484a54', { hazard: true, repeat: [1, 1] }); break;
      case 'panelRed': m = painted('#5a2b2b', { repeat: [1, 1] }); break;
      case 'crate':    m = painted('#67707c', { repeat: [1, 1] }); break;
      case 'pipe':     m = new THREE.MeshStandardMaterial({ color: 0x6a707a, metalness: 0.95, roughness: 0.35 }); break;
      case 'glass':    m = new THREE.MeshStandardMaterial({ color: 0x0a1a22, metalness: 0.1, roughness: 0.05,
                            transparent: true, opacity: 0.35 }); break;
      /* The station sizes its own tiling per surface (see tiledBox), so its
         materials repeat exactly once and let the geometry decide. */
      case 'stnHull':  m = hullPlate({ base: '#6d7783', repeat: [1, 1] }); break;
      case 'stnDark':  m = hullPlate({ base: '#48505b', dark: '#2b323b', rough: 0.68, repeat: [1, 1] }); break;
      case 'stnDeck':  m = hullPlate({ base: '#5b636e', cell: 96, rough: 0.62, repeat: [1, 1] }); break;
      case 'stnGrate': m = grating({ repeat: [1, 1] }); break;
      case 'stnPaint': m = painted('#7a828e', { repeat: [1, 1] }); break;
      /* The cupola's panes are the one window in the game you look at rather
         than through, so they are much clearer than a bulkhead port. */
      case 'stnPane':  m = new THREE.MeshStandardMaterial({ color: 0xbcd8ea, metalness: 0.1,
                            roughness: 0.08, transparent: true, opacity: 0.1,
                            envMapIntensity: 0.12, depthWrite: false, side: THREE.DoubleSide });
                       break;
      default:         m = new THREE.MeshStandardMaterial({ color: 0x808080 });
    }
    cache.set(name, m);
    return m;
  }

  /* A mission disposes everything in its scene when it ends, and that
     includes these shared materials. Drop the cache with it so the next
     mission builds fresh ones instead of reusing disposed objects. */
  function reset() {
    for (const m of cache.values()) {
      for (const k of ['map', 'normalMap', 'roughnessMap']) if (m[k]) m[k].dispose();
      m.dispose();
    }
    cache.clear();
  }

  /* ---------- texel density ----------
     A material carries one repeat setting, shared by every mesh using it. So
     a plate texture tuned for a two-metre crate is smeared across a sixty-
     metre deck, and the deck reads as a painted backdrop rather than as
     plating. Rewriting a box's own UVs fixes it per surface: every face is
     scaled so one texture tile covers the same real distance everywhere.

     Face order in a BoxGeometry is +X, -X, +Y, -Y, +Z, -Z, four vertices
     each at one segment, and each face's U and V run along a different pair
     of the box's dimensions. */
  function tileUVs(geo, w, h, d, tile) {
    const uv = geo.attributes.uv;
    if (!uv || uv.count !== 24) return geo;
    const T = tile || 2;
    const spans = [[d, h], [d, h], [w, d], [w, d], [w, h], [w, h]];
    for (let f = 0; f < 6; f++) {
      const [su, sv] = spans[f];
      const ru = Math.max(1, Math.round(su / T));
      const rv = Math.max(1, Math.round(sv / T));
      for (let i = 0; i < 4; i++) {
        const k = f * 4 + i;
        uv.setXY(k, uv.getX(k) * ru, uv.getY(k) * rv);
      }
    }
    uv.needsUpdate = true;
    return geo;
  }

  /* A box whose texture covers a fixed real-world distance per tile. */
  function tiledBox(w, h, d, tile) {
    return tileUVs(new THREE.BoxGeometry(w, h, d), w, h, d, tile);
  }

  SF.materials = { get, hullPlate, grating, painted, emissive, heightToNormal,
                   tileUVs, tiledBox, reset, canvasOf: canvas };
})(window.SF);
