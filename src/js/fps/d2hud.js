/* ============================================================
   VANGUARD HUD — the component's markup and its data binding.

   One call builds the whole readout into a host element and hands back
   a small API: set the vitals, set an ability's charge, set the ammo.
   Nothing here reads the game directly, so the same component drives
   the live HUD and the preview page.
   ============================================================ */
(function (SF) {
  'use strict';

  const ABILITIES = [
    { id: 'grenade', label: 'GRENADE', glyph: '◈' },
    { id: 'melee',   label: 'MELEE',   glyph: '✦' },
    { id: 'super',   label: 'SUPER',   glyph: '◆', super: true }
  ];

  const clamp01 = (v) => Math.max(0, Math.min(1, v || 0));

  function create(host, opts) {
    const o = opts || {};
    const root = document.createElement('div');
    root.className = 'vhud';

    root.innerHTML =
      '<div class="vhud-frame"></div>' +

      '<div class="vhud-vitals">' +
        '<div class="vhud-vitals-label"><span data-name>GUARDIAN</span>' +
        '<span><b data-shieldpct>100</b> SHIELD</span></div>' +
        /* Three segments, 2px apart. The first is 30% of the bar and is
           health; the two after it carry the 70% that is shield. */
        '<div class="vhud-bar">' +
          '<div class="vhud-seg health"><i data-health></i></div>' +
          '<div class="vhud-seg shield"><i data-shield-a></i></div>' +
          '<div class="vhud-seg shield"><i data-shield-b></i></div>' +
        '</div>' +
      '</div>' +

      '<div class="vhud-abilities">' +
        ABILITIES.map((a) =>
          `<div class="vhud-ability${a.super ? ' super' : ''}" data-ability="${a.id}">` +
            '<div class="vhud-slot">' +
              '<div class="vhud-fill"></div>' +
              `<span class="vhud-glyph">${a.glyph}</span>` +
            '</div>' +
            `<div class="vhud-label">${a.label}</div>` +
          '</div>').join('') +
      '</div>' +

      '<div class="vhud-weapon vhud-panel">' +
        '<span class="vhud-mag" data-mag>24</span>' +
        '<span class="vhud-divider"></span>' +
        '<span class="vhud-reserve" data-reserve>168</span>' +
        '<span class="vhud-weapon-name" data-weapon>ARC LANCE</span>' +
      '</div>' +

      '<div class="vhud-reticle"><span></span><span></span><span></span><span></span></div>';

    host.appendChild(root);

    const q = (sel) => root.querySelector(sel);
    const els = {
      health: q('[data-health]'),
      shieldA: q('[data-shield-a]'),
      shieldB: q('[data-shield-b]'),
      shieldPct: q('[data-shieldpct]'),
      name: q('[data-name]'),
      mag: q('[data-mag]'),
      reserve: q('[data-reserve]'),
      weapon: q('[data-weapon]')
    };
    const abilities = {};
    for (const a of ABILITIES) {
      const node = root.querySelector('[data-ability="' + a.id + '"]');
      abilities[a.id] = { node: node, fill: node.querySelector('.vhud-fill') };
    }

    /* Health is the first 30% of the bar, shield the remaining 70% across
       two segments — so a shield at half full drains the far segment first
       and then the near one, the way a two-stage bar should read. */
    function vitals(healthFrac, shieldFrac) {
      const h = clamp01(healthFrac), s = clamp01(shieldFrac);
      els.health.style.width = (h * 100) + '%';
      els.shieldA.style.width = (clamp01(s * 2) * 100) + '%';
      els.shieldB.style.width = (clamp01(s * 2 - 1) * 100) + '%';
      els.shieldPct.textContent = Math.round(s * 100);
    }

    function ability(id, charge, ready) {
      const a = abilities[id];
      if (!a) return;
      a.fill.style.height = (clamp01(charge) * 100) + '%';
      a.node.classList.toggle('ready', ready === undefined ? charge >= 1 : !!ready);
    }

    function ammo(mag, reserve) {
      els.mag.textContent = mag;
      els.reserve.textContent = reserve;
    }

    const setName = (v) => { els.name.textContent = v; };
    const setWeapon = (v) => { els.weapon.textContent = v; };

    vitals(o.health == null ? 1 : o.health, o.shield == null ? 1 : o.shield);
    for (const a of ABILITIES) ability(a.id, 1, true);

    return {
      root, vitals, ability, ammo, setName, setWeapon,
      destroy() { root.remove(); },
      get abilities() { return ABILITIES.slice(); }
    };
  }

  SF.d2hud = { create, ABILITIES };
})(window.SF = window.SF || {});
