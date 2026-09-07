/* ============================================================
   In-mission HUD: a thin DOM layer over the canvas (vitals, ability
   cooldown, ammo, crosshair with a hit-marker flash, an objective
   line, an optional boss bar, and a small kill feed). The elements
   themselves live in index.html; this module only ever reads/writes
   them, so it stays a set of setters rather than its own render loop.
   ============================================================ */
(function (DB) {
  'use strict';

  function el(id) { return document.getElementById(id); }

  function create() {
    const nodes = {
      root: el('mission-hud'),
      vitalsFill: el('hud-vitals-fill'),
      vitalsText: el('hud-vitals-text'),
      shieldFill: el('hud-shield-fill'),
      abilityFill: el('hud-ability-fill'),
      abilityIcon: el('hud-ability-icon'),
      abilityName: el('hud-ability-name'),
      ammoText: el('hud-ammo'),
      crosshair: el('hud-crosshair'),
      objective: el('hud-objective'),
      bossWrap: el('hud-boss'),
      bossFill: el('hud-boss-fill'),
      bossName: el('hud-boss-name'),
      killfeed: el('hud-killfeed'),
      damageFlash: el('hud-damage-flash'),
      classLabel: el('hud-class-label')
    };

    let hitT = 0, damageT = 0;

    function setVitals(frac, shieldFrac) {
      if (nodes.vitalsFill) nodes.vitalsFill.style.width = Math.max(0, frac * 100) + '%';
      if (nodes.vitalsText) nodes.vitalsText.textContent = Math.max(0, Math.round(frac * 100)) + '%';
      if (nodes.shieldFill) nodes.shieldFill.style.width = Math.max(0, (shieldFrac || 0) * 100) + '%';
    }

    function setAbility(frac, name, icon) {
      if (nodes.abilityFill) nodes.abilityFill.style.setProperty('--frac', frac);
      if (nodes.abilityName) nodes.abilityName.textContent = name || '';
      if (nodes.abilityIcon) nodes.abilityIcon.textContent = icon || '';
      if (nodes.root) nodes.root.classList.toggle('ability-ready', frac >= 1);
    }

    function setAmmo(inMag, reserve, reloading) {
      if (nodes.ammoText) nodes.ammoText.textContent = reloading ? 'RELOADING' : (inMag + ' / ' + reserve);
    }

    function setObjective(text) { if (nodes.objective) nodes.objective.textContent = text || ''; }

    function setBoss(visible, frac, name) {
      if (!nodes.bossWrap) return;
      nodes.bossWrap.classList.toggle('show', !!visible);
      if (nodes.bossFill) nodes.bossFill.style.width = Math.max(0, frac * 100) + '%';
      if (nodes.bossName) nodes.bossName.textContent = name || '';
    }

    function setClassLabel(text) { if (nodes.classLabel) nodes.classLabel.textContent = text || ''; }

    function hitMarker(headshot) {
      hitT = 0.16;
      if (nodes.crosshair) nodes.crosshair.classList.add(headshot ? 'hit-head' : 'hit');
    }

    function killFeed(text) {
      if (!nodes.killfeed) return;
      const row = document.createElement('div');
      row.className = 'kf-row';
      row.textContent = text;
      nodes.killfeed.appendChild(row);
      setTimeout(function () { row.classList.add('fade'); }, 2200);
      setTimeout(function () { row.remove(); }, 2800);
      while (nodes.killfeed.children.length > 5) nodes.killfeed.removeChild(nodes.killfeed.firstChild);
    }

    function damageFlash(t) { damageT = Math.max(damageT, t || 0.35); }

    function tick(dt) {
      hitT = Math.max(0, hitT - dt);
      if (hitT <= 0 && nodes.crosshair) nodes.crosshair.classList.remove('hit', 'hit-head');
      damageT = Math.max(0, damageT - dt * 1.6);
      if (nodes.damageFlash) nodes.damageFlash.style.opacity = damageT.toFixed(2);
    }

    function show(v) { if (nodes.root) nodes.root.classList.toggle('show', !!v); }

    return {
      setVitals: setVitals, setAbility: setAbility, setAmmo: setAmmo,
      setObjective: setObjective, setBoss: setBoss, setClassLabel: setClassLabel,
      hitMarker: hitMarker, killFeed: killFeed, damageFlash: damageFlash,
      tick: tick, show: show
    };
  }

  DB.hud = { create: create };
})(window.DB || (window.DB = {}));
