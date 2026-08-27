/* ============================================================
   In-mission HUD. DOM over the canvas — cheaper than drawing it in
   GL and it keeps the menu styling consistent with the rest of the app.
   ============================================================ */
(function (SF) {
  'use strict';
  const { $, el } = SF.util;

  function create() {
    const root = $('#fps-hud');
    const crosshair = $('#crosshair');
    const marker = $('#hitmarker');
    const feed = $('#kill-feed');
    const bannerBox = $('#hud-banner');
    const subtitle = $('#subtitle');

    let spreadPx = 8;

    const api = {
      refreshVitals(hp, maxHp, shield) {
        $('#v-hp').style.width = Math.max(0, (hp / maxHp) * 100) + '%';
        $('#v-hp-text').textContent = Math.max(0, Math.ceil(hp));
        $('#v-sh').style.width = Math.min(100, (shield / 100) * 100) + '%';
        $('#v-sh-text').textContent = Math.ceil(shield);
        root.classList.toggle('critical', hp / maxHp < 0.3);
      },

      refreshAmmo(w) {
        $('#a-mag').textContent = w.ammo;
        $('#a-reserve').textContent = w.reserve;
        $('#ammo').classList.toggle('empty', w.ammo === 0);
        $('#reload-tag').hidden = !w.reloading && w.ammo > 0;
        $('#reload-tag').textContent = w.reloading ? 'RELOADING' : 'PRESS R';
      },

      refreshAbility(w) {
        const pct = w.abilityCool > 0 ? 1 - w.abilityCool / w.ability.cooldown : 1;
        $('#ab-fill').style.width = (pct * 100) + '%';
        $('#ab-name').textContent = w.ability.name;
        $('#ability').classList.toggle('ready', w.abilityCool <= 0);
      },

      setSpread(px) { spreadPx = px; },

      updateCrosshair(dt, spread) {
        spreadPx += (spread - spreadPx) * Math.min(1, 12 * dt);
        crosshair.style.setProperty('--gap', spreadPx.toFixed(1) + 'px');
      },

      hitMarker(head) {
        marker.classList.remove('show', 'head');
        void marker.offsetWidth;                 // restart the animation
        marker.classList.add('show');
        if (head) marker.classList.add('head');
        SF.audio.sfx[head ? 'hitmarkerHead' : 'hitmarker']();
      },

      killFeed(name) {
        const line = el('div', 'kf-line', `<span class="kf-x">✕</span> ${name}`);
        feed.appendChild(line);
        setTimeout(() => line.remove(), 4000);
        while (feed.children.length > 5) feed.removeChild(feed.firstChild);
      },

      banner(text) {
        bannerBox.textContent = text;
        bannerBox.classList.remove('show');
        void bannerBox.offsetWidth;
        bannerBox.classList.add('show');
      },

      objective(text) { $('#hud-objective').innerHTML = text; },

      /* Comms lines carry the story now that there is no dialogue screen. */
      say(speaker, text, ms) {
        subtitle.innerHTML = `<b>${speaker}</b> ${text}`;
        subtitle.classList.add('show');
        clearTimeout(api._subT);
        api._subT = setTimeout(() => subtitle.classList.remove('show'), ms || 5200);
      },

      damageFrom(angle) {
        const ind = el('div', 'dmg-dir');
        ind.style.transform = `rotate(${angle}rad)`;
        $('#dmg-ring').appendChild(ind);
        setTimeout(() => ind.remove(), 900);
      },

      setVisible(v) { root.hidden = !v; }
    };

    return api;
  }

  SF.hud = { create };
})(window.SF);
