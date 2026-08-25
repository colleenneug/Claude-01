/* Shared helpers + global namespace. */
window.SF = window.SF || {};

(function (SF) {
  'use strict';

  const $  = (sel, root) => (root || document).querySelector(sel);
  const $$ = (sel, root) => Array.from((root || document).querySelectorAll(sel));

  function el(tag, cls, html) {
    const n = document.createElement(tag);
    if (cls) n.className = cls;
    if (html != null) n.innerHTML = html;
    return n;
  }

  const clamp = (v, lo, hi) => Math.max(lo, Math.min(hi, v));
  const rand  = (a, b) => a + Math.floor(Math.random() * (b - a + 1));
  const pick  = (arr) => arr[Math.floor(Math.random() * arr.length)];
  const chance = (p) => Math.random() < p;
  const wait  = (ms) => new Promise((r) => setTimeout(r, ms));

  /* Roll a d20-ish check against a target number, modified by an attribute. */
  function check(mod, dc) {
    const roll = rand(1, 20);
    const total = roll + mod;
    return { roll, total, dc, pass: total >= dc, crit: roll === 20, fumble: roll === 1 };
  }

  function toast(msg, kind) {
    const rail = $('#toast-rail');
    if (!rail) return;
    const t = el('div', 'toast' + (kind ? ' ' + kind : ''), msg);
    rail.appendChild(t);
    setTimeout(() => t.remove(), 3400);
  }

  /* Confirm dialog that resolves to a boolean. */
  function confirmDialog(title, text, okLabel, danger) {
    return new Promise((resolve) => {
      const modal = $('#modal');
      $('#modal-title').textContent = title;
      $('#modal-text').innerHTML = text;
      const actions = $('#modal-actions');
      actions.innerHTML = '';

      const cancel = el('button', 'btn btn-ghost btn-sm', 'CANCEL');
      const ok = el('button', 'btn btn-sm ' + (danger ? 'btn-danger' : 'btn-major'), okLabel || 'CONFIRM');
      cancel.dataset.hover = 'CANCEL';
      ok.dataset.hover = okLabel || 'CONFIRM';

      const close = (val) => { modal.hidden = true; resolve(val); };
      cancel.addEventListener('click', () => close(false));
      ok.addEventListener('click', () => close(true));

      actions.append(cancel, ok);
      modal.hidden = false;
    });
  }

  SF.util = { $, $$, el, clamp, rand, pick, chance, wait, check, toast, confirmDialog };
})(window.SF);
