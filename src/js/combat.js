/* ============================================================
   Turn-based engagement engine.

   Order of a round:
     player acts (one ability or one item, or FOCUS)
       -> resolve, check for victory
       -> each living hostile executes its telegraphed intent
       -> check for defeat
       -> statuses tick, CORE regenerates, new intents roll
   ============================================================ */
(function (SF) {
  'use strict';
  const { $, $$, el, clamp, rand, chance, wait } = SF.util;

  let ch = null;            // the character record
  let enemies = [];
  let turn = 1;
  let busy = false;
  let onWin = null, onLose = null;
  let cooldowns = {};
  let awaitingTarget = null;   // ability pending a target pick
  let blindsideReady = false;

  /* ---------------- status helpers ---------------- */

  function addStatus(unit, key, turns, value) {
    const cur = unit.statuses[key];
    if (cur && key === 'TOXIN') {
      cur.turns = Math.max(cur.turns, turns);
      cur.value = (cur.value || 0) + (value || 0);
    } else {
      unit.statuses[key] = { turns: turns, value: value || 0 };
    }
  }
  const hasStatus = (unit, key) => !!unit.statuses[key];
  function dropStatus(unit, key) { delete unit.statuses[key]; }

  const HOSTILE_STATUSES = ['BLEED', 'EXPOSED', 'SILENCED', 'GLITCH', 'SUNDER', 'TOXIN', 'STAGGER'];

  function cleanse(unit) {
    for (const k of HOSTILE_STATUSES) {
      if (unit.statuses[k]) { delete unit.statuses[k]; return k; }
    }
    return null;
  }

  /* ---------------- damage maths ---------------- */

  function damageEnemy(target, amount, opts) {
    opts = opts || {};
    let amt = amount;
    if (opts.crit) amt = Math.round(amt * 2);
    if (hasStatus(ch, 'RESOLVE')) amt = Math.round(amt * 1.5);
    if (hasStatus(ch, 'PRIMED') && !opts.crit) {
      amt = Math.round(amt * 2);
      dropStatus(ch, 'PRIMED');
      opts.crit = true;
    }
    if (blindsideReady) {
      amt = Math.round(amt * 2);
      blindsideReady = false;
      opts.crit = true;
      log('BLINDSIDE — they never registered you were in the room.', 'crit');
    }

    if (!opts.ignoreArmor) {
      const armor = Math.max(0, target.armor - (hasStatus(target, 'SUNDER') ? 3 : 0));
      amt = Math.max(1, amt - armor);
    }
    if (!opts.pierce && target.shield > 0) {
      const absorbed = Math.min(target.shield, amt);
      target.shield -= absorbed;
      amt -= absorbed;
    }
    target.hp = Math.max(0, target.hp - amt);

    const node = nodeFor(target);
    if (node) {
      node.classList.add('hit-shake');
      setTimeout(() => node.classList.remove('hit-shake'), 320);
      SF.fx.floatNum(node, '-' + amt, opts.crit ? 'crit' : 'dmg');
    }
    SF.audio.sfx[opts.crit ? 'crit' : 'hit']();
    if (target.hp <= 0) kill(target);
    renderEnemies();
    return amt;
  }

  function damagePlayer(source, amount) {
    let amt = amount;
    if (source && hasStatus(source, 'GLITCH')) amt = Math.round(amt * 0.5);
    if (hasStatus(ch, 'EXPOSED')) amt = Math.round(amt * 1.4);

    if (hasStatus(ch, 'EVASION')) {
      dropStatus(ch, 'EVASION');
      log('EVADED — the strike passes through where you were.', 'buff');
      SF.audio.sfx.shield();
      return 0;
    }
    if (SF.classes.CLASSES[ch.cls].id === 'bulwark') amt = Math.max(1, amt - 3);
    if (hasStatus(ch, 'FORTIFY')) amt = Math.round(amt * 0.5);

    if (ch.shield > 0) {
      const absorbed = Math.min(ch.shield, amt);
      ch.shield -= absorbed;
      amt -= absorbed;
    }
    ch.hp = clamp(ch.hp - amt, 0, ch.maxHp);

    SF.fx.shake(amt > 16 ? 12 : 7);
    SF.fx.flash('red');
    SF.audio.sfx.hit();
    SF.ui.refresh();
    return amt;
  }

  function healPlayer(amount) {
    const before = ch.hp;
    ch.hp = clamp(ch.hp + amount, 0, ch.maxHp);
    SF.audio.sfx.heal();
    SF.ui.refresh();
    return ch.hp - before;
  }

  function kill(target) {
    target.dead = true;
    target.hp = 0;
    target.intent = null;
    log(`${target.name} is down.`, 'crit');
  }

  const alive = () => enemies.filter((e) => !e.dead);

  /* ---------------- logging ---------------- */

  function log(text, kind) {
    const box = $('#log');
    const line = el('div', 'log-line' + (kind ? ' ' + kind : ''), text);
    box.appendChild(line);
    box.scrollTop = box.scrollHeight;
    while (box.children.length > 90) box.removeChild(box.firstChild);
  }

  /* ---------------- ability context ---------------- */

  function context(target) {
    return {
      p: ch,
      target: target,
      get enemies() { return alive(); },
      log: log,
      chance: chance,
      rand: rand,
      damage: damageEnemy,
      heal: healPlayer,
      addShield(unit, amount) {
        if (unit === ch) { ch.shield += amount; SF.ui.refresh(); }
        else { unit.shield += amount; renderEnemies(); }
        SF.audio.sfx.shield();
      },
      status: addStatus,
      hasStatus: hasStatus,
      cleanse: cleanse,
      gainEnergy(n) { ch.energy = clamp(ch.energy + n, 0, ch.maxEnergy); SF.ui.refresh(); }
    };
  }

  /* Context handed to enemy moves. */
  function enemyContext() {
    return {
      log: log,
      hurtPlayer: (self, amt) => damagePlayer(self, amt),
      healEnemy: (self, amt) => { self.hp = Math.min(self.maxHp, self.hp + amt); renderEnemies(); },
      shieldEnemy: (self, amt) => { self.shield += amt; renderEnemies(); },
      statusPlayer: (key, turns, value) => { addStatus(ch, key, turns, value); SF.ui.refresh(); },
      drainEnergy: (n) => { ch.energy = clamp(ch.energy - n, 0, ch.maxEnergy); SF.ui.refresh(); },
      stripPlayerShield: () => { const s = ch.shield; ch.shield = 0; SF.ui.refresh(); return s; },
      summon: (id) => {
        if (enemies.length >= 4) return;
        const e = SF.enemies.spawn(id, 1);
        SF.enemies.rollIntent(e);
        enemies.push(e);
        renderEnemies();
      }
    };
  }

  /* ---------------- rendering ---------------- */

  const nodeFor = (enemy) => $(`.enemy[data-uid="${enemy.uid}"]`);

  function statusChips(unit) {
    return Object.keys(unit.statuses).map((k) => {
      const s = unit.statuses[k];
      const kind = ['TOXIN', 'BLEED', 'EXPOSED', 'SILENCED', 'SUNDER', 'GLITCH', 'STAGGER'].includes(k)
        ? (unit === ch ? 'bad' : 'warn') : 'good';
      return `<span class="status-chip ${kind}">${k}${s.turns ? ' ' + s.turns : ''}</span>`;
    }).join('');
  }

  function renderEnemies() {
    const board = $('#enemy-board');
    board.innerHTML = '';
    for (const e of enemies) {
      const node = el('button', 'enemy' + (e.dead ? ' dead' : ''));
      node.dataset.uid = e.uid;
      node.dataset.hover = e.dead ? 'DOWN' : 'TARGET';
      node.innerHTML =
        (e.intent && !e.dead
          ? `<div class="e-intent${e.intent.lethal ? ' lethal' : ''}">▲ ${e.intent.tell}</div>` : '') +
        `<div class="e-glyph">${e.glyph}</div>` +
        `<div class="e-name">${e.name}</div>` +
        `<div class="e-sub">${e.sub}</div>` +
        `<div class="e-bar"><div class="e-fill" style="width:${(e.hp / e.maxHp) * 100}%"></div></div>` +
        `<div class="e-hp">${e.hp} / ${e.maxHp}${e.armor ? ' &nbsp;ARM ' + e.armor : ''}</div>` +
        (e.shield ? `<div class="e-shield">SHIELD ${e.shield}</div>` : '') +
        `<div class="e-status">${statusChips(e)}</div>`;
      node.addEventListener('click', () => pickTarget(e));
      board.appendChild(node);
    }
    renderIntentStrip();
  }

  function renderIntentStrip() {
    const strip = $('#intent-strip');
    const live = alive();
    if (!live.length) { strip.innerHTML = ''; return; }
    strip.innerHTML = 'INCOMING: ' + live
      .map((e) => `<b>${e.name}</b> ▸ ${e.intent ? e.intent.tell : '—'}`)
      .join(' &nbsp;·&nbsp; ') +
      (Object.keys(ch.statuses).length ? ' &nbsp;||&nbsp; YOU: ' + statusChips(ch) : '');
  }

  function renderAbilities() {
    const bar = $('#ability-bar');
    bar.innerHTML = '';
    const list = SF.classes.CLASSES[ch.cls].abilities;
    list.forEach((ab, i) => {
      const cd = cooldowns[ab.id] || 0;
      const silenced = hasStatus(ch, 'SILENCED') && ab.ultimate;
      const usable = !busy && cd === 0 && ch.energy >= ab.cost && !silenced;
      const node = el('button', 'ability' + (ab.ultimate ? ' ultimate' : ''));
      node.disabled = !usable;
      node.dataset.hover = usable ? ab.name
        : busy ? 'RESOLVING'
        : cd ? 'COOLING ' + cd
        : silenced ? 'SILENCED'
        : 'NO CORE';
      node.innerHTML =
        `<span class="ab-key">${i + 1}</span>` +
        `<div class="ab-name">${ab.name}</div>` +
        `<div class="ab-cost">${ab.cost} CORE${ab.cd ? ' · CD ' + ab.cd : ''}</div>` +
        `<div class="ab-desc">${ab.desc}</div>` +
        (cd ? `<div class="ab-cd">${cd}</div>` : '');
      node.addEventListener('click', () => useAbility(ab));
      bar.appendChild(node);
    });
    bar.dataset.busy = busy ? '1' : '0';
    $('#turn-tag').textContent = 'TURN ' + turn;
    $('#btn-focus').disabled = busy;
  }

  /* ---------------- player actions ---------------- */

  function useAbility(ab) {
    if (busy) return;
    const cd = cooldowns[ab.id] || 0;
    if (cd > 0 || ch.energy < ab.cost) { SF.audio.sfx.deny(); return; }

    if (ab.target === 'enemy' && alive().length > 1) {
      awaitingTarget = ab;
      document.body.classList.add('targeting');
      SF.util.toast('SELECT A TARGET', 'warn');
      return;
    }
    commit(ab, ab.target === 'enemy' ? alive()[0] : null);
  }

  function pickTarget(enemy) {
    if (!awaitingTarget || enemy.dead) { if (awaitingTarget) SF.audio.sfx.deny(); return; }
    const ab = awaitingTarget;
    awaitingTarget = null;
    document.body.classList.remove('targeting');
    commit(ab, enemy);
  }

  async function commit(ab, target) {
    busy = true;
    ch.energy -= ab.cost;
    if (ab.cd) cooldowns[ab.id] = ab.cd + 1;   // decremented at end of this round
    SF.audio.sfx.ability();
    if (ab.ultimate) SF.fx.flash('cyan');
    SF.ui.refresh();
    renderAbilities();

    ab.run(context(target));
    renderEnemies();
    await wait(520);
    await endPlayerTurn();
  }

  async function useItem(key) {
    if (busy || !ch.items[key] || ch.items[key] <= 0) { SF.audio.sfx.deny(); return; }
    busy = true;
    ch.items[key]--;
    const item = ITEMS[key];
    item.use(context(alive()[0]));
    SF.ui.refresh();
    renderEnemies();
    renderAbilities();
    await wait(450);
    await endPlayerTurn();
  }

  async function focus() {
    if (busy) return;
    busy = true;
    ch.energy = clamp(ch.energy + 4, 0, ch.maxEnergy);
    log('You hold position and let the CORE spin back up. +4.', 'buff');
    SF.audio.sfx.confirm();
    SF.ui.refresh();
    renderAbilities();
    await wait(320);
    await endPlayerTurn();
  }

  /* ---------------- round resolution ---------------- */

  async function endPlayerTurn() {
    if (!alive().length) return finish(true);

    // hostile phase
    for (const e of alive()) {
      if (hasStatus(e, 'STAGGER')) {
        dropStatus(e, 'STAGGER');
        log(`${e.name} is staggered and loses its action.`, 'buff');
        renderEnemies();
        await wait(380);
        continue;
      }
      const move = e.intent || SF.enemies.rollIntent(e);
      SF.audio.sfx.enemy();
      const node = nodeFor(e);
      if (node) { node.classList.add('flash-white'); setTimeout(() => node.classList.remove('flash-white'), 240); }
      move.run(enemyContext(), e);
      renderEnemies();
      await wait(560);
      if (ch.hp <= 0) return finish(false);
    }

    // status ticks
    for (const e of alive()) {
      const tox = e.statuses.TOXIN;
      if (tox) {
        e.hp = Math.max(0, e.hp - tox.value);
        SF.fx.floatNum(nodeFor(e), '-' + tox.value, 'dmg');
        log(`${e.name} takes ${tox.value} from neuro toxin.`, 'dmg');
        if (e.hp <= 0) kill(e);
      }
      tickDown(e);
    }
    const bleed = ch.statuses.BLEED;
    if (bleed) {
      ch.hp = clamp(ch.hp - bleed.value, 0, ch.maxHp);
      log(`Bleeding for ${bleed.value}.`, 'dmg');
    }
    tickDown(ch);

    if (ch.hp <= 0) return finish(false);
    if (!alive().length) return finish(true);

    // core regeneration
    let regen = 3;
    if (ch.cls === 'oracle') regen += 2;
    if (hasStatus(ch, 'OVERCLOCK')) regen += 4;
    ch.energy = clamp(ch.energy + regen, 0, ch.maxEnergy);

    for (const k of Object.keys(cooldowns)) cooldowns[k] = Math.max(0, cooldowns[k] - 1);

    for (const e of alive()) SF.enemies.rollIntent(e);
    turn++;
    busy = false;
    SF.ui.refresh();
    renderEnemies();
    renderAbilities();
  }

  function tickDown(unit) {
    for (const k of Object.keys(unit.statuses)) {
      const s = unit.statuses[k];
      if (s.turns > 0) s.turns--;
      if (s.turns <= 0) delete unit.statuses[k];
    }
  }

  /* ---------------- lifecycle ---------------- */

  const ITEMS = {
    medgel: { name: 'MEDGEL', desc: 'Restore 34 vitals.',
      use: (c) => { const h = c.heal(34); c.log(`Medgel applied. +${h} vitals.`, 'heal'); } },
    pulse:  { name: 'PULSE CHARGE', desc: 'Throw for 22 damage, ignores armour.',
      use: (c) => { if (!c.target) { c.log('No target.', 'dmg'); return; }
                    const d = c.damage(c.target, 22, { ignoreArmor: true });
                    c.log(`Pulse charge detonates for ${d}.`, 'dmg'); } },
    cell:   { name: 'SHIELD CELL', desc: 'Gain 26 shield instantly.',
      use: (c) => { c.addShield(c.p, 26); c.log('Shield cell spent. +26 shield.', 'buff'); } }
  };

  function start(spec, character, callbacks) {
    ch = character;
    onWin = callbacks.onWin;
    onLose = callbacks.onLose;
    enemies = spec.enemies.map((id) => SF.enemies.spawn(id, spec.scale || 1));
    if (spec.modify) spec.modify(enemies, ch);
    for (const e of enemies) { e.hp = Math.max(1, e.hp); SF.enemies.rollIntent(e); }

    turn = 1;
    busy = false;
    cooldowns = {};
    awaitingTarget = null;
    ch.statuses = {};
    ch.shield = 0;
    ch.energy = ch.maxEnergy;
    blindsideReady = ch.cls === 'wraith';

    $('#combat-layer').hidden = false;
    $('#choices').classList.add('hidden');
    SF.audio.sfx.alarm();
    SF.fx.warpPulse();
    log('— ENGAGEMENT BEGINS —', 'crit');

    renderEnemies();
    renderAbilities();
    SF.ui.refresh();
  }

  async function finish(won) {
    busy = true;
    document.body.classList.remove('targeting');
    awaitingTarget = null;

    if (won) {
      SF.audio.sfx.win();
      SF.fx.banner('CLEAR', 'win');
      const gained = enemies.reduce((s, e) => s + e.xp, 0);
      log(`— ENGAGEMENT CLEAR — +${gained} XP —`, 'crit');
      const ups = SF.classes.grantXp(ch, gained);
      await wait(1500);
      for (const u of ups) { SF.audio.sfx.levelup(); SF.util.toast(u, 'good'); log(u, 'heal'); }
    } else {
      SF.audio.sfx.lose();
      SF.fx.banner('VITALS LOST', 'lose');
      await wait(1600);
    }

    $('#combat-layer').hidden = true;
    $('#choices').classList.remove('hidden');
    ch.shield = 0;
    ch.statuses = {};
    SF.ui.refresh();
    (won ? onWin : onLose)();
  }

  function bind() {
    $('#btn-focus').addEventListener('click', focus);
    document.addEventListener('keydown', (e) => {
      if ($('#combat-layer').hidden || busy) return;
      const list = SF.classes.CLASSES[ch.cls].abilities;
      const n = parseInt(e.key, 10);
      if (n >= 1 && n <= list.length) useAbility(list[n - 1]);
      if (e.key === ' ') { e.preventDefault(); focus(); }
      if (e.key === 'Escape' && awaitingTarget) {
        awaitingTarget = null;
        document.body.classList.remove('targeting');
      }
    });
  }

  const isActive = () => !$('#combat-layer').hidden;
  const isBusy = () => busy;

  SF.combat = { start, bind, useItem, ITEMS, isActive, isBusy, log };
})(window.SF);
