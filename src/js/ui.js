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
  let dossier = null;     // live 3D operative preview on the armoury screen
  let bootAborted = false;

  /* ---------------- screens ---------------- */

  function show(name) {
    $$('.screen').forEach((s) => s.classList.remove('active'));
    const target = $('#screen-' + name);
    if (target) target.classList.add('active');
    SF.cursor.reset();
  }

  /* Is anything on the page at all? Every path that hides the screens —
     dropping into a mission, the debrief — is supposed to put something else
     up. If one of them throws first, the player is left facing a blank page
     with nothing to click, and a reload only starts the same sequence again.
     So: a way back, and something that calls it. */
  function nothingVisible() {
    if (document.body.classList.contains('in-mission')) return false;
    if (document.body.classList.contains('booting')) return false;
    if (!$('#loading').hidden) return false;
    return !$$('.screen').some((s) => s.classList.contains('active'));
  }

  function recover(why) {
    $('#loading').hidden = true;
    $('#engage').hidden = true;
    $('#pause-menu').hidden = true;
    document.body.classList.remove('in-mission', 'booting');
    $('#gl').hidden = true;
    if (mission) { try { mission.destroy(); } catch (e) { /* already gone */ } mission = null; }
    if (ch && slotIndex >= 0) openCampaign(slotIndex);
    else { renderSlots(); show('slots'); }
    if (why) toast('RECOVERED — ' + why, 'bad');
  }

  /* Anything that escapes to the top level takes the UI down with it unless
     something puts a screen back up. */
  function installSafetyNet() {
    const trip = (label) => {
      if (!nothingVisible()) return;
      recover(label);
    };
    window.addEventListener('error', (e) => trip((e.message || 'ERROR').slice(0, 60)));
    window.addEventListener('unhandledrejection', (e) => {
      const m = e && e.reason && (e.reason.message || String(e.reason));
      trip((m || 'FAILED').slice(0, 60));
    });
    // and a slow backstop, for a blank page nothing reported
    setInterval(() => { if (nothingVisible()) recover('BLANK SCREEN'); }, 4000);
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
          await wait(2);
        }
        row.innerHTML = line;
      }
      await wait(line ? 34 : 60);
    }
    await wait(320);
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

    SF.gear.ensure(ch);
    const cls = SF.classes.CLASSES[ch.cls];
    $('#camp-who').textContent =
      `${ch.name} · ${cls.name} · RANK ${ch.level} · POWER ${SF.gear.powerOfCharacter(ch)}`;

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

    /* The station is always open. It is the one place on this screen that
       is not a fight, so it goes first rather than buried under the list. */
    const hubRow = el('button', 'camp-row station');
    hubRow.dataset.hover = 'DOCK';
    hubRow.innerHTML =
      '<div class="camp-n">⌂</div>' +
      '<div>' +
        '<div class="camp-name">THE CRADLE</div>' +
        '<div class="camp-obj">INTERNATIONAL SPACE STATION · DIVISION HOME PORT</div>' +
        '<div class="camp-diff"><i></i><i></i><i></i><i></i><i></i></div>' +
      '</div>' +
      '<div class="camp-state next">OPEN</div>';
    hubRow.addEventListener('click', () => { missionIndex = 'cradle'; drop(); });
    body.insertBefore(hubRow, body.firstChild);

    /* Orbital destinations open once the habitat ring is behind you. */
    const skyOpen = SF.planets.unlocked(ch);
    for (const dest of SF.planets.DESTINATIONS) {
      const best = (ch.expeditions || {})[dest.id] || 0;
      const row = el('button', 'camp-row destination');
      row.disabled = !skyOpen;
      row.dataset.hover = skyOpen ? 'BRIEF' : 'LOCKED';
      row.innerHTML =
        '<div class="camp-n">◎</div>' +
        '<div>' +
          `<div class="camp-name">${dest.name}</div>` +
          `<div class="camp-obj">${dest.sub}</div>` +
          '<div class="camp-diff">' + [1,2,3,4,5].map(() => '<i class="on"></i>').join('') + '</div>' +
        '</div>' +
        `<div class="camp-state ${skyOpen ? 'next' : 'locked'}">` +
          (skyOpen ? (best ? 'BEST WAVE ' + best : 'OPEN')
                   : 'CLEAR MISSION ' + SF.planets.UNLOCK_AFTER) +
        '</div>';
      if (skyOpen) row.addEventListener('click', () => openDestination(dest.id));
      body.appendChild(row);
    }

    SF.audio.sfx.open();
    show('campaign');
  }

  function openDestination(id) {
    const dest = SF.planets.byId(id);
    missionIndex = id;
    const cls = SF.classes.CLASSES[ch.cls];
    const wep = ch.equipped && ch.equipped.weapon;
    $('#brief-body').innerHTML =
      `<div class="brief-head" style="--accent:${cls.accent}">` +
        `<div class="bh-glyph">${cls.glyph}</div>` +
        `<div><div class="bh-name">${ch.name}</div>` +
        `<div class="bh-cls">${cls.name} · RANK ${ch.level} · POWER ${SF.gear.powerOfCharacter(ch)}</div>` +
        `<div class="bh-kit">${wep ? wep.name : SF.weapons.WEAPONS[ch.cls].name}</div></div>` +
      '</div>' +
      '<div class="brief-text">' +
        `<p class="sys">ORBITAL DESTINATION — ${dest.name}<br>${dest.sub}<br>` +
        'FORMAT: ENDLESS WAVES. EXTRACTION OPENS AT WAVE 5.<br>' +
        'REWARD: SALVAGE SCALES WITH THE WAVE YOU REACH.</p>' +
        `<p>${dest.brief}</p>` +
        '<p class="alert">STAND ON THE BEACON AND HOLD F TO CALL THE CUTTER. ' +
        'HARNESS CHARGES APPLY HERE.</p>' +
      '</div>';
    SF.audio.sfx.open();
    show('brief');
  }

  /* ---------------- squad link ---------------- */

  function openCoop() {
    if (!ch) return;
    renderCoop();
    show('coop');
    SF.audio.sfx.open();
  }

  function renderCoop() {
    const net = SF.net;
    const status = $('#coop-status');
    const online = net.active;

    status.className = 'coop-status' + (online ? ' online'
      : net.status === 'connecting' ? ' connecting' : '');
    status.textContent = online
      ? `CONNECTED · ROOM ${net.room} · ${net.isHost ? 'HOSTING' : 'GUEST'}` +
        (net.latency ? ` · ${net.latency}ms` : '')
      : net.status === 'connecting' ? 'CONNECTING…' : 'OFFLINE';

    $('#coop-code').hidden = !online;
    $('#coop-code-value').textContent = online ? net.room : '—';
    $('#btn-leave').hidden = !online;
    $('#btn-host').disabled = online;
    $('#btn-join').disabled = online;

    $('#coop-note').innerHTML = online
      ? 'Everyone in the room drops together — start a mission and your squad follows you in.'
      : (location.protocol === 'file:'
        ? 'Squad link needs the game served by its own server. Run <code>node server/server.js</code> ' +
          'and open <code>http://localhost:8080</code>.'
        : 'Open a room and share the code, or type a friend\'s code to join them. Up to four.');

    const list = $('#squad-list');
    list.innerHTML = '';
    if (!online) { list.appendChild(el('div', 'squad-empty', 'NOT CONNECTED')); return; }

    const rows = [{ id: net.id, name: ch.name, cls: ch.cls,
                    power: SF.gear.powerOfCharacter(ch), isHost: net.isHost, self: true }]
      .concat(net.roster());

    for (const p of rows) {
      const cls = SF.classes.CLASSES[p.cls] || SF.classes.CLASSES.bulwark;
      const row = el('div', 'squad-row');
      row.style.setProperty('--accent', cls.accent);
      row.innerHTML =
        `<div class="sq-glyph">${cls.glyph}</div>` +
        `<div><div class="sq-name">${p.name}${p.self ? ' (YOU)' : ''}</div>` +
        `<div class="sq-sub">${cls.name}${p.isHost ? ' · HOST' : ''}</div></div>` +
        `<div class="sq-power">${p.power || 0}</div>`;
      list.appendChild(row);
    }
  }

  async function connectCoop(code) {
    try {
      $('#coop-status').className = 'coop-status connecting';
      $('#coop-status').textContent = 'CONNECTING…';
      await SF.net.connect(ch, code || null);
      SF.audio.sfx.confirm();
      toast('SQUAD LINK ESTABLISHED — ROOM ' + SF.net.room, 'good');
    } catch (err) {
      $('#coop-status').className = 'coop-status error';
      $('#coop-status').textContent = 'NO LINK — ' + err.message.toUpperCase();
      SF.audio.sfx.deny();
    }
    renderCoop();
  }

  /* ---------------- armoury ---------------- */

  /* ---------------- contracts ---------------- */
  function openContracts() {
    if (!ch) return;
    const B = SF.bounties;
    B.ensure(ch);
    B.restock(ch);
    $('#ct-who').textContent = `${ch.name} · ${ch.contractsDone} COMPLETED`;
    const ctPurse = $('#ct-purse');
    if (ctPurse) { ctPurse.innerHTML = ''; ctPurse.appendChild(purseStrip()); }
    show('contracts');
    renderContracts();
    SF.audio.sfx.open();
  }

  /* One card shape for both columns: the difference is the button on it. */
  function contractCard(b, mode) {
    const B = SF.bounties;
    const done = B.complete(b);
    const pct = Math.min(100, Math.round((b.have / b.need) * 100));
    const r = B.reward(b);
    const card = el('div', 'ct-card' + (done ? ' done' : ''));
    card.style.setProperty('--tc', B.TIER_COLOUR[b.tier]);
    card.innerHTML =
      `<div class="ct-name">${b.name}<span class="ct-tier">${B.TIER_NAMES[b.tier]}</span></div>` +
      `<div class="ct-text">${b.text}</div>` +
      (mode === 'active'
        ? `<div class="ct-bar"><i style="width:${pct}%"></i></div>` +
          `<div class="ct-prog">${b.have} / ${b.need}${done ? ' &nbsp;·&nbsp; READY' : ''}</div>`
        : `<div class="ct-pay">${r.xp} XP &nbsp;·&nbsp; ${r.parts} PARTS &nbsp;·&nbsp; SALVAGE</div>`);
    return card;
  }

  function renderContracts() {
    const B = SF.bounties;
    B.ensure(ch);
    const active = $('#ct-active');
    const board = $('#ct-board');
    active.innerHTML = '';
    board.innerHTML = '';

    $('#ct-slots').textContent = ch.bounties.length + ' / ' + B.MAX_ACTIVE;
    $('#ct-done').textContent = ch.board.length + ' OFFERED';
    const rr = $('#btn-reroll');
    if (rr) {
      const short = SF.economy.shortOf(ch, SF.economy.PRICE.reroll);
      rr.textContent = 'NEW BOARD · ' + SF.economy.priceText(SF.economy.PRICE.reroll);
      rr.disabled = !!short;
      rr.dataset.hover = short ? 'NOT ENOUGH CHITS' : 'CLEAR AND REPOST';
    }

    if (!ch.bounties.length) {
      active.appendChild(el('div', 'item-empty', 'NOTHING TAKEN. THE BOARD IS TO YOUR RIGHT.'));
    }
    for (const b of ch.bounties) {
      const card = contractCard(b, 'active');
      const drop = el('button', 'ct-btn ghost', 'ABANDON');
      drop.dataset.hover = 'GIVE IT BACK';
      drop.addEventListener('click', () => {
        B.abandon(ch, b.uid);
        B.refreshBoard(ch);
        SF.storage.save(slotIndex, ch);
        SF.audio.sfx.back();
        renderContracts();
      });
      card.appendChild(drop);
      active.appendChild(card);
    }

    const full = ch.bounties.length >= B.MAX_ACTIVE;
    if (!ch.board.length) board.appendChild(el('div', 'item-empty', 'THE BOARD IS EMPTY.'));
    for (const b of ch.board) {
      const card = contractCard(b, 'board');
      const take = el('button', 'ct-btn', full ? 'NO ROOM' : 'TAKE');
      take.disabled = full;
      take.dataset.hover = full ? 'FINISH ONE FIRST' : 'ACCEPT';
      take.addEventListener('click', () => {
        if (!B.accept(ch, b.uid)) return;
        SF.storage.save(slotIndex, ch);
        SF.audio.sfx.confirm();
        renderContracts();
      });
      card.appendChild(take);
      board.appendChild(card);
    }

    const ready = B.readyCount(ch);
    const claim = $('#btn-claim');
    claim.disabled = !ready;
    claim.textContent = ready ? 'COLLECT ' + ready + ' CONTRACT' + (ready === 1 ? '' : 'S')
                              : 'NOTHING READY';
  }

  /* The Rook takes anything you are not wearing and pays in parts. Rare and
     better is left alone unless you say otherwise, because losing a good roll
     to a careless keypress is not a mistake worth allowing. */
  function appraise() {
    const G = SF.gear;
    const preview = G.breakdownPreview(ch, 'uncommon');
    if (!preview.count) {
      said('THE ROOK', 'Nothing here I would take. Come back with worse.');
      return;
    }
    const out = G.breakdown(ch, 'uncommon');
    SF.storage.save(slotIndex, ch);
    SF.audio.sfx.confirm();
    const parts = Object.keys(out.parts).filter((k) => out.parts[k])
      .map((k) => out.parts[k] + '× ' + G.partOf(k).name).join(', ');
    said('THE ROOK', `${out.count} pieces down to ${out.total} parts and ` +
                     `${SF.economy.priceText(out.purse)}. ${parts}.`);
    renderArmoury();
  }

  function claimContracts() {
    const out = SF.bounties.claim(ch);
    if (!out) return;
    const notes = SF.classes.grantXp(ch, out.xp);
    SF.storage.save(slotIndex, ch);
    SF.audio.sfx.win();
    const parts = Object.keys(out.parts).filter((k) => out.parts[k])
      .map((k) => out.parts[k] + '× ' + SF.gear.partOf(k).name).join(', ');
    said('SHAW', `${out.count} settled. ${SF.economy.priceText(out.purse)}, ` +
                 `${out.xp} experience, ${parts || 'no parts'}, ` +
                 `${out.drops.length} piece${out.drops.length === 1 ? '' : 's'} of salvage.` +
                 (notes.length ? ' ' + notes.join(' ') : ''));
    renderContracts();
  }

  /* The purse, drawn the same way wherever it appears. */
  function purseStrip() {
    const E = SF.economy;
    E.ensure(ch);
    const row = el('div', 'purse-row');
    row.innerHTML = E.CURRENCIES.map((c) =>
      `<span class="pr-c" style="--pc:${c.colour}" title="${c.line}">` +
      `<b>${ch.purse[c.id] || 0}</b><i>${c.name}</i></span>`).join('');
    return row;
  }

  /* Voss's store. Chits are the answer to "I did not get the drop I wanted",
     which is the job a soft currency exists to do. */
  function buildStore(ch) {
    const E = SF.economy, G = SF.gear;
    const store = el('div', 'store');
    store.innerHTML = '<div class="bench-head"><span>THE STORE</span>' +
                      '<span class="bh-stock">VOSS TAKES CHITS</span></div>';
    const rows = el('div', 'bench-rows');

    const stock = [
      { name: 'RUNNER PART', desc: 'One part over the counter, whatever is on the shelf.',
        cost: E.PRICE.part, take: () => {
          const pick = G.PARTS[Math.floor(Math.random() * G.PARTS.length)];
          const bundle = {}; bundle[pick.id] = 1;
          G.grantParts(ch, bundle);
          return '1× ' + pick.name;
        } },
      { name: 'PARTS CRATE', desc: 'Five parts, mixed, cheaper by the box.',
        cost: E.PRICE.partBundle, take: () => {
          const bundle = G.rollParts(5);
          G.grantParts(ch, bundle);
          return Object.keys(bundle).filter((k) => bundle[k])
            .map((k) => bundle[k] + '× ' + G.partOf(k).name).join(', ');
        } },
      { name: 'SALVAGE CASE', desc: 'Two rolls of gear at your rank. No promises.',
        cost: E.PRICE.salvageCase, take: () => {
          const drops = G.rollDrops(ch, Math.min(16, 4 + ch.level), false);
          G.grant(ch, drops);
          return drops.map((d) => d.name).join(', ');
        } },
      { name: 'PRIME, TRADED', desc: 'A prime at the price of a prime. Voss is not a charity.',
        cost: E.PRICE.primeTrade, take: () => { E.earn(ch, { prime: 1 }); return '1× PRIME'; } }
    ];

    for (const item of stock) {
      const short = E.shortOf(ch, item.cost);
      const row = el('div', 'bench-row' + (short ? ' blocked' : ''));
      row.innerHTML =
        '<div class="br-main">' +
          `<div class="br-name">${item.name}</div>` +
          `<div class="br-desc">${item.desc}</div>` +
          `<div class="br-gain">${E.priceText(item.cost)}</div>` +
        '</div><div class="br-side"></div>';
      const buy = el('button', 'br-fit', short ? 'NEED ' + short.name : 'BUY');
      buy.disabled = !!short;
      buy.dataset.hover = short ? 'NOT ENOUGH' : 'PAY VOSS';
      buy.addEventListener('click', () => {
        if (!E.spend(ch, item.cost)) return;
        const got = item.take();
        SF.storage.save(slotIndex, ch);
        SF.audio.sfx.confirm();
        said('VOSS', got + '. Pleasure.');
        renderArmoury();
      });
      row.querySelector('.br-side').appendChild(buy);
      rows.appendChild(row);
    }
    store.appendChild(rows);
    return store;
  }

  /* The wardrobe. Chits buy paint, and paint changes no number at all —
     which is exactly what a soft currency should be able to buy, or it is
     only a slower route to power. Two in each set are earned instead. */
  function buildWardrobe(ch) {
    const C = SF.cosmetics, E = SF.economy;
    C.ensure(ch);
    const box = el('div', 'wardrobe');
    box.innerHTML = '<div class="bench-head"><span>THE WARDROBE</span>' +
                    '<span class="bh-stock">PAINT ONLY. NOTHING HERE MAKES YOU STRONGER.</span></div>';

    for (const set of C.SETS) {
      const head = el('div', 'wd-set');
      head.innerHTML = `<span>${set.name}</span><em>${set.line}</em>`;
      box.appendChild(head);

      const grid = el('div', 'wd-grid');
      for (const item of set.list) {
        const owned = C.owns(ch, set.id, item.id);
        const wornNow = ch.worn[set.id] === item.id;
        const block = C.blocker(ch, set.id, item.id);
        const a = item.suit || item.shell || '#4a525c';
        const bcol = item.trim || item.glow || '#9fb0c0';

        const card = el('button', 'wd-card' + (wornNow ? ' on' : '') + (owned ? '' : ' locked'));
        card.style.setProperty('--a', a);
        card.style.setProperty('--b', bcol);
        card.innerHTML =
          '<span class="wd-chip"><i></i><b></b></span>' +
          `<span class="wd-name">${item.name}</span>` +
          `<span class="wd-foot">${
            wornNow ? 'WORN'
            : owned ? 'OWNED'
            : item.price == null ? (item.earn || 'EARNED')
            : E.priceText({ chits: item.price })}</span>`;
        card.dataset.hover = wornNow ? 'ALREADY ON' : owned ? 'WEAR IT'
                            : item.price == null ? 'NOT YET' : 'BUY AND WEAR';
        card.disabled = wornNow || (!owned && !!block);
        card.title = item.note || item.earn || '';
        card.addEventListener('click', () => {
          if (!owned && !C.buy(ch, set.id, item.id)) return;
          C.wear(ch, set.id, item.id);
          SF.storage.save(slotIndex, ch);
          SF.audio.sfx.confirm();
          if (!owned) said('VOSS', item.name + '. Wear it in good health.');
          renderArmoury();
          if (dossier) dossier.setLook(ch);
        });
        grid.appendChild(card);
      }
      box.appendChild(grid);
    }
    return box;
  }

  function openArmoury() {
    if (!ch) return;
    SF.gear.ensure(ch);
    SF.cosmetics.ensure(ch);
    const cls = SF.classes.CLASSES[ch.cls];
    $('#arm-who').textContent = `${ch.name} · ${cls.name} · RANK ${ch.level}`;
    show('armoury');

    if (!dossier) dossier = SF.dossier.create($('#dossier-canvas'));
    dossier.start();
    renderArmoury();
    SF.audio.sfx.open();
  }

  function renderArmoury() {
    /* ---- the bench ----
       Parts are a pool, not a slot: you hold a stock of coils, braces,
       plates and cells, and you decide which frame gets them. What a frame
       will take is capped by its rarity, so a better frame is worth more
       than its own numbers. Stripping gives everything back, so committing
       parts is never a mistake you cannot undo. */
    function buildBench(ch, runner) {
      const G = SF.gear;
      const bench = el('div', 'bench');
      const stock = ch.parts || {};
      const held = G.PARTS.reduce((a, p) => a + (stock[p.id] || 0), 0);

      const head = el('div', 'bench-head');
      head.innerHTML = '<span>UPGRADE BENCH</span>' +
        `<span class="bh-stock">${held} PART${held === 1 ? '' : 'S'} · ` +
        `${SF.economy.balance(ch, 'prime')} PRIME</span>`;
      bench.appendChild(head);

      if (!runner) {
        bench.appendChild(el('div', 'item-empty', 'EQUIP A RUNNER TO FIT PARTS'));
        return bench;
      }

      const cap = G.partCap(runner);
      const rows = el('div', 'bench-rows');
      for (const part of G.PARTS) {
        const fitted = G.fittedCount(runner, part.id);
        const have = stock[part.id] || 0;
        const blocker = G.partBlocker(ch, runner, part.id);

        const row = el('div', 'bench-row' + (blocker ? ' blocked' : ''));
        const pips = Array.from({ length: cap }, (_, i) =>
          `<i class="${i < fitted ? 'on' : ''}"></i>`).join('');
        row.innerHTML =
          `<div class="br-main">` +
            `<div class="br-name">${part.name}<span class="br-have">×${have}</span></div>` +
            `<div class="br-desc">${part.desc}</div>` +
            `<div class="br-gain">${part.line.replace('{v}', part.per)} each &nbsp;·&nbsp; ` +
              `NOW ${part.line.replace('{v}', fitted * part.per)}` +
              `<span class="br-cost">${SF.economy.priceText(SF.economy.PRICE.fitPart)} TO FIT</span></div>` +
          `</div>` +
          `<div class="br-side"><div class="br-pips">${pips}</div></div>`;

        const fit = el('button', 'br-fit');
        fit.textContent = blocker || 'FIT';
        fit.disabled = !!blocker;
        fit.dataset.hover = blocker || 'FIT ONE';
        fit.addEventListener('click', () => {
          if (!G.installPart(ch, runner, part.id)) return;
          SF.storage.save(slotIndex, ch);
          SF.audio.sfx.confirm();
          renderArmoury();
        });
        row.querySelector('.br-side').appendChild(fit);
        rows.appendChild(row);
      }
      bench.appendChild(rows);

      const total = G.PARTS.reduce((a, p) => a + G.fittedCount(runner, p.id), 0);
      const strip = el('button', 'bench-strip');
      const stripCost = SF.economy.priceText(SF.economy.PRICE.strip);
      strip.textContent = total ? 'STRIP FRAME — RECOVER ' + total + ' · ' + stripCost
                                : 'NOTHING FITTED';
      strip.disabled = !total || !SF.economy.canAfford(ch, SF.economy.PRICE.strip);
      strip.dataset.hover = 'RECOVER EVERY PART';
      strip.addEventListener('click', () => {
        if (G.stripParts(ch, runner) < 0) return;
        SF.storage.save(slotIndex, ch);
        SF.audio.sfx.back();
        renderArmoury();
      });
      bench.appendChild(strip);
      return bench;
    }

    const G = SF.gear;
    $('#arm-power').textContent = G.powerOfCharacter(ch);
    const a = G.armourStats(ch);
    $('#arm-power-sub').textContent =
      `+${a.hp} VITALS · ${a.resist.toFixed(0)}% RESIST · RANK ${ch.level}`;
    const purseBox = $('#arm-purse');
    if (purseBox) { purseBox.innerHTML = ''; purseBox.appendChild(purseStrip()); }

    /* ---- equipment, one block per slot ---- */
    const gearBox = $('#arm-gear');
    gearBox.innerHTML = '';
    for (const slot of G.SLOTS) {
      const owned = ch.inventory.filter((it) => it.slot === slot.id)
                               .sort((x, y) => y.power - x.power);
      const equipped = ch.equipped[slot.id];
      const block = el('div', 'slot-block');
      block.innerHTML =
        `<div class="slot-head"><span>${slot.name}</span>` +
        `<span class="sh-equipped">${equipped ? equipped.name : 'NOTHING EQUIPPED'}</span></div>`;

      const list = el('div', 'item-list');
      if (!owned.length) list.appendChild(el('div', 'item-empty', 'NO SALVAGE OF THIS TYPE YET'));

      for (const it of owned) {
        const r = G.rarityOf(it.rarity);
        const on = equipped && equipped.uid === it.uid;
        const btn = el('button', 'item' + (on ? ' on' : ''));
        btn.style.setProperty('--rc', r.colour);
        btn.dataset.hover = on ? 'EQUIPPED' : 'EQUIP';
        btn.innerHTML =
          `<span class="item-pow">${it.power}</span>` +
          `<div class="item-name">${it.name}</div>` +
          `<div class="item-meta">${r.name}${it.tier > 1 ? ' · TIER ' + it.tier : ''}` +
            (it.slot === 'runner'
              ? ' · ' + G.PARTS.reduce((a, p) => a + G.fittedCount(it, p.id), 0) +
                '/' + (G.partCap(it) * G.PARTS.length) + ' PARTS'
              : '') + `</div>` +
          (it.affixes.length
            ? `<div class="item-affix">${it.affixes.map((x) => '› ' + x.label).join('<br>')}</div>`
            : '');
        btn.addEventListener('click', () => {
          ch.equipped[slot.id] = it;
          SF.storage.save(slotIndex, ch);
          SF.audio.sfx.confirm();
          renderArmoury();
          dossier.setLook(ch);
        });
        list.appendChild(btn);
      }
      block.appendChild(list);
      if (slot.id === 'runner') block.appendChild(buildBench(ch, equipped));
      gearBox.appendChild(block);
    }

    gearBox.appendChild(buildStore(ch));
    gearBox.appendChild(buildWardrobe(ch));

    /* ---- appearance ---- */
    const look = $('#arm-look');
    look.innerHTML = '';

    const swatchRow = (label, colours, key) => {
      const row = el('div', 'look-row');
      row.innerHTML = `<span class="lr-label">${label}</span>`;
      const box = el('div', 'swatches');
      colours.forEach((c, i) => {
        const sw = el('button', 'swatch' + (ch.look[key] === i ? ' on' : ''));
        sw.style.setProperty('--c', c);
        sw.dataset.hover = label;
        sw.addEventListener('click', () => {
          ch.look[key] = i;
          SF.storage.save(slotIndex, ch);
          SF.audio.sfx.click();
          renderArmoury();
          dossier.setLook(ch);
        });
        box.appendChild(sw);
      });
      row.appendChild(box);
      return row;
    };

    look.appendChild(swatchRow('SKIN TONE', SF.gear.SKINS, 'skin'));
    look.appendChild(swatchRow('HAIR COLOUR', SF.gear.HAIR_COLOURS, 'hairColour'));

    const styles = el('div', 'look-row');
    styles.innerHTML = '<span class="lr-label">HAIR STYLE</span>';
    const sBox = el('div', 'swatches');
    SF.gear.HAIR_STYLES.forEach((st, i) => {
      const btn = el('button', 'style-btn' + (ch.look.hair === i ? ' on' : ''), st.name);
      btn.dataset.hover = st.name;
      btn.addEventListener('click', () => {
        ch.look.hair = i;
        SF.storage.save(slotIndex, ch);
        SF.audio.sfx.click();
        renderArmoury();
        dossier.setLook(ch);
      });
      sBox.appendChild(btn);
    });
    styles.appendChild(sBox);
    look.appendChild(styles);

    dossier.setLook(ch);
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

    const steps = missionIndex === 'cradle'
      ? [['MATCHING ORBIT', 0.3], ['HARD DOCK', 0.6],
         ['PRESSURISING COLLAR', 0.85], ['WELCOME BACK', 1]]
      : [['GENERATING HULL SURFACES', 0.25],
         ['BUILDING DECK GEOMETRY', 0.5],
         ['SEEDING HOSTILE PATTERNS', 0.72],
         ['SPINNING UP OPTICS', 0.9],
         ['LINK ESTABLISHED', 1]];
    for (const [label, pct] of steps) {
      $('#lo-step').textContent = label;
      $('#lo-fill').style.width = (pct * 100) + '%';
      await wait(140);
    }

    if (SF.net.active && SF.net.isHost) {
      const m = (typeof missionIndex === 'string')
        ? SF.planets.byId(missionIndex) : SF.campaign.byIndex(missionIndex);
      SF.net.send({ t: 'mission', d: { id: missionIndex, name: m ? m.name : '' } });
    }
    document.body.classList.add('in-mission');
    $('#gl').hidden = false;
    $('#weapon-name').textContent = (ch.equipped && ch.equipped.weapon)
      ? ch.equipped.weapon.name : SF.weapons.WEAPONS[ch.cls].name;

    // one frame for the browser to paint the loading state before the build
    await wait(60);
    try {
      mission = SF.game.create(ch, missionIndex, exitMission);
      window.__m = mission;             // handle for automated smoke tests
      mission.start();
    } catch (err) {
      /* A build that throws used to leave every screen hidden and the loading
         card up forever — a white page with no way out of it. */
      mission = null;
      $('#loading').hidden = true;
      recover((err && err.message ? err.message : 'INSERTION FAILED').slice(0, 60));
      return;
    }
    $('#loading').hidden = true;
    $('#engage').hidden = false;
  }

  /* A line from whoever you just dealt with, through the existing rail. */
  function said(who, text) { toast(`<b>${who}</b> &nbsp;${text}`, 'said'); }

  function exitMission(reason) {
    mission = null;
    document.body.classList.remove('in-mission');
    $('#gl').hidden = true;
    $('#pause-menu').hidden = true;
    $('#engage').hidden = true;
    $('#screen-end').classList.remove('active');
    if (!ch) { renderSlots(); show('slots'); return; }
    /* A station terminal names where it is sending you, so walking up to the
       armoury kiosk opens the armoury rather than dumping you on the list. */
    if (reason === 'armoury') { openCampaign(slotIndex); openArmoury(); return; }
    if (reason === 'coop') { openCampaign(slotIndex); openCoop(); return; }
    if (reason === 'contracts') { openCampaign(slotIndex); openContracts(); return; }
    if (reason === 'appraise') { openCampaign(slotIndex); openArmoury(); appraise(); return; }
    openCampaign(slotIndex);
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
        if (dest === 'campaign') { if (dossier) dossier.stop(); openCampaign(slotIndex); return; }
        if (dest === 'armoury') { openArmoury(); return; }
        if (dest === 'coop') { openCoop(); return; }
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
    $('#btn-armoury').addEventListener('click', openArmoury);
    $('#btn-coop').addEventListener('click', openCoop);
    $('#btn-host').addEventListener('click', () => connectCoop(null));
    $('#btn-join').addEventListener('click', () =>
      connectCoop(($('#room-input').value || '').trim().toUpperCase()));
    $('#room-input').addEventListener('keydown', (e) => {
      if (e.key === 'Enter') connectCoop(($('#room-input').value || '').trim().toUpperCase());
    });
    $('#btn-leave').addEventListener('click', () => {
      SF.net.disconnect();
      SF.audio.sfx.back();
      renderCoop();
    });

    /* Roster changes and a squadmate's mission choice both land here. */
    SF.net.on('players', () => { if ($('#screen-coop').classList.contains('active')) renderCoop(); });
    SF.net.on('status', () => { if ($('#screen-coop').classList.contains('active')) renderCoop(); });
    SF.net.on('mission', (d) => {
      if (!ch || !d) return;
      toast('SQUAD DROPPING — ' + (d.name || d.id), 'warn');
      missionIndex = d.id;
      drop();
    });

    $('#btn-resume').addEventListener('click', () => {
      if (!mission) return;
      $('#pause-menu').hidden = true;
      mission.pause(false);
      mission.requestLock();
    });
    $('#btn-claim').addEventListener('click', claimContracts);
    $('#btn-reroll').addEventListener('click', () => {
      if (!SF.bounties.reroll(ch)) return;
      SF.storage.save(slotIndex, ch);
      SF.audio.sfx.click();
      renderContracts();
    });
    $('#btn-abandon').addEventListener('click', abandon);
    $('#btn-debrief').addEventListener('click', () => { if (mission) mission.destroy(); });

    window.addEventListener('beforeunload', () => {
      if (ch && slotIndex >= 0) SF.storage.save(slotIndex, ch);
    });
    installSafetyNet();
    void sessionStart;
  }

  SF.ui = { runBoot, skipBoot, bind, show, renderSlots, openCampaign, openArmoury, openCoop,
            openContracts, renderContracts, appraise,
            get character() { return ch; } };
})(window.SF);
