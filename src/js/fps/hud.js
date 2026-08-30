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

      /* Brief line over the ammo counter when something is picked up. */
      pickup(text) {
        const n = $('#pickup-note');
        if (n.textContent === text && n.classList.contains('show')) return;
        n.textContent = text;
        n.classList.remove('show');
        void n.offsetWidth;
        n.classList.add('show');
      },

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

      setVisible(v) { root.hidden = !v; },

      /* ---------- public events ---------- */
      event(name, line, secondsLeft, progress) {
        const box = $('#event-panel');
        box.hidden = false;
        $('#event-name').textContent = name;
        $('#event-line').textContent = line;
        const m = Math.floor(secondsLeft / 60), sec = Math.floor(secondsLeft % 60);
        $('#event-timer').textContent = m + ':' + String(sec).padStart(2, '0');
        $('#event-fill').style.width = Math.round(Math.max(0, Math.min(1, progress)) * 100) + '%';
      },
      eventDistance(m) {
        const d = $('#event-dist');
        if (d) d.textContent = m + 'm';
      },
      clearEvent() { $('#event-panel').hidden = true; },

      /* Squadmates and their vitals, during a co-op mission. */
      squad(list) {
        const box = $('#squad-hud');
        box.hidden = !list || !list.length;
        if (box.hidden) return;
        box.innerHTML = list.map((p) => {
          const cls = SF.classes.CLASSES[p.cls] || SF.classes.CLASSES.bulwark;
          const hp = Math.max(0, Math.min(100, p.hp == null ? 100 : p.hp));
          return `<div class="sq-live${hp <= 0 ? ' down' : ''}" style="--c:${cls.accent}">` +
                 `<b>${p.name}</b>` +
                 `<span class="sq-bar"><i style="width:${hp}%"></i></span></div>`;
        }).join('');
      },

      /* ---------- death and respawn ---------- */
      refreshHarness(left, max) {
        const box = $('#harness');
        box.hidden = max === 0;
        if (!max) return;
        if (box.childElementCount !== max + 1) {
          box.innerHTML = '<span class="h-label">HARNESS</span>';
          for (let i = 0; i < max; i++) box.appendChild(el('i'));
        }
        Array.from(box.querySelectorAll('i')).forEach((c, i) => c.classList.toggle('spent', i >= left));
      },

      deathOverlay(show, remaining, message) {
        const o = $('#death');
        o.hidden = !show;
        if (!show) return;
        $('#death-msg').textContent = message || 'TRAUMA HARNESS ENGAGING';
        $('#death-sub').textContent = remaining > 0
          ? remaining + (remaining === 1 ? ' CHARGE REMAINING' : ' CHARGES REMAINING')
          : 'NO CHARGES REMAINING';
        o.classList.toggle('final', !remaining);
      },

      /* ---------- boss encounter ---------- */
      bossShow(name, sub) {
        $('#boss-bar').hidden = false;
        root.classList.add('boss-fight');
        $('#boss-name').textContent = name;
        $('#boss-sub').textContent = sub || '';
      },
      bossHide() { $('#boss-bar').hidden = true; root.classList.remove('boss-fight'); },

      bossHealth(hp, maxHp, phase, phases, shielded) {
        const pct = Math.max(0, hp / maxHp) * 100;
        $('#boss-fill').style.width = pct + '%';
        $('#boss-hp-text').textContent = Math.max(0, Math.ceil(hp)) + ' / ' + maxHp;
        $('#boss-bar').classList.toggle('shielded', !!shielded);
        const seg = $('#boss-phases');
        if (seg.childElementCount !== phases) {
          seg.innerHTML = '';
          for (let i = 0; i < phases; i++) seg.appendChild(el('i'));
        }
        Array.from(seg.children).forEach((c, i) => c.classList.toggle('done', i < phase));
      },

      bossShield(up, message) {
        const tag = $('#boss-shield');
        tag.textContent = message || (up ? 'SHIELDED' : 'SHIELD DOWN');
        tag.className = up ? 'up' : 'down';
      },

      /* The phrase to play back, as a row of pips that fill in as you get
         each note right. */
      bossPuzzle(length, done, colours) {
        const box = $('#boss-puzzle');
        box.hidden = length === 0;
        if (!length) return;
        if (box.childElementCount !== length) {
          box.innerHTML = '';
          for (let i = 0; i < length; i++) box.appendChild(el('i'));
        }
        Array.from(box.children).forEach((c, i) => {
          c.classList.toggle('lit', i < done);
          c.style.setProperty('--c', '#' + (colours[i % colours.length]).toString(16).padStart(6, '0'));
        });
      },

      bossDefeated() {
        $('#boss-bar').classList.add('dead');
        $('#boss-shield').textContent = 'SILENCED';
      },

      /* Which node is which, shown while the fight is live. */
      bossNodes(colours) {
        const key = $('#node-key');
        key.hidden = false;
        key.innerHTML = colours.map((c, i) =>
          `<span style="--c:#${c.toString(16).padStart(6, '0')}">NODE ${i + 1}</span>`).join('');
      },
      hideNodes() { $('#node-key').hidden = true; }
    };

    return api;
  }

  SF.hud = { create };
})(window.SF);
