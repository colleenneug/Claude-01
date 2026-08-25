/* Starfield, screen shake, floating numbers, typewriter. */
(function (SF) {
  'use strict';
  const { $, el, rand, wait } = SF.util;

  /* ---------- starfield ---------- */
  let stars = [], canvas, ctx, W = 0, H = 0, warp = 0;

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
    warp *= 0.94;
    for (const s of stars) {
      s.y += (0.045 + s.z * 0.14) * (1 + warp * 26);
      if (s.y > H) { s.y = -2; s.x = Math.random() * W; }
      const tw = 0.55 + 0.45 * Math.sin(t / 620 + s.tw);
      const a = s.z * tw;
      ctx.fillStyle = `rgba(160, 226, 255, ${a * 0.75})`;
      const size = s.z * 1.8;
      if (warp > 0.02) {
        ctx.fillRect(s.x, s.y, size, size + warp * 60 * s.z);
      } else {
        ctx.fillRect(s.x, s.y, size, size);
      }
    }
    requestAnimationFrame(drawStars);
  }

  const warpPulse = () => { warp = 1; };

  /* ---------- screen feedback ---------- */
  function shake(intensity) {
    const app = $('#app');
    const amp = intensity || 8;
    let n = 0;
    const id = setInterval(() => {
      n++;
      const f = 1 - n / 12;
      app.style.transform = `translate(${(Math.random() - 0.5) * amp * f}px, ${(Math.random() - 0.5) * amp * f}px)`;
      if (n >= 12) { clearInterval(id); app.style.transform = ''; }
    }, 24);
  }

  function flash(kind) {
    const f = el('div', 'screen-flash ' + (kind || 'red'));
    document.body.appendChild(f);
    setTimeout(() => f.remove(), 340);
  }

  function floatNum(target, text, kind) {
    if (!target) return;
    const host = target.offsetParent || target.parentElement || document.body;
    const n = el('div', 'float-num ' + (kind || 'dmg'), text);
    const tr = target.getBoundingClientRect();
    const hr = host.getBoundingClientRect();
    n.style.left = (tr.left - hr.left + tr.width / 2 - 16 + rand(-14, 14)) + 'px';
    n.style.top = (tr.top - hr.top + tr.height * 0.32) + 'px';
    host.appendChild(n);
    setTimeout(() => n.remove(), 1050);
  }

  function banner(text, kind) {
    const layer = $('#combat-layer');
    const b = el('div', 'banner ' + kind, text);
    layer.appendChild(b);
    setTimeout(() => b.remove(), 2200);
  }

  /* ---------- typewriter ---------- */
  let typeToken = 0;
  let inFlight = null;   // { node, html } currently being typed

  /* Types html-ish text into a node. Tags are emitted whole so markup
     never appears half-written. Returns a promise; a new call cancels
     any run still in flight. */
  async function typeInto(node, html, speed) {
    const my = ++typeToken;
    inFlight = { node: node, html: html };
    node.classList.add('typing');
    node.innerHTML = '';
    const step = speed || 9;
    let i = 0, out = '';
    while (i < html.length) {
      if (my !== typeToken) return;
      if (html[i] === '<') {
        const close = html.indexOf('>', i);
        out += html.slice(i, close + 1);
        i = close + 1;
        continue;
      }
      // burst a few characters per frame so long passages stay readable
      const burst = rand(2, 4);
      out += html.slice(i, i + burst);
      i += burst;
      node.innerHTML = out;
      if (i % 5 < 3) SF.audio.sfx.type();
      await wait(step);
    }
    node.innerHTML = html;
    node.classList.remove('typing');
    inFlight = null;
  }

  /* Finish the current passage instantly (the player clicked through it). */
  function skipTyping() {
    if (!inFlight) return;
    typeToken++;
    inFlight.node.innerHTML = inFlight.html;
    inFlight.node.classList.remove('typing');
    inFlight = null;
  }

  SF.fx = { initStars, warpPulse, shake, flash, floatNum, banner, typeInto, skipTyping };
})(window.SF);
