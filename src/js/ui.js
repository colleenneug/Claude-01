/* ============================================================
   Menus: boot, title, crew registry (three slots), enlistment,
   codex, mission briefing — and the handoff into the mission.

   Everything outside a mission is cursor-driven; everything inside
   one is pointer-locked. This module owns the seam between them.
   ============================================================ */
(function (SF) {
  'use strict';
  const { $, $$, el, pick, wait, toast, confirmDialog } = SF.util;

  let ch = null;          // active character
  let slotIndex = -1;
  let draftClass = null;
  let sessionStart = 0;
  let mission = null;     // live SF.game instance
  let missionIndex = 1;   // which campaign mission is being flown
  let bootAborted = false;

  /* ---------------- screens ---------------- */

  function show(name) {
    $$('.screen').forEach((s) => s.classList.remove('active'));
    const target = $('#screen-' + name);
    if (target) target.classList.add('active');
    SF.cursor.reset();
  }

  /* ---------------- boot ---------------- */

  const BOOT_LINES = [
    ['RECOVERY DIVISION FIELD TERMINAL — REV 9.4', 'dim'],
    ['POST ................................ <span class="ok">PASS</span>', ''],
    ['NEURAL HARNESS LINK ................. <span class="ok">PASS</span>', ''],
    ['WEAPON AUTHORISATION ................ <span class="ok">GRANTED</span>', ''],
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

  function skipBoot() {
    if (bootAborted || !document.body.classList.contains('booting')) return;
    bootAborted = true;
    document.body.classList.remove('booting');
    show('title');
  }

  /* ---------------- registry ---------------- */

  function renderSlots() {
    const grid = $('#slot-grid');
    grid.innerHTML = '';

    SF.storage.all().forEach((data, i) => {
      const card = el('div', 'slot' + (data ? '' : ' empty'));
      const bay = `<div class="bay">BAY ${String(i + 1).padStart(2, '0')} // ${data ? 'OCCUPIED' : 'VACANT'}</div>`;

      if (!data) {
        card.innerHTML = bay +
          '<div class="slot-empty-mark">+</div>' +
          '<div class="slot-empty-txt">ENLIST OPERATIVE</div>';
        card.dataset.hover = 'ENLIST';
        card.addEventListener('click', () => openCreate(i));
      } else {
        const cls = SF.classes.CLASSES[data.cls];
        const wep = SF.weapons.WEAPONS[data.cls];
        card.innerHTML = bay +
          `<div class="slot-glyph" style="color:${cls.accent};border-color:${cls.accent}">${cls.glyph}</div>` +
          `<div class="slot-name">${data.name}</div>` +
          `<div class="slot-class" style="color:${cls.accent}">${cls.name} · ${cls.role}</div>` +
          '<div class="slot-meta">' +
            `<div class="row"><span>RANK</span><b>${data.level}</b></div>` +
            `<div class="row"><span>WEAPON</span><b>${wep.name}</b></div>` +
            `<div class="row"><span>PROGRESS</span><b>${(data.campaign && Object.keys(data.campaign.cleared || {}).length) || 0} / ${SF.campaign.LAST}</b></div>` +
            `<div class="row"><span>STATUS</span><b>${campaignStatus(data)}</b></div>` +
          '</div>' +
          '<div class="slot-actions">' +
            '<button class="btn btn-sm" data-act="deploy">DEPLOY</button>' +
            '<button class="btn btn-sm btn-danger hold-btn" data-act="erase" data-hold="1" ' +
              'data-hover="HOLD TO ERASE"><span class="hold-fill"></span>ERASE</button>' +
          '</div>';

        card.querySelector('[data-act="deploy"]').addEventListener('click', () => openCampaign(i));
        card.querySelector('[data-act="erase"]').addEventListener('holdcomplete', () => {
          SF.storage.erase(i);
          toast(`BAY ${i + 1} PURGED`, 'bad');
          renderSlots();
        });
      }
      grid.appendChild(card);
    });
  }

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
    const wep = SF.weapons.WEAPONS[id];
    const ab = SF.weapons.ABILITIES[id];
    const detail = $('#class-detail');
    detail.style.setProperty('--accent', cls.accent);
    detail.innerHTML =
      `<div class="cd-title">${cls.name}</div>` +
      `<div class="cc-role" style="color:${cls.accent}">${cls.role}</div>` +
      `<div class="cd-flavor">${cls.flavor}</div>` +

      '<div class="cd-sec">ISSUED WEAPON</div>' +
      '<div class="cd-ability">' +
        `<div class="an">${wep.name}</div>` +
        `<div class="ac">${wep.kind}</div>` +
        `<div class="ad">${wep.desc}</div>` +
      '</div>' +
      '<div class="cd-stats">' +
        `<div><span class="k">DAMAGE</span><span class="v">${wep.damage}${wep.pellets > 1 ? ' ×' + wep.pellets : ''}</span></div>` +
        `<div><span class="k">RPM</span><span class="v">${wep.rpm}</span></div>` +
        `<div><span class="k">MAGAZINE</span><span class="v">${wep.mag}</span></div>` +
        `<div><span class="k">HEAD SHOT</span><span class="v">×${wep.headMult}</span></div>` +
        `<div><span class="k">RANGE</span><span class="v">${wep.range} m</span></div>` +
        `<div><span class="k">VITALS</span><span class="v">${cls.base.hp}</span></div>` +
      '</div>' +

      '<div class="cd-sec">FIELD ABILITY — Q</div>' +
      '<div class="cd-ability">' +
        `<div class="an">${ab.name}</div>` +
        `<div class="ac">${ab.cooldown}s COOLDOWN</div>` +
        `<div class="ad">${ab.desc}</div>` +
      '</div>' +

      '<div class="cd-sec">PASSIVE — ' + cls.perk.name + '</div>' +
      `<div class="cd-perk">${cls.perk.desc}</div>`;

    SF.audio.sfx.confirm();
    validateEnlist();
  }

  function validateEnlist() {
    $('#btn-enlist').disabled = !(draftClass && $('#name-input').value.trim().length >= 2);
  }

  function enlist() {
    const name = $('#name-input').value.trim().toUpperCase();
    if (!draftClass || name.length < 2) { SF.audio.sfx.deny(); return; }
    SF.storage.save(slotIndex, SF.classes.makeCharacter(name, draftClass));
    toast(`${name} REGISTERED TO BAY ${slotIndex + 1}`, 'good');
    SF.audio.sfx.confirm();
    openCampaign(slotIndex);
  }

  /* ---------------- codex ---------------- */

  function renderCodex() {
    $('#codex-body').innerHTML = SF.story.CODEX
      .map((e) => `<div class="codex-entry"><h4>${e.t}</h4><p>${e.b}</p></div>`).join('');
  }

  /* ---------------- campaign ---------------- */

  function campaignStatus(data) {
    if (!data.campaign) return 'UNBLOODED';
    const done = Object.keys(data.campaign.cleared || {}).length;
    if (done >= SF.campaign.LAST) return 'ARK SILENCED';
    if (done === 0) return 'UNBLOODED';
    return 'MISSION ' + Math.min(SF.campaign.LAST, data.campaign.unlocked);
  }

  function openCampaign(i) {
    slotIndex = i;
    ch = SF.storage.get(i);
    if (!ch) return;
    ch.slot = i;
    SF.campaign.stateFor(ch);

    const cls = SF.classes.CLASSES[ch.cls];
    $('#camp-who').textContent = `${ch.name} · ${cls.name} · RANK ${ch.level}`;

    const body = $('#camp-body');
    body.innerHTML = '';
    for (const m of SF.campaign.MISSIONS) {
      const unlocked = SF.campaign.isUnlocked(ch, m.n);
      const cleared = SF.campaign.isCleared(ch, m.n);
      const isNext = unlocked && !cleared;

      const row = el('button', 'camp-row' + (m.boss ? ' boss' : ''));
      row.disabled = !unlocked;
      row.dataset.hover = unlocked ? 'BRIEF ' + m.n : 'LOCKED';
      // five pips of difficulty across ten missions
      const pips = Math.max(1, Math.round((m.n / SF.campaign.LAST) * 5));
      row.innerHTML =
        `<div class="camp-n">${String(m.n).padStart(2, '0')}</div>` +
        '<div>' +
          `<div class="camp-name">${m.name}</div>` +
          `<div class="camp-obj">${m.objective}</div>` +
          `<div class="camp-diff">${[1,2,3,4,5].map(k =>
            `<i class="${k <= pips ? 'on' : ''}"></i>`).join('')}</div>` +
        '</div>' +
        `<div class="camp-state ${cleared ? 'cleared' : unlocked ? (isNext ? 'next' : '') : 'locked'}">` +
          (cleared ? 'CLEARED' : unlocked ? (m.boss ? 'FINAL' : 'READY') : 'LOCKED') +
        '</div>';
      if (unlocked) row.addEventListener('click', () => openBrief(m.n));
      body.appendChild(row);
    }

    SF.audio.sfx.open();
    show('campaign');
  }

  /* ---------------- briefing ---------------- */

  function openBrief(n) {
    missionIndex = n;
    const m = SF.campaign.byIndex(n);
    const cls = SF.classes.CLASSES[ch.cls];
    const wep = SF.weapons.WEAPONS[ch.cls];
    const prev = m.from ? SF.campaign.MISSIONS.find((x) => x.zone === m.from) : null;

    $('#brief-body').innerHTML =
      `<div class="brief-head" style="--accent:${cls.accent}">` +
        `<div class="bh-glyph">${cls.glyph}</div>` +
        `<div><div class="bh-name">${ch.name}</div>` +
        `<div class="bh-cls">${cls.name} · ${cls.role} · RANK ${ch.level}</div>` +
        `<div class="bh-kit">${wep.name} — ${wep.kind} &nbsp;·&nbsp; ${SF.weapons.ABILITIES[ch.cls].name}</div></div>` +
      '</div>' +
      '<div class="brief-text">' +
        `<p class="sys">MISSION ${m.n} OF ${SF.campaign.LAST} — ${m.name}<br>` +
        `SECTOR: ${(SF.level.LAYOUT.spaces.find((sp) => sp.id === m.zone) || {}).name || '—'}<br>` +
        (prev ? `ENTRY: FROM ${prev.name}<br>` : 'ENTRY: DOCKING COLLAR, DORSAL<br>') +
        `OBJECTIVE: ${m.objective.toUpperCase()}</p>` +
        `<p>${m.brief}</p>` +
        (m.boss ? '<p class="alert">ITS SHIELD IS NOT ARMOUR AND CANNOT BE SHOT OFF. ' +
                  'IT SINGS A PHRASE ON THE FOUR RESONANCE NODES; SHOOT THEM BACK IN THE ' +
                  'SAME ORDER TO DROP IT.</p>' : '') +
        (m.n === 1 ? SF.story.BRIEF.body : '') +
      '</div>';

    SF.audio.sfx.open();
    show('brief');
  }

  /* ---------------- mission ---------------- */

  async function drop() {
    SF.audio.sfx.confirm();
    show('none');
    $('#loading').hidden = false;

    const steps = [
      ['GENERATING HULL SURFACES', 0.25],
      ['BUILDING DECK GEOMETRY', 0.5],
      ['SEEDING HOSTILE PATTERNS', 0.72],
      ['SPINNING UP OPTICS', 0.9],
      ['LINK ESTABLISHED', 1]
    ];
    for (const [label, pct] of steps) {
      $('#lo-step').textContent = label;
      $('#lo-fill').style.width = (pct * 100) + '%';
      await wait(140);
    }

    document.body.classList.add('in-mission');
    $('#gl').hidden = false;
    $('#weapon-name').textContent = SF.weapons.WEAPONS[ch.cls].name;

    // one frame for the browser to paint the loading state before the build
    await wait(60);
    mission = SF.game.create(ch, missionIndex, exitMission);
    window.__m = mission;               // handle for automated smoke tests
    $('#loading').hidden = true;
    mission.start();
    $('#engage').hidden = false;
  }

  function exitMission() {
    mission = null;
    document.body.classList.remove('in-mission');
    $('#gl').hidden = true;
    $('#pause-menu').hidden = true;
    $('#engage').hidden = true;
    $('#screen-end').classList.remove('active');
    if (ch) openCampaign(slotIndex); else { renderSlots(); show('slots'); }
  }

  async function abandon() {
    const ok = await confirmDialog('ABANDON MISSION',
      'Progress on this insertion is lost. The operative keeps their rank.', 'ABANDON', true);
    if (!ok) return;
    SF.storage.save(slotIndex, ch);
    mission.destroy();
  }

  /* ---------------- wiring ---------------- */

  function bind() {
    $('#screen-brief').querySelector('[data-go]').dataset.go = 'campaign';
    $$('[data-go]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const dest = btn.dataset.go;
        SF.audio.sfx[dest === 'title' ? 'back' : 'click']();
        if (dest === 'slots') renderSlots();
        if (dest === 'codex') renderCodex();
        if (dest === 'campaign') { openCampaign(slotIndex); return; }
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
    $('#btn-drop').addEventListener('click', drop);

    $('#engage').addEventListener('click', () => {
      if (!mission) return;
      $('#engage').hidden = true;
      SF.audio.unlock();
      mission.engage();
    });

    $('#btn-resume').addEventListener('click', () => {
      if (!mission) return;
      $('#pause-menu').hidden = true;
      mission.pause(false);
      mission.requestLock();
    });
    $('#btn-abandon').addEventListener('click', abandon);
    $('#btn-debrief').addEventListener('click', () => { if (mission) mission.destroy(); });

    window.addEventListener('beforeunload', () => {
      if (ch && slotIndex >= 0) SF.storage.save(slotIndex, ch);
    });
    void sessionStart;
  }

  SF.ui = { runBoot, skipBoot, bind, show, renderSlots, openCampaign,
            get character() { return ch; } };
})(window.SF);
