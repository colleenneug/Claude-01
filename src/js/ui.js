/* ============================================================
   Screens, the three character slots, enlistment, and the
   in-mission HUD / story driver.
   ============================================================ */
(function (SF) {
  'use strict';
  const { $, $$, el, clamp, pick, wait, toast, confirmDialog } = SF.util;

  let ch = null;          // active character
  let slotIndex = -1;     // which registry bay it belongs to
  let draftClass = null;  // doctrine selected during enlistment
  let sessionStart = 0;
  let navToken = 0;       // guards against a passage resolving after the player moved on
  let bootAborted = false;

  /* ---------------- screens ---------------- */

  function show(name) {
    $$('.screen').forEach((s) => s.classList.remove('active'));
    const target = $('#screen-' + name);
    if (target) target.classList.add('active');
    SF.cursor.reset();
    window.scrollTo(0, 0);
  }

  /* ---------------- boot ---------------- */

  const BOOT_LINES = [
    ['RECOVERY DIVISION FIELD TERMINAL — REV 9.4', 'dim'],
    ['POST ................................ <span class="ok">PASS</span>', ''],
    ['NEURAL HARNESS LINK ................. <span class="ok">PASS</span>', ''],
    ['TRAUMA COCKTAIL RESERVOIR ........... <span class="ok">1 CHARGE</span>', ''],
    ['LONG-RANGE UPLINK ................... <span class="bad">NO CARRIER</span>', ''],
    ['', ''],
    ['MOUNTING MISSION PACKAGE 44-C ...', 'dim'],
    ['TARGET: COLONY ARK <em>EREBUS CRADLE</em>', ''],
    ['LAST CONTACT: 40 YEARS, 2 MONTHS', ''],
    ['SOULS ABOARD AT DEPARTURE: 203,000', ''],
    ['CURRENT BROADCAST: <span class="warn">ACTIVE — UNCLASSIFIED WAVEFORM</span>', ''],
    ['', ''],
    ['<span class="warn">WARNING: WAVEFORM ANALYSIS INCOMPLETE. IT IS SINGING.</span>', ''],
    ['', ''],
    ['INTERFACE READY. POINT AND COMMIT.<span class="caret">▌</span>', '']
  ];

  async function runBoot() {
    bootAborted = false;
    const box = $('#boot-log');
    box.innerHTML = '';
    SF.audio.sfx.boot();
    for (const [line, cls] of BOOT_LINES) {
      if (bootAborted) return;
      const row = el('div', cls, '');
      box.appendChild(row);
      if (line) {
        // reveal each line character-wise, tags emitted whole
        let out = '', i = 0;
        while (i < line.length) {
          if (bootAborted) return;
          if (line[i] === '<') { const c = line.indexOf('>', i); out += line.slice(i, c + 1); i = c + 1; continue; }
          out += line[i++];
          row.innerHTML = out;
          if (i % 3 === 0) SF.audio.sfx.type();
          await wait(6);
        }
        row.innerHTML = line;
      }
      await wait(line ? 70 : 120);
    }
    await wait(700);
    if (bootAborted) return;
    document.body.classList.remove('booting');
    show('title');
  }

  /* The player clicked or pressed through the POST sequence. */
  function skipBoot() {
    if (bootAborted || !document.body.classList.contains('booting')) return;
    bootAborted = true;
    document.body.classList.remove('booting');
    show('title');
  }

  /* ---------------- registry (3 slots) ---------------- */

  function renderSlots() {
    const grid = $('#slot-grid');
    grid.innerHTML = '';
    const slots = SF.storage.all();

    slots.forEach((data, i) => {
      const card = el('div', 'slot' + (data ? '' : ' empty'));
      const bay = `<div class="bay">BAY ${String(i + 1).padStart(2, '0')} // ${data ? 'OCCUPIED' : 'VACANT'}</div>`;

      if (!data) {
        card.innerHTML = bay +
          '<div class="slot-empty-mark">+</div>' +
          '<div class="slot-empty-txt">ENLIST OPERATIVE</div>';
        card.dataset.hover = 'ENLIST';
        card.tabIndex = 0;
        card.addEventListener('click', () => openCreate(i));
      } else {
        const cls = SF.classes.CLASSES[data.cls];
        const node = SF.story.NODES[data.node] || {};
        card.innerHTML = bay +
          `<div class="slot-glyph" style="color:${cls.accent};border-color:${cls.accent}">${cls.glyph}</div>` +
          `<div class="slot-name">${data.name}</div>` +
          `<div class="slot-class" style="color:${cls.accent}">${cls.name} · ${cls.role}</div>` +
          '<div class="slot-meta">' +
            `<div class="row"><span>RANK</span><b>${data.level}</b></div>` +
            `<div class="row"><span>VITALS</span><b>${data.hp}/${data.maxHp}</b></div>` +
            `<div class="row"><span>CHAPTER</span><b>${romanise(data.chapter || 1)}</b></div>` +
            (data.completions
              ? `<div class="row"><span>FILES CLOSED</span><b>${data.completions} · ${Object.keys(data.endings || {}).join(', ').toUpperCase()}</b></div>`
              : `<div class="row"><span>POSITION</span><b>${(node.loc || 'UNKNOWN').split(' / ')[0]}</b></div>`) +
          '</div>' +
          '<div class="slot-actions">' +
            '<button class="btn btn-sm" data-act="deploy">DEPLOY</button>' +
            '<button class="btn btn-sm btn-danger hold-btn" data-act="erase" data-hold="1" ' +
              'data-hover="HOLD TO ERASE"><span class="hold-fill"></span>ERASE</button>' +
          '</div>';

        card.querySelector('[data-act="deploy"]').addEventListener('click', () => deploy(i));
        const eraseBtn = card.querySelector('[data-act="erase"]');
        eraseBtn.addEventListener('holdcomplete', () => {
          SF.storage.erase(i);
          toast(`BAY ${i + 1} PURGED`, 'bad');
          renderSlots();
        });
      }
      grid.appendChild(card);
    });
  }

  const romanise = (n) => ['—', 'I', 'II', 'III', 'IV'][n] || String(n);

  /* ---------------- enlistment ---------------- */

  const CALLSIGNS = ['VESPER', 'HALLOW', 'IRONWAKE', 'NULLPOINT', 'SABLE', 'CATHEDRAL',
                     'QUIETUS', 'MERIDIAN', 'GRAVEWELL', 'ASHLINE', 'PALEHORSE', 'TERMINUS'];

  function openCreate(i) {
    slotIndex = i;
    draftClass = null;
    $('#create-bay').textContent = String(i + 1);
    $('#name-input').value = '';
    $('#btn-enlist').disabled = true;
    renderClassRack();
    $('#class-detail').innerHTML = '<div class="empty-detail">SELECT A DOCTRINE</div>';
    SF.audio.sfx.open();
    show('create');
  }

  function renderClassRack() {
    const rack = $('#class-rack');
    rack.innerHTML = '';
    Object.values(SF.classes.CLASSES).forEach((cls) => {
      const card = el('button', 'class-card' + (draftClass === cls.id ? ' selected' : ''));
      card.style.setProperty('--accent', cls.accent);
      card.dataset.hover = cls.name;
      card.innerHTML =
        `<div class="cc-glyph">${cls.glyph}</div>` +
        '<div>' +
          `<div class="cc-name">${cls.name}</div>` +
          `<div class="cc-role">${cls.role}</div>` +
          `<div class="cc-line">${cls.tagline}</div>` +
        '</div>';
      card.addEventListener('click', () => selectClass(cls.id));
      rack.appendChild(card);
    });
  }

  function selectClass(id) {
    draftClass = id;
    renderClassRack();
    const cls = SF.classes.CLASSES[id];
    const detail = $('#class-detail');
    detail.style.setProperty('--accent', cls.accent);
    detail.innerHTML =
      `<div class="cd-title">${cls.name}</div>` +
      `<div class="cc-role" style="color:${cls.accent}">${cls.role}</div>` +
      `<div class="cd-flavor">${cls.flavor}</div>` +

      '<div class="cd-sec">BASE PROFILE</div>' +
      '<div class="cd-stats">' +
        `<div><span class="k">VITALS</span><span class="v">${cls.base.hp}</span></div>` +
        `<div><span class="k">CORE</span><span class="v">${cls.base.energy}</span></div>` +
        `<div><span class="k">MIGHT</span><span class="v">${cls.base.might}</span></div>` +
        `<div><span class="k">SYNC</span><span class="v">${cls.base.sync}</span></div>` +
        `<div><span class="k">GUILE</span><span class="v">${cls.base.guile}</span></div>` +
        `<div><span class="k">FIELD SKILL</span><span class="v">${cls.skill}</span></div>` +
      '</div>' +

      '<div class="cd-sec">PASSIVE — ' + cls.perk.name + '</div>' +
      `<div class="cd-perk">${cls.perk.desc}</div>` +

      '<div class="cd-sec">ABILITY SUITE</div>' +
      cls.abilities.map((a) =>
        '<div class="cd-ability">' +
          `<div class="an">${a.name}${a.ultimate ? ' ★' : ''}</div>` +
          `<div class="ac">${a.cost} CORE${a.cd ? ' · COOLDOWN ' + a.cd : ''}</div>` +
          `<div class="ad">${a.desc}</div>` +
        '</div>').join('');

    SF.audio.sfx.confirm();
    validateEnlist();
  }

  function validateEnlist() {
    const name = $('#name-input').value.trim();
    $('#btn-enlist').disabled = !(draftClass && name.length >= 2);
  }

  function enlist() {
    const name = $('#name-input').value.trim().toUpperCase();
    if (!draftClass || name.length < 2) { SF.audio.sfx.deny(); return; }
    const character = SF.classes.makeCharacter(name, draftClass);
    SF.storage.save(slotIndex, character);
    toast(`${name} REGISTERED TO BAY ${slotIndex + 1}`, 'good');
    SF.audio.sfx.confirm();
    deploy(slotIndex);
  }

  /* ---------------- codex ---------------- */

  function renderCodex() {
    $('#codex-body').innerHTML = SF.story.CODEX
      .map((e) => `<div class="codex-entry"><h4>${e.t}</h4><p>${e.b}</p></div>`).join('');
  }

  /* ---------------- mission ---------------- */

  function deploy(i) {
    slotIndex = i;
    ch = SF.storage.get(i);
    if (!ch) return;
    if (!ch.statuses) ch.statuses = {};
    sessionStart = Date.now();

    const cls = SF.classes.CLASSES[ch.cls];
    document.documentElement.style.setProperty('--accent', cls.accent);
    $('#hud-portrait').style.setProperty('--accent', cls.accent);
    $('#portrait-glyph').textContent = cls.glyph;
    $('#hud-name').textContent = ch.name;
    $('#hud-class').textContent = cls.name + ' · ' + cls.role;
    $('#log').innerHTML = '';

    SF.audio.sfx.open();
    SF.fx.warpPulse();
    show('game');
    refresh();
    goNode(ch.node, true);
  }

  function refresh() {
    if (!ch) return;
    setBar('#hp-fill', '#hp-text', ch.hp, ch.maxHp, `${ch.hp}/${ch.maxHp}`);
    setBar('#sh-fill', '#sh-text', Math.min(ch.shield, 60), 60, String(ch.shield));
    setBar('#en-fill', '#en-text', ch.energy, ch.maxEnergy, `${ch.energy}/${ch.maxEnergy}`);
    const need = SF.classes.xpForLevel(ch.level);
    setBar('#xp-fill', '#xp-text', ch.xp, need, `${ch.xp}/${need}`);
    $('#lvl-text').textContent = ch.level;

    $('#hud-attrs').innerHTML = SF.classes.ATTRS.map((a) =>
      `<div class="attr" data-key="${a.key}" data-hover="${a.label} ${ch[a.key]}">` +
      `<div class="an">${a.label}</div><div class="av">${ch[a.key]}</div></div>`).join('');

    renderInventory();
  }

  function setBar(fillSel, textSel, value, max, label) {
    const fill = $(fillSel);
    const pct = max > 0 ? clamp((value / max) * 100, 0, 100) : 0;
    if (fill.style.width !== pct + '%') {
      fill.classList.add('bar-flash');
      setTimeout(() => fill.classList.remove('bar-flash'), 420);
    }
    fill.style.width = pct + '%';
    $(textSel).textContent = label;
  }

  function renderInventory() {
    const box = $('#hud-inv');
    const items = SF.combat.ITEMS;
    const owned = Object.keys(items).filter((k) => (ch.items[k] || 0) > 0);
    box.innerHTML = '<div class="inv-head">FIELD KIT</div>';
    if (!owned.length) {
      box.appendChild(el('div', 'inv-empty', 'KIT EMPTY'));
      return;
    }
    for (const key of owned) {
      const item = items[key];
      const btn = el('button', 'inv-item');
      btn.dataset.hover = SF.combat.isActive() ? 'USE ' + item.name : 'COMBAT ONLY';
      btn.disabled = !SF.combat.isActive();
      btn.innerHTML = `<span>${item.name}</span><span class="qty">×${ch.items[key]}</span>`;
      btn.title = item.desc;
      btn.addEventListener('click', () => SF.combat.useItem(key));
      box.appendChild(btn);
    }
  }

  /* ---------------- story driver ---------------- */

  async function goNode(id, isEntry) {
    const node = SF.story.NODES[id];
    if (!node) { toast('NAVIGATION ERROR: ' + id, 'bad'); return; }
    const my = ++navToken;

    ch.node = id;
    if (node.onEnter) node.onEnter(ch);
    if (node.chapter) ch.chapter = Math.max(ch.chapter || 1, node.chapter);

    $('#stage-chapter').textContent = 'CHAPTER ' + romanise(node.chapter || ch.chapter || 1);
    $('#stage-loc').textContent = node.loc || '';
    $('#objective').textContent = node.objective || '—';

    if (!isEntry) SF.combat.log('▸ ' + (node.loc || id), 'story');

    const html = typeof node.text === 'function' ? node.text(ch) : (node.text || '');
    const choicesBox = $('#choices');
    choicesBox.innerHTML = '';
    $('#stage-scroll').scrollTop = 0;

    await SF.fx.typeInto($('#narrative'), html, 7);
    if (my !== navToken) return;
    renderChoices(node);
    autosave();
  }

  function renderChoices(node) {
    const box = $('#choices');
    box.innerHTML = '';

    if (node.ending) {
      const btn = el('button', 'choice', '<span class="tag">END</span>Return to the crew registry.');
      btn.dataset.hover = 'REGISTRY';
      btn.addEventListener('click', () => {
        // The file is closed. The operative keeps their rank and can be sent
        // back in — the ark resets, they do not.
        ch.endings = ch.endings || {};
        ch.endings[node.ending] = true;
        ch.completions = (ch.completions || 0) + 1;
        ch.node = 'act1_approach';
        ch.chapter = 1;
        ch.flags = {};
        ch.hp = ch.maxHp;
        autosave();
        SF.audio.sfx.confirm();
        renderSlots();
        show('slots');
      });
      box.appendChild(btn);
      SF.audio.sfx.win();
      return;
    }

    for (const choice of (node.choices || [])) {
      const locked = choice.req && choice.req.cls && choice.req.cls !== ch.cls;
      const btn = el('button', 'choice' +
        (choice.tag ? ' gated' : '') +
        (choice.risky ? ' risky' : '') +
        (locked ? ' locked' : ''));

      const gateName = locked ? SF.classes.CLASSES[choice.req.cls].name : null;
      btn.innerHTML = (choice.tag ? `<span class="tag">${choice.tag}</span>` : '') +
        choice.text + (locked ? ` <em style="color:var(--ink-dim)">— ${gateName} ONLY</em>` : '');
      btn.disabled = locked;
      btn.dataset.hover = locked ? 'DOCTRINE LOCKED' : (choice.tag || 'SELECT');
      if (!locked) btn.addEventListener('click', () => takeChoice(node, choice));
      box.appendChild(btn);
    }
  }

  function takeChoice(node, choice) {
    SF.audio.sfx.click();

    if (choice.flag) ch.flags[choice.flag] = true;
    if (choice.xp) {
      const ups = SF.classes.grantXp(ch, choice.xp);
      SF.combat.log(`+${choice.xp} XP`, 'heal');
      for (const u of ups) { SF.audio.sfx.levelup(); toast(u, 'good'); }
    }
    if (choice.give) {
      for (const k of Object.keys(choice.give)) {
        ch.items[k] = (ch.items[k] || 0) + choice.give[k];
        toast(`ACQUIRED ${SF.combat.ITEMS[k].name} ×${choice.give[k]}`, 'good');
      }
    }
    if (choice.restore) ch.hp = Math.max(1, Math.round(ch.maxHp * choice.restore));
    if (choice.heal) ch.hp = clamp(ch.hp + choice.heal, 0, ch.maxHp);
    if (choice.hurt) ch.hp = clamp(ch.hp - choice.hurt, 1, ch.maxHp);
    if (choice.effect) choice.effect(ch);
    refresh();

    if (choice.combat && node.combat) {
      const spec = node.combat;
      SF.combat.start(spec, ch, {
        onWin: () => { renderInventory(); goNode(spec.win); },
        onLose: () => { renderInventory(); goNode(spec.lose); }
      });
      renderInventory();
      return;
    }

    if (choice.to) goNode(choice.to);
  }

  /* ---------------- persistence ---------------- */

  function autosave() {
    if (!ch || slotIndex < 0) return;
    ch.playtime = (ch.playtime || 0) + Math.round((Date.now() - sessionStart) / 1000);
    sessionStart = Date.now();
    SF.storage.save(slotIndex, ch);
  }

  function manualSave() {
    autosave();
    toast('DOSSIER WRITTEN TO REGISTRY', 'good');
    SF.audio.sfx.confirm();
  }

  async function quit() {
    if (SF.combat.isActive()) { toast('CANNOT DISENGAGE MID-ENGAGEMENT', 'bad'); SF.audio.sfx.deny(); return; }
    const ok = await confirmDialog('DISENGAGE',
      'Progress is written to the registry at every waypoint. Return to the crew registry?', 'DISENGAGE');
    if (!ok) return;
    autosave();
    SF.audio.sfx.back();
    renderSlots();
    show('slots');
  }

  /* ---------------- wiring ---------------- */

  function bind() {
    $$('[data-go]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const dest = btn.dataset.go;
        SF.audio.sfx[dest === 'title' ? 'back' : 'click']();
        if (dest === 'slots') renderSlots();
        if (dest === 'codex') renderCodex();
        show(dest);
      });
    });

    $('#name-input').addEventListener('input', validateEnlist);
    $('#name-input').addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && !$('#btn-enlist').disabled) enlist();
    });
    $('#name-random').addEventListener('click', () => {
      $('#name-input').value = pick(CALLSIGNS);
      SF.audio.sfx.click();
      validateEnlist();
    });
    $('#btn-enlist').addEventListener('click', enlist);
    $('#btn-save').addEventListener('click', manualSave);
    $('#btn-quit').addEventListener('click', quit);

    // click anywhere in the narrative to skip the typewriter
    $('#stage-scroll').addEventListener('click', () => SF.fx.skipTyping());
    window.addEventListener('beforeunload', autosave);
  }

  SF.ui = { runBoot, skipBoot, bind, show, refresh, renderSlots, goNode,
            get character() { return ch; } };
})(window.SF);
