/* ============================================================
   Three character slots, persisted to localStorage.
   Every read is defensive: a browser with storage blocked, or a
   corrupted blob, degrades to an empty registry instead of a crash.
   ============================================================ */
(function (SF) {
  'use strict';

  const KEY = 'erebus.cradle.registry.v1';
  const SLOTS = 3;

  function blank() { return { slots: [null, null, null] }; }

  function read() {
    try {
      const raw = localStorage.getItem(KEY);
      if (!raw) return blank();
      const data = JSON.parse(raw);
      if (!data || !Array.isArray(data.slots)) return blank();
      data.slots.length = SLOTS;
      for (let i = 0; i < SLOTS; i++) if (data.slots[i] === undefined) data.slots[i] = null;
      return data;
    } catch (err) {
      return blank();
    }
  }

  function write(data) {
    try {
      localStorage.setItem(KEY, JSON.stringify(data));
      return true;
    } catch (err) {
      SF.util.toast('REGISTRY WRITE FAILED — PROGRESS IS SESSION-ONLY', 'bad');
      return false;
    }
  }

  const all = () => read().slots;
  const get = (i) => read().slots[i] || null;

  function save(i, character) {
    const data = read();
    character.saved = Date.now();
    data.slots[i] = character;
    return write(data);
  }

  function erase(i) {
    const data = read();
    data.slots[i] = null;
    return write(data);
  }

  SF.storage = { all, get, save, erase, SLOTS };
})(window.SF);
