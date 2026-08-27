/* Menu-screen ambience: the drifting starfield behind the registry and
   briefing screens. In-mission feedback lives in fps/. */
(function (SF) {
  'use strict';
  const { $ } = SF.util;

  /* ---------- starfield ---------- */
  let stars = [], canvas, ctx, W = 0, H = 0;

  function initStars() {
    canvas = $('#starfield');
    ctx = canvas.getContext('2d');
    resize();
    window.addEventListener('resize', resize);
    seed();
    requestAnimationFrame(drawStars);
  }

  function resize() {
    W = canvas.width = window.innerWidth;
    H = canvas.height = window.innerHeight;
  }

  function seed() {
    stars = [];
    const count = Math.min(280, Math.floor((W * H) / 6200));
    for (let i = 0; i < count; i++) {
      stars.push({
        x: Math.random() * W,
        y: Math.random() * H,
        z: Math.random() * 0.85 + 0.15,
        tw: Math.random() * Math.PI * 2
      });
    }
  }

  function drawStars(t) {
    ctx.clearRect(0, 0, W, H);
    for (const s of stars) {
      s.y += 0.045 + s.z * 0.14;
      if (s.y > H) { s.y = -2; s.x = Math.random() * W; }
      const tw = 0.55 + 0.45 * Math.sin(t / 620 + s.tw);
      const a = s.z * tw;
      ctx.fillStyle = `rgba(160, 226, 255, ${a * 0.75})`;
      const size = s.z * 1.8;
      ctx.fillRect(s.x, s.y, size, size);
    }
    requestAnimationFrame(drawStars);
  }

  SF.fx = { initStars };
})(window.SF);
