/* ============================================================
   Gear: rarity, drops, armour, appearance and power.

   Every mission you clear pays out salvage. Items roll on a five-tier
   rarity ladder; the tier sets a flat stat multiplier and how many
   affixes the item carries, and the odds shift toward the top of the
   ladder the further down the ark you are fighting.

   Power is the single number that summarises an operative: rank plus
   everything equipped. It is what "getting stronger" means here.
   ============================================================ */
(function (SF) {
  'use strict';

  const RARITY = [
    { id: 'common',   name: 'COMMON',   colour: '#9fb0c0', mult: 1.00, affixes: 0, weight: 100 },
    { id: 'uncommon', name: 'UNCOMMON', colour: '#7dff9b', mult: 1.10, affixes: 1, weight: 52 },
    { id: 'rare',     name: 'RARE',     colour: '#5eeaff', mult: 1.22, affixes: 2, weight: 24 },
    { id: 'epic',     name: 'EPIC',     colour: '#b98cff', mult: 1.40, affixes: 3, weight: 8 },
    { id: 'exotic',   name: 'EXOTIC',   colour: '#ffb454', mult: 1.65, affixes: 4, weight: 2 }
  ];
  const rarityOf = (id) => RARITY.find((r) => r.id === id) || RARITY[0];
  const rarityRank = (id) => RARITY.findIndex((r) => r.id === id);

  /* Later missions bias the roll upward: by the Conductor, common is rare
     and exotic is a real possibility. */
  function rollRarity(missionN) {
    const tilt = Math.max(0, (missionN - 1) / 15);          // 0 at mission 1, 1 at 16
    const weights = RARITY.map((r, i) => r.weight * Math.pow(2.4, i * tilt));
    let total = weights.reduce((a, w) => a + w, 0);
    let pick = Math.random() * total;
    for (let i = 0; i < RARITY.length; i++) {
      pick -= weights[i];
      if (pick <= 0) return RARITY[i].id;
    }
    return 'common';
  }

  /* ---------- naming ---------- */
  const PREFIX = ['SALVAGED', 'MARK-VII', 'COLONIAL', 'ARK-PATTERN', 'RECLAIMED', 'DIVISION',
                  'HOLLOWED', 'QUIET', 'LONG-CROSSING', 'YEAR-SIX'];
  const SUFFIX = ['OF THE GARDEN', 'OF DECK ZERO', 'OF THE LONG SPINE', 'OF THE FALSE SKY',
                  'OF THE CHOIR', 'OF WARD SIX', 'OF THE THRESHOLD'];
  const EXOTIC_NAMES = ['LULLABY', 'THE GREEN DOOR', 'NINE HOURS', 'ADA', 'ONE NOTE', 'REST'];

  /* ---------- affixes ---------- */
  const WEAPON_AFFIXES = [
    { id: 'dmg',    label: '+{v}% damage',        stat: 'damage', min: 4,  max: 14 },
    { id: 'mag',    label: '+{v}% magazine',      stat: 'mag',    min: 8,  max: 26 },
    { id: 'rpm',    label: '+{v}% rate of fire',  stat: 'rpm',    min: 5,  max: 16 },
    { id: 'reload', label: '-{v}% reload time',   stat: 'reload', min: 8,  max: 24 },
    { id: 'spread', label: '-{v}% spread',        stat: 'spread', min: 10, max: 30 }
  ];
  const ARMOUR_AFFIXES = [
    { id: 'hp',     label: '+{v} vitals',         stat: 'hp',     min: 6,  max: 22 },
    { id: 'resist', label: '+{v}% resistance',    stat: 'resist', min: 2,  max: 7 },
    { id: 'regen',  label: '+{v}% vital regen',   stat: 'regen',  min: 8,  max: 25 }
  ];

  /* ---------- runner parts ---------- */
  /* Parts are the one thing in the game you spend rather than equip. They
     come out of crates and events, they stack in a pool, and you bolt them
     onto a runner at the bench. A part is worth the same on any frame, so
     the choice is which frame you commit them to — and a frame's rarity is
     what caps how many it will take of each. */
  const PARTS = [
    { id: 'thrust', name: 'THRUST COIL', stat: 'speed', per: 6,
      line: '+{v}% top speed',    desc: 'Wound coil off a cutter drive. Adds top end.' },
    { id: 'brace',  name: 'FRAME BRACE', stat: 'accel', per: 9,
      line: '+{v}% acceleration', desc: 'Stiffens the spine so the drive stops flexing away from it.' },
    { id: 'grip',   name: 'GRIP PLATE',  stat: 'turn',  per: 8,
      line: '+{v}% handling',     desc: 'A field plate that bites the ground through the turn.' },
    { id: 'cell',   name: 'BOOST CELL',  stat: 'boost', per: 7,
      line: '+{v}% boost',        desc: 'Holds a bigger charge, so the burn runs longer and harder.' }
  ];
  const partOf = (id) => PARTS.find((p) => p.id === id) || PARTS[0];

  /* How many of each part a frame will take, by rarity. A common runner is
     worth upgrading; an exotic one is worth hunting parts for. */
  const PART_CAP = { common: 2, uncommon: 3, rare: 4, epic: 5, exotic: 7 };
  const partCap = (item) => (item ? PART_CAP[item.rarity] || 2 : 0);

  const SLOTS = [
    { id: 'weapon', name: 'WEAPON' },
    { id: 'head',   name: 'HELM' },
    { id: 'chest',  name: 'PLATE' },
    { id: 'legs',   name: 'GREAVES' },
    { id: 'runner', name: 'RUNNER' }
  ];

  /* Runners: the salvaged transit frames Division issues for open ground.
     Two families that handle nothing alike. */
  const RUNNERS = {
    courser: {
      id: 'courser', name: 'COURSER', kind: 'SPEED FRAME',
      speed: 4.4, accel: 5.5, turn: 1.5, boost: 1.5,
      desc: 'A single-rail frame built for the straight line. Enormous top end, ' +
            'turns like an argument.'
    },
    skiff: {
      id: 'skiff', name: 'SKIFF', kind: 'PLATE FRAME',
      speed: 3.3, accel: 11, turn: 2.6, boost: 1.25,
      desc: 'A flat plate that hovers a hand off the ground. Slower flat out, but ' +
            'it goes where you point it and it stops when you ask.'
    }
  };
  const RUNNER_AFFIXES = [
    { id: 'top',   label: '+{v}% top speed',    stat: 'speed', min: 4,  max: 15 },
    { id: 'acc',   label: '+{v}% acceleration', stat: 'accel', min: 8,  max: 26 },
    { id: 'grip',  label: '+{v}% handling',     stat: 'turn',  min: 8,  max: 24 },
    { id: 'boost', label: '+{v}% boost',        stat: 'boost', min: 6,  max: 20 }
  ];

  let nextId = 1;

  function rollAffixes(pool, count) {
    const chosen = [];
    const bag = pool.slice();
    for (let i = 0; i < count && bag.length; i++) {
      const a = bag.splice(Math.floor(Math.random() * bag.length), 1)[0];
      const v = a.min + Math.floor(Math.random() * (a.max - a.min + 1));
      chosen.push({ id: a.id, stat: a.stat, value: v, label: a.label.replace('{v}', v) });
    }
    return chosen;
  }

  function powerOf(item) {
    if (!item) return 0;
    const base = 12 + rarityRank(item.rarity) * 9;
    const affix = item.affixes.reduce((a, x) => a + x.value * 0.6, 0);
    // parts bolted on read as power too, so the bench shows its work
    const fitted = item.upgrades
      ? Object.keys(item.upgrades).reduce((a, k) => a + item.upgrades[k], 0) * 5
      : 0;
    return Math.round((base + affix) * (1 + (item.tier || 1) * 0.06)) + fitted;
  }

  /* ---------- the bench ---------- */
  /* Older runners predate the upgrade table; give them an empty one rather
     than letting the bench read undefined. */
  function ensureUpgrades(item) {
    if (item && item.slot === 'runner' && !item.upgrades) {
      item.upgrades = { thrust: 0, brace: 0, grip: 0, cell: 0 };
    }
    return item;
  }

  function fittedCount(item, partId) {
    ensureUpgrades(item);
    return item && item.upgrades ? (item.upgrades[partId] || 0) : 0;
  }

  /* Why a part cannot go on, in words the bench can print. Null means it can. */
  function partBlocker(ch, item, partId) {
    if (!item) return 'NO RUNNER EQUIPPED';
    if (!(ch.parts && ch.parts[partId] > 0)) return 'NONE IN STOCK';
    if (fittedCount(item, partId) >= partCap(item)) return 'FRAME IS FULL';
    return null;
  }

  function installPart(ch, item, partId) {
    if (partBlocker(ch, item, partId)) return false;
    ensureUpgrades(item);
    item.upgrades[partId]++;
    ch.parts[partId]--;
    item.power = powerOf(item);
    return true;
  }

  /* Strip a frame back to nothing and get every part back. Nothing is lost,
     so moving your stock onto a better frame costs only the trip. */
  function stripParts(ch, item) {
    ensureUpgrades(item);
    let n = 0;
    for (const p of PARTS) {
      n += item.upgrades[p.id];
      ch.parts[p.id] += item.upgrades[p.id];
      item.upgrades[p.id] = 0;
    }
    item.power = powerOf(item);
    return n;
  }

  /* Crates and events pay out parts as well as gear. */
  function rollParts(count) {
    const out = { thrust: 0, brace: 0, grip: 0, cell: 0 };
    for (let i = 0; i < count; i++) out[PARTS[Math.floor(Math.random() * PARTS.length)].id]++;
    return out;
  }

  function grantParts(ch, bundle) {
    ensure(ch);
    for (const k of Object.keys(bundle)) ch.parts[k] = (ch.parts[k] || 0) + bundle[k];
    return ch;
  }

  /* ---------- item construction ---------- */

  function makeWeapon(classId, rarityId, tier) {
    const spec = SF.weapons.WEAPONS[classId];
    const r = rarityOf(rarityId);
    const affixes = rollAffixes(WEAPON_AFFIXES, r.affixes);
    const item = {
      uid: 'w' + (nextId++), kind: 'weapon', slot: 'weapon', cls: classId,
      rarity: rarityId, tier: tier || 1, base: spec.name, affixes: affixes,
      name: rarityId === 'exotic'
        ? '"' + EXOTIC_NAMES[Math.floor(Math.random() * EXOTIC_NAMES.length)] + '" ' + spec.name
        : (rarityId === 'common' ? 'ISSUED ' + spec.name
           : PREFIX[Math.floor(Math.random() * PREFIX.length)] + ' ' + spec.name +
             (rarityId === 'epic' ? ' ' + SUFFIX[Math.floor(Math.random() * SUFFIX.length)] : ''))
    };
    item.power = powerOf(item);
    return item;
  }

  function makeArmour(slot, rarityId, tier) {
    const r = rarityOf(rarityId);
    const item = {
      uid: 'a' + (nextId++), kind: 'armour', slot: slot,
      rarity: rarityId, tier: tier || 1,
      affixes: rollAffixes(ARMOUR_AFFIXES, Math.max(1, r.affixes)),
      name: (rarityId === 'exotic'
        ? '"' + EXOTIC_NAMES[Math.floor(Math.random() * EXOTIC_NAMES.length)] + '" '
        : PREFIX[Math.floor(Math.random() * PREFIX.length)] + ' ') +
        SLOTS.find((s) => s.id === slot).name
    };
    item.power = powerOf(item);
    return item;
  }

  function makeRunner(familyId, rarityId, tier) {
    const fam = RUNNERS[familyId] || RUNNERS.courser;
    const r = rarityOf(rarityId);
    const item = {
      uid: 'r' + (nextId++), kind: 'runner', slot: 'runner', family: fam.id,
      rarity: rarityId, tier: tier || 1,
      upgrades: { thrust: 0, brace: 0, grip: 0, cell: 0 },
      affixes: rollAffixes(RUNNER_AFFIXES, Math.max(1, r.affixes)),
      name: (rarityId === 'exotic'
        ? '"' + EXOTIC_NAMES[Math.floor(Math.random() * EXOTIC_NAMES.length)] + '" '
        : rarityId === 'common' ? 'ISSUED '
        : PREFIX[Math.floor(Math.random() * PREFIX.length)] + ' ') + fam.name
    };
    item.power = powerOf(item);
    return item;
  }

  /* The numbers the ride actually uses, after rarity and affixes. */
  function runnerStats(character) {
    const item = character.equipped && character.equipped.runner;
    if (!item) return null;
    const fam = RUNNERS[item.family] || RUNNERS.courser;
    const r = rarityOf(item.rarity);
    const out = {
      family: fam.id, name: item.name, kind: fam.kind,
      speed: fam.speed * (1 + (r.mult - 1) * 0.6),
      accel: fam.accel * (1 + (r.mult - 1) * 0.6),
      turn: fam.turn, boost: fam.boost
    };
    for (const a of item.affixes) {
      if (a.stat === 'speed') out.speed *= 1 + a.value / 100;
      if (a.stat === 'accel') out.accel *= 1 + a.value / 100;
      if (a.stat === 'turn')  out.turn  *= 1 + a.value / 100;
      if (a.stat === 'boost') out.boost *= 1 + a.value / 100;
    }
    // and then whatever is bolted to it
    ensureUpgrades(item);
    for (const p of PARTS) {
      const n = item.upgrades ? (item.upgrades[p.id] || 0) : 0;
      if (n) out[p.stat] *= 1 + (n * p.per) / 100;
    }
    out.parts = item.upgrades ? Object.assign({}, item.upgrades) : null;
    out.cap = partCap(item);
    return out;
  }

  /* One or two pieces of salvage per cleared mission, better the deeper
     you are, and the boss always pays out. */
  function rollDrops(character, missionN, isBoss) {
    const drops = [];
    const count = isBoss ? 3 : (Math.random() < 0.45 ? 2 : 1);
    for (let i = 0; i < count; i++) {
      let rarity = rollRarity(missionN);
      if (isBoss && rarityRank(rarity) < 2) rarity = 'rare';     // no junk from the Conductor
      const roll = Math.random();
      if (roll < 0.34) drops.push(makeWeapon(character.cls, rarity, missionN));
      else if (roll < 0.46) drops.push(makeRunner(
        Math.random() < 0.5 ? 'courser' : 'skiff', rarity, missionN));
      else drops.push(makeArmour(['head', 'chest', 'legs'][Math.floor(Math.random() * 3)],
                                 rarity, missionN));
    }
    return drops;
  }

  /* ---------- equipped stats ---------- */

  /* Multipliers the weapon module applies over its base spec. */
  function weaponMods(character) {
    const item = character.equipped && character.equipped.weapon;
    const mods = { damage: 1, mag: 1, rpm: 1, reload: 1, spread: 1 };
    if (!item) return mods;
    const r = rarityOf(item.rarity);
    mods.damage = r.mult;
    for (const a of item.affixes) {
      if (a.stat === 'damage') mods.damage *= 1 + a.value / 100;
      if (a.stat === 'mag')    mods.mag    *= 1 + a.value / 100;
      if (a.stat === 'rpm')    mods.rpm    *= 1 + a.value / 100;
      if (a.stat === 'reload') mods.reload *= 1 - a.value / 100;
      if (a.stat === 'spread') mods.spread *= 1 - a.value / 100;
    }
    return mods;
  }

  /* Flat bonuses from the three armour slots. */
  function armourStats(character) {
    const out = { hp: 0, resist: 0, regen: 0 };
    const eq = character.equipped || {};
    for (const slot of ['head', 'chest', 'legs']) {
      const item = eq[slot];
      if (!item) continue;
      const r = rarityOf(item.rarity);
      out.hp += Math.round(8 * r.mult);
      out.resist += 1.5 * r.mult;
      for (const a of item.affixes) {
        if (a.stat === 'hp') out.hp += a.value;
        if (a.stat === 'resist') out.resist += a.value;
        if (a.stat === 'regen') out.regen += a.value;
      }
    }
    out.resist = Math.min(55, out.resist);          // never immune
    return out;
  }

  const powerOfCharacter = (ch) => {
    const eq = ch.equipped || {};
    // a runner is transport, not firepower: it does not inflate your power
    return Math.round(ch.level * 8 +
      ['weapon', 'head', 'chest', 'legs'].reduce((a, s) => a + powerOf(eq[s]), 0));
  };

  /* ---------- appearance ---------- */

  const SKINS = ['#f0d0b8', '#e0b394', '#c89272', '#a06a48', '#7a4a30', '#5a3520', '#3d2418'];
  const HAIR_COLOURS = ['#141210', '#3a2a1e', '#6b4a2a', '#a8722f', '#c9a227', '#8a8a8a',
                        '#e8e8e8', '#5eeaff', '#ff5ea8', '#7dff9b'];
  const HAIR_STYLES = [
    { id: 'shaved',  name: 'SHAVED' },
    { id: 'crop',    name: 'CROP' },
    { id: 'braids',  name: 'BRAIDS' },
    { id: 'tail',    name: 'TIED BACK' },
    { id: 'locs',    name: 'LOCS' },
    { id: 'mohawk',  name: 'CREST' },
    { id: 'long',    name: 'LOOSE' }
  ];

  const defaultLook = () => ({
    skin: Math.floor(Math.random() * SKINS.length),
    hair: Math.floor(Math.random() * HAIR_STYLES.length),
    hairColour: Math.floor(Math.random() * 7)
  });

  /* Bring an older save up to date without losing it. */
  function ensure(ch) {
    if (!ch.look) ch.look = defaultLook();
    if (!ch.inventory) ch.inventory = [];
    if (!ch.equipped) ch.equipped = { weapon: null, head: null, chest: null, legs: null };
    if (!ch.equipped.weapon) {
      const starter = makeWeapon(ch.cls, 'common', 1);
      ch.inventory.push(starter);
      ch.equipped.weapon = starter;
    }
    if (!ch.equipped.runner) {                 // everyone gets something to ride
      const ride = makeRunner('courser', 'common', 1);
      ch.inventory.push(ride);
      ch.equipped.runner = ride;
    }
    if (!ch.parts) ch.parts = { thrust: 0, brace: 0, grip: 0, cell: 0 };
    for (const p of PARTS) if (typeof ch.parts[p.id] !== 'number') ch.parts[p.id] = 0;

    /* A save round-trips through JSON, which turns the equipped item and its
       inventory entry into two objects that merely look alike. That was
       harmless while items were read-only, but a runner carries fitted parts
       now: re-equipping it from the list would hand back the stale copy and
       the parts would appear to vanish. Re-link them by uid on load, so
       there is exactly one object per item again. */
    for (const slot of SLOTS) {
      const eq = ch.equipped[slot.id];
      if (!eq) continue;
      // the equipped copy is the one the bench mutates, so it is the truth
      const i = ch.inventory.findIndex((it) => it.uid === eq.uid);
      if (i >= 0) ch.inventory[i] = eq;
      else ch.inventory.push(eq);
    }
    for (const it of ch.inventory) ensureUpgrades(it);
    ensureUpgrades(ch.equipped.runner);
    return ch;
  }

  /* ---------- the appraiser ----------
     Sixty slots fill up fast, and nothing was ever done with the twelfth
     common helmet you picked up. Breaking salvage down turns it into runner
     parts, which is the one thing you always want more of. Rarer gear is
     worth more, and anything equipped is never touched. */
  const BREAK_YIELD = { common: 1, uncommon: 1, rare: 2, epic: 3, exotic: 4 };

  function isEquipped(ch, item) {
    return SLOTS.some((s) => ch.equipped[s.id] && ch.equipped[s.id].uid === item.uid);
  }

  /* What the appraiser would take, and what it would pay. `floor` is the
     highest rarity it is allowed to break — nothing above it is touched. */
  function breakdownPreview(ch, floor) {
    ensure(ch);
    const limit = rarityRank(floor);
    const take = ch.inventory.filter((it) => !isEquipped(ch, it) && rarityRank(it.rarity) <= limit);
    const pay = take.reduce((a, it) => a + (BREAK_YIELD[it.rarity] || 1), 0);
    return { count: take.length, parts: pay };
  }

  function breakdown(ch, floor) {
    const limit = rarityRank(floor);
    const take = ch.inventory.filter((it) => !isEquipped(ch, it) && rarityRank(it.rarity) <= limit);
    if (!take.length) return { count: 0, parts: {} };
    const gained = { thrust: 0, brace: 0, grip: 0, cell: 0 };
    for (const it of take) {
      const n = BREAK_YIELD[it.rarity] || 1;
      for (let i = 0; i < n; i++) gained[PARTS[Math.floor(Math.random() * PARTS.length)].id]++;
    }
    const gone = new Set(take.map((it) => it.uid));
    ch.inventory = ch.inventory.filter((it) => !gone.has(it.uid));
    grantParts(ch, gained);
    return { count: take.length, parts: gained,
             total: Object.keys(gained).reduce((a, k) => a + gained[k], 0) };
  }

  function grant(ch, items) {
    ensure(ch);
    for (const it of items) {
      ch.inventory.push(it);
      // auto-equip a straight upgrade so the reward is felt immediately
      const cur = ch.equipped[it.slot];
      if (!cur || it.power > cur.power) ch.equipped[it.slot] = it;
    }
    if (ch.inventory.length > 60) ch.inventory = ch.inventory.slice(-60);
    return ch;
  }

  SF.gear = {
    RARITY, SLOTS, SKINS, HAIR_COLOURS, HAIR_STYLES, RUNNERS, PARTS,
    rarityOf, rarityRank, rollRarity, rollDrops, makeWeapon, makeArmour,
    makeRunner, runnerStats, partOf, partCap, fittedCount, partBlocker,
    installPart, stripParts, rollParts, grantParts,
    breakdown, breakdownPreview, isEquipped,
    weaponMods, armourStats, powerOf, powerOfCharacter, ensure, grant, defaultLook
  };
})(window.SF);
