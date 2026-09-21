/* Baseball Masters — canvas rendering: the overhead field and the batter's-eye
   pitch tunnel. Pure drawing; game state is passed in each frame. */
(function (BM) {
  'use strict';
  const P = BM.physics, ZONE = BM.ZONE;

  function Field(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.scale = 1;
    this.resize();
  }
  Field.prototype.resize = function () {
    const c = this.canvas;
    const dpr = window.devicePixelRatio || 1;
    const w = c.clientWidth, h = c.clientHeight;
    c.width = Math.round(w * dpr); c.height = Math.round(h * dpr);
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.w = w; this.h = h;
    this.originX = w / 2;
    this.originY = h - Math.min(70, h * 0.14);
    this.scale = Math.min(w / 620, h / 560);
  };
  Field.prototype.toScreen = function (x, y) {
    return { x: this.originX + x * this.scale, y: this.originY - y * this.scale };
  };

  Field.prototype.drawStadium = function (stadium) {
    const ctx = this.ctx, s = this.scale;
    ctx.save();
    ctx.fillStyle = '#0c2418';
    ctx.fillRect(0, 0, this.w, this.h);

    // Outfield grass arc + fence.
    ctx.beginPath();
    const steps = 40;
    for (let i = 0; i <= steps; i++) {
      const ang = -45 + (90 * i / steps);
      const d = BM.data.fenceAt(stadium, ang);
      const rad = ang * Math.PI / 180;
      const p = this.toScreen(d * Math.sin(rad), d * Math.cos(rad));
      if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y);
    }
    const home = this.toScreen(0, 0);
    ctx.lineTo(home.x, home.y);
    ctx.closePath();
    ctx.fillStyle = stadium.turf || '#2f7d3e';
    ctx.fill();

    // Fence line.
    ctx.beginPath();
    for (let i = 0; i <= steps; i++) {
      const ang = -45 + (90 * i / steps);
      const d = BM.data.fenceAt(stadium, ang);
      const rad = ang * Math.PI / 180;
      const p = this.toScreen(d * Math.sin(rad), d * Math.cos(rad));
      if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y);
    }
    ctx.strokeStyle = '#7a5230'; ctx.lineWidth = Math.max(3, 5 * s / 1); ctx.stroke();

    // Infield dirt diamond.
    const dirt = [[0, -8], [95, 95], [0, 190], [-95, 95]];
    ctx.beginPath();
    dirt.forEach((pt, i) => {
      const p = this.toScreen(pt[0], pt[1]);
      if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y);
    });
    ctx.closePath();
    ctx.fillStyle = '#a5713f';
    ctx.fill();

    // Grass diamond inset.
    const grassIn = [[0, 18], [70, 88], [0, 158], [-70, 88]];
    ctx.beginPath();
    grassIn.forEach((pt, i) => {
      const p = this.toScreen(pt[0], pt[1]);
      if (i === 0) ctx.moveTo(p.x, p.y); else ctx.lineTo(p.x, p.y);
    });
    ctx.closePath();
    ctx.fillStyle = stadium.turf || '#2f7d3e';
    ctx.fill();

    // Foul lines.
    ctx.beginPath();
    const lfEnd = this.toScreen(-BM.data.fenceAt(stadium, -45) * 0.99, BM.data.fenceAt(stadium, -45) * 0.05 + 0);
    const rfEnd = this.toScreen(BM.data.fenceAt(stadium, 45) * 0.99, BM.data.fenceAt(stadium, 45) * 0.05);
    ctx.moveTo(home.x, home.y); ctx.lineTo(this.toScreen(-BM.data.fenceAt(stadium, -45) * Math.SQRT1_2, BM.data.fenceAt(stadium, -45) * Math.SQRT1_2).x,
      this.toScreen(-BM.data.fenceAt(stadium, -45) * Math.SQRT1_2, BM.data.fenceAt(stadium, -45) * Math.SQRT1_2).y);
    ctx.moveTo(home.x, home.y); ctx.lineTo(this.toScreen(BM.data.fenceAt(stadium, 45) * Math.SQRT1_2, BM.data.fenceAt(stadium, 45) * Math.SQRT1_2).x,
      this.toScreen(BM.data.fenceAt(stadium, 45) * Math.SQRT1_2, BM.data.fenceAt(stadium, 45) * Math.SQRT1_2).y);
    ctx.strokeStyle = '#e8e4d8'; ctx.lineWidth = 2; ctx.stroke();

    // Bases.
    P.BASES.forEach(b => {
      const p = this.toScreen(b.x, b.y);
      ctx.fillStyle = '#f2efe4';
      ctx.beginPath();
      const r = Math.max(4, 5 * s);
      ctx.rect(p.x - r, p.y - r, r * 2, r * 2);
      ctx.fill();
    });
    // Home plate as a pentagon-ish dot.
    ctx.beginPath(); ctx.arc(home.x, home.y, Math.max(5, 6 * s), 0, Math.PI * 2);
    ctx.fillStyle = '#fff'; ctx.fill();

    // Mound.
    const mound = this.toScreen(0, 60.5);
    ctx.beginPath(); ctx.arc(mound.x, mound.y, Math.max(9, 10 * s), 0, Math.PI * 2);
    ctx.fillStyle = '#a5713f'; ctx.fill();

    ctx.restore();
  };

  Field.prototype.drawFielders = function (team, highlightPos) {
    const ctx = this.ctx;
    P.FIELDERS.forEach(f => {
      if (f.pos === 'C') return;
      const p = this.toScreen(f.x, f.y);
      const active = f.pos === highlightPos;
      ctx.beginPath();
      ctx.arc(p.x, p.y, active ? 8 : 6, 0, Math.PI * 2);
      ctx.fillStyle = active ? '#ffd24a' : (team ? team.primary : '#5f7fff');
      ctx.fill();
      ctx.strokeStyle = 'rgba(0,0,0,.4)'; ctx.lineWidth = 1.5; ctx.stroke();
      ctx.fillStyle = '#fff'; ctx.font = '10px system-ui'; ctx.textAlign = 'center';
      ctx.fillText(f.pos, p.x, p.y - 10);
    });
  };

  Field.prototype.drawRunners = function (bases) {
    const ctx = this.ctx;
    const pts = [P.BASES[0], P.BASES[1], P.BASES[2]];
    bases.forEach((r, i) => {
      if (!r) return;
      const p = this.toScreen(pts[i].x, pts[i].y);
      ctx.beginPath();
      ctx.arc(p.x, p.y - 14, 6, 0, Math.PI * 2);
      ctx.fillStyle = '#ff5a5a'; ctx.fill();
      ctx.strokeStyle = '#fff'; ctx.lineWidth = 1; ctx.stroke();
    });
  };

  /** Draw a moving marker along a flight path at time t. Returns false when done. */
  Field.prototype.drawBallAt = function (path, t) {
    const pt = P.sampleAt(path, t);
    const s = this.toScreen(pt.x, pt.y);
    const ctx = this.ctx;
    const lift = pt.z * this.scale * 0.55;
    ctx.beginPath();
    ctx.arc(s.x, s.y - lift, Math.max(3, 4 + pt.z * 0.05), 0, Math.PI * 2);
    ctx.fillStyle = '#fff'; ctx.fill();
    ctx.strokeStyle = '#222'; ctx.lineWidth = 1; ctx.stroke();
    // Shadow on the ground.
    ctx.beginPath();
    ctx.ellipse(s.x, s.y, 4, 2, 0, 0, Math.PI * 2);
    ctx.fillStyle = 'rgba(0,0,0,.25)'; ctx.fill();
    return pt;
  };

  Field.prototype.drawTrail = function (path, uptoT) {
    const ctx = this.ctx;
    ctx.beginPath();
    let started = false;
    for (const pt of path) {
      if (pt.t > uptoT) break;
      const s = this.toScreen(pt.x, pt.y);
      const lift = pt.z * this.scale * 0.55;
      if (!started) { ctx.moveTo(s.x, s.y - lift); started = true; }
      else ctx.lineTo(s.x, s.y - lift);
    }
    ctx.strokeStyle = 'rgba(255,255,255,.55)';
    ctx.lineWidth = 1.5;
    ctx.stroke();
  };

  /* ---------------------------------------------------------- pitch tunnel */

  function PitchView(canvas) {
    this.canvas = canvas;
    this.ctx = canvas.getContext('2d');
    this.resize();
  }
  PitchView.prototype.resize = function () {
    const c = this.canvas;
    const dpr = window.devicePixelRatio || 1;
    const w = c.clientWidth, h = c.clientHeight;
    c.width = Math.round(w * dpr); c.height = Math.round(h * dpr);
    this.ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    this.w = w; this.h = h;
    // Map feet -> pixels. x: [-2.6,2.6], z: [0.1, 5.2] (z inverted for screen).
    this.padX = w * 0.5;
    this.padTop = h * 0.08;
    this.zoneH = h * 0.78;
    this.scaleX = (w * 0.42) / 2.6;
    this.scaleZ = this.zoneH / 5.1;
  };
  PitchView.prototype.toScreen = function (x, z) {
    return { x: this.padX + x * this.scaleX, y: this.padTop + this.zoneH - z * this.scaleZ };
  };

  PitchView.prototype.drawBackground = function () {
    const ctx = this.ctx;
    ctx.save();
    const grad = ctx.createLinearGradient(0, 0, 0, this.h);
    grad.addColorStop(0, '#0a1626'); grad.addColorStop(1, '#132a1c');
    ctx.fillStyle = grad; ctx.fillRect(0, 0, this.w, this.h);
    // Mound-to-plate guide lines for depth cue.
    ctx.strokeStyle = 'rgba(255,255,255,.08)'; ctx.lineWidth = 1;
    const vp = { x: this.padX, y: this.padTop - this.h * 0.12 };
    [[-1, 0.2], [1, 0.2], [-1, 5.0], [1, 5.0]].forEach(([sx, z]) => {
      const p = this.toScreen(sx * 2.4, z);
      ctx.beginPath(); ctx.moveTo(vp.x, vp.y); ctx.lineTo(p.x, p.y); ctx.stroke();
    });
    ctx.restore();
  };

  PitchView.prototype.drawZone = function () {
    const ctx = this.ctx;
    const a = this.toScreen(-ZONE.halfW, ZONE.top);
    const b = this.toScreen(ZONE.halfW, ZONE.bot);
    ctx.save();
    ctx.strokeStyle = 'rgba(255,255,255,.85)';
    ctx.lineWidth = 2.5;
    ctx.strokeRect(a.x, a.y, b.x - a.x, b.y - a.y);
    ctx.strokeStyle = 'rgba(255,255,255,.28)'; ctx.lineWidth = 1;
    for (let i = 1; i < 3; i++) {
      const x = a.x + (b.x - a.x) * i / 3;
      ctx.beginPath(); ctx.moveTo(x, a.y); ctx.lineTo(x, b.y); ctx.stroke();
      const y = a.y + (b.y - a.y) * i / 3;
      ctx.beginPath(); ctx.moveTo(a.x, y); ctx.lineTo(b.x, y); ctx.stroke();
    }
    ctx.restore();
  };

  PitchView.prototype.drawAimReticle = function (x, z, color) {
    const p = this.toScreen(x, z);
    const ctx = this.ctx;
    ctx.save();
    ctx.strokeStyle = color || '#ffd24a'; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(p.x, p.y, 12, 0, Math.PI * 2); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p.x - 18, p.y); ctx.lineTo(p.x - 6, p.y); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p.x + 6, p.y); ctx.lineTo(p.x + 18, p.y); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p.x, p.y - 18); ctx.lineTo(p.x, p.y - 6); ctx.stroke();
    ctx.beginPath(); ctx.moveTo(p.x, p.y + 6); ctx.lineTo(p.x, p.y + 18); ctx.stroke();
    ctx.restore();
  };

  PitchView.prototype.drawPCI = function (x, z, radius) {
    const p = this.toScreen(x, z);
    const rr = radius * (this.scaleX + this.scaleZ) / 2;
    const ctx = this.ctx;
    ctx.save();
    ctx.strokeStyle = 'rgba(90,215,255,.95)'; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.arc(p.x, p.y, Math.max(6, rr), 0, Math.PI * 2); ctx.stroke();
    ctx.fillStyle = 'rgba(90,215,255,.18)'; ctx.fill();
    ctx.beginPath(); ctx.arc(p.x, p.y, 2.5, 0, Math.PI * 2); ctx.fillStyle = '#5ad7ff'; ctx.fill();
    ctx.restore();
  };

  /** Ball position for a given progress u (0..1) along its flight to the plate. */
  PitchView.prototype.drawBall = function (game, pitch, u, color) {
    const pos = game.pitchPos(pitch, u);
    const p = this.toScreen(pos.x, pos.z);
    const size = 3 + u * 6;
    const ctx = this.ctx;
    ctx.save();
    ctx.beginPath(); ctx.arc(p.x, p.y, size, 0, Math.PI * 2);
    ctx.fillStyle = color || '#fff';
    ctx.shadowColor = color || '#fff'; ctx.shadowBlur = 6 * u;
    ctx.fill();
    ctx.restore();
    return pos;
  };

  BM.render = { Field: Field, PitchView: PitchView };
})(window.BM = window.BM || {});
