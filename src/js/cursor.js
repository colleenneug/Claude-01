/* ============================================================
   The cursor rig. The native pointer is hidden everywhere; this
   reticle replaces it and reports what it is hovering over.
   Also provides "hold to commit" for irreversible actions.
   ============================================================ */
(function (SF) {
  'use strict';
  const { $, el } = SF.util;

  let cur, trail, label, prog;
  let x = window.innerWidth / 2, y = window.innerHeight / 2;
  let rx = x, ry = y;               // smoothed position
  let lastTrail = 0;
  let hovering = null;

  const HOLD_MS = 900;

  function init() {
    cur = $('#cursor');
    trail = $('#cursor-trail');
    label = cur.querySelector('.cur-label');
    prog = cur.querySelector('.cur-progress circle');

    document.addEventListener('pointermove', onMove, { passive: true });
    document.addEventListener('pointerdown', onDown);
    document.addEventListener('pointerup', onUp);
    document.addEventListener('pointerover', onOver);
    document.addEventListener('pointerout', onOut);
    document.addEventListener('mouseleave', () => (cur.style.opacity = '0'));
    document.addEventListener('mouseenter', () => (cur.style.opacity = '1'));

    requestAnimationFrame(loop);
  }

  function onMove(e) { x = e.clientX; y = e.clientY; }

  function loop(t) {
    // ease toward the true pointer so the reticle has weight
    rx += (x - rx) * 0.34;
    ry += (y - ry) * 0.34;
    cur.style.transform = `translate(${rx}px, ${ry}px)`;

    if (t - lastTrail > 26 && (Math.abs(x - rx) > 0.6 || Math.abs(y - ry) > 0.6)) {
      lastTrail = t;
      const d = el('div', 'trail-dot');
      d.style.left = rx + 'px';
      d.style.top = ry + 'px';
      trail.appendChild(d);
      setTimeout(() => d.remove(), 520);
    }
    requestAnimationFrame(loop);
  }

  /* --- hover states ------------------------------------------------ */
  function interactiveOf(node) {
    if (!node || !node.closest) return null;
    return node.closest('button, input, [data-hover], a');
  }

  function onOver(e) {
    const target = interactiveOf(e.target);
    if (!target || target === hovering) return;
    hovering = target;

    cur.classList.add('hot');
    if (target.tagName === 'INPUT') cur.classList.add('text');
    if (target.classList.contains('btn-danger') || target.classList.contains('risky')) cur.classList.add('danger');
    if (target.disabled) { cur.classList.add('blocked'); cur.classList.remove('hot'); }

    const txt = target.dataset.hover || guessLabel(target);
    if (txt) { label.textContent = txt; cur.classList.add('labelled'); }
    if (!target.disabled) SF.audio.sfx.hover();
  }

  function guessLabel(t) {
    if (t.disabled) return 'LOCKED';
    if (t.tagName === 'INPUT') return 'INPUT';
    const txt = (t.textContent || '').trim().split('\n')[0];
    return txt.length > 26 ? 'SELECT' : txt.toUpperCase();
  }

  function onOut(e) {
    const target = interactiveOf(e.target);
    if (!target || target !== hovering) return;
    if (e.relatedTarget && interactiveOf(e.relatedTarget) === target) return;
    hovering = null;
    cur.classList.remove('hot', 'danger', 'text', 'blocked', 'labelled');
    cancelHold();
  }

  /* --- click + hold ------------------------------------------------ */
  let holdTimer = null, holdRaf = null, holdStart = 0, holdTarget = null;

  function onDown(e) {
    cur.classList.add('clicking');
    ripple(e.clientX, e.clientY);

    const t = interactiveOf(e.target);
    if (t && t.dataset.hold != null && !t.disabled) startHold(t);
  }

  function onUp() {
    cur.classList.remove('clicking');
    cancelHold();
  }

  function startHold(target) {
    holdTarget = target;
    holdStart = performance.now();
    cur.classList.add('holding');
    const fill = target.querySelector('.hold-fill');

    const step = () => {
      const p = Math.min(1, (performance.now() - holdStart) / HOLD_MS);
      prog.style.strokeDashoffset = String(276 * (1 - p));
      if (fill) fill.style.width = p * 100 + '%';
      if (p < 1) holdRaf = requestAnimationFrame(step);
    };
    holdRaf = requestAnimationFrame(step);

    holdTimer = setTimeout(() => {
      SF.audio.sfx.confirm();
      target.dispatchEvent(new CustomEvent('holdcomplete', { bubbles: true }));
      cancelHold();
    }, HOLD_MS);
  }

  function cancelHold() {
    if (holdTimer) clearTimeout(holdTimer);
    if (holdRaf) cancelAnimationFrame(holdRaf);
    holdTimer = holdRaf = null;
    cur.classList.remove('holding');
    prog.style.strokeDashoffset = '276';
    if (holdTarget) {
      const fill = holdTarget.querySelector('.hold-fill');
      if (fill) fill.style.width = '0%';
    }
    holdTarget = null;
  }

  function ripple(px, py) {
    const r = el('div', 'ripple');
    r.style.left = px + 'px';
    r.style.top = py + 'px';
    document.body.appendChild(r);
    setTimeout(() => r.remove(), 600);
  }

  /* Force the reticle back to neutral (used when the DOM under it is replaced). */
  function reset() {
    hovering = null;
    cur.classList.remove('hot', 'danger', 'text', 'blocked', 'labelled');
    cancelHold();
  }

  SF.cursor = { init, reset, ripple };
})(window.SF);
