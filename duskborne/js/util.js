/* ============================================================
   Small shared helpers: clamping, a toast, and a localStorage wrapper
   that degrades to an in-memory save rather than crashing a private
   window or blocked-storage session.
   ============================================================ */
(function (DB) {
  'use strict';

  function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)); }
  function lerp(a, b, t) { return a + (b - a) * t; }
  function rand(lo, hi) { return lo + Math.random() * (hi - lo); }
  function pick(arr) { return arr[(Math.random() * arr.length) | 0]; }

  let toastTimer = null;
  function toast(msg, ms) {
    let el = document.getElementById('db-toast');
    if (!el) {
      el = document.createElement('div');
      el.id = 'db-toast';
      document.body.appendChild(el);
    }
    el.textContent = msg;
    el.classList.add('show');
    clearTimeout(toastTimer);
    toastTimer = setTimeout(function () { el.classList.remove('show'); }, ms || 2200);
  }

  const STORAGE_KEY = 'duskborne.save.v1';
  let memoryFallback = null;
  let storageOk = true;
  try {
    const probe = '__duskborne_probe__';
    window.localStorage.setItem(probe, '1');
    window.localStorage.removeItem(probe);
  } catch (err) { storageOk = false; void err; }

  const storage = {
    get ok() { return storageOk; },
    load() {
      if (!storageOk) return memoryFallback;
      try {
        const raw = window.localStorage.getItem(STORAGE_KEY);
        return raw ? JSON.parse(raw) : null;
      } catch (err) { void err; return memoryFallback; }
    },
    save(data) {
      if (!storageOk) { memoryFallback = data; return; }
      try { window.localStorage.setItem(STORAGE_KEY, JSON.stringify(data)); }
      catch (err) { memoryFallback = data; void err; }
    },
    clear() {
      memoryFallback = null;
      if (!storageOk) return;
      try { window.localStorage.removeItem(STORAGE_KEY); } catch (err) { void err; }
    }
  };

  DB.util = { clamp: clamp, lerp: lerp, rand: rand, pick: pick, toast: toast, storage: storage };
})(window.DB || (window.DB = {}));
