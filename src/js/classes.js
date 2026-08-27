/* ============================================================
   The three doctrines.

   Each class differs on three axes:
     1. its weapon (see fps/weapons.js — one per doctrine, all distinct)
     2. its field ability (barrier / EMP / phase step)
     3. a passive perk that changes how a firefight plays out
   ============================================================ */
(function (SF) {
  'use strict';

  /* Attribute keys: MIGHT drives Bulwark, SYNC drives Oracle, GUILE drives Wraith.
     They set starting vitals and grow on promotion. */

  const CLASSES = {

    /* ------------------------------------------------------------------ */
    bulwark: {
      id: 'bulwark',
      name: 'BULWARK',
      role: 'AEGIS DOCTRINE',
      glyph: '✦',
      accent: '#ffb454',
      skill: 'FORCE',
      skillAttr: 'might',
      tagline: 'Armour grown from the hull of a dead ship. Walks first, always.',
      flavor: 'They fed you a ship. Reclaimed plating, sintered into the long bones of your legs, ' +
              'and you have not felt cold since. Recovery Division sends a Bulwark through the door ' +
              'first because a Bulwark is the door.',
      base: { hp: 124, energy: 10, might: 4, sync: 1, guile: 1 },
      growth: { hp: 14, energy: 1, attr: 'might' },
      perk: {
        name: 'BULKHEAD PLATING',
        desc: 'Sealed armour absorbs 22% of all incoming damage. The slowest doctrine on its feet, ' +
              'and the only one that can stand in a corridor and trade.'
      },
    },

    /* ------------------------------------------------------------------ */
    oracle: {
      id: 'oracle',
      name: 'ORACLE',
      role: 'SIGNAL DOCTRINE',
      glyph: '◈',
      accent: '#5eeaff',
      skill: 'SYSTEMS',
      skillAttr: 'sync',
      tagline: 'Wetware spliced to a dead god\'s switchboard. Talks to machines that stopped listening.',
      flavor: 'The implant behind your ear is older than you and was salvaged from something that ' +
              'was never meant to be salvaged. You do not hack a ship so much as convince it that ' +
              'it has always been yours. Some of them argue.',
      base: { hp: 94, energy: 14, might: 1, sync: 5, guile: 2 },
      growth: { hp: 9, energy: 2, attr: 'sync' },
      perk: {
        name: 'GHOST IN THE WIRE',
        desc: 'Induction rounds pass through a target and into whatever stood behind it. Deepest ' +
              'magazine and the flattest recoil on the roster.'
      },
    },

    /* ------------------------------------------------------------------ */
    wraith: {
      id: 'wraith',
      name: 'WRAITH',
      role: 'UMBRAL DOCTRINE',
      glyph: '✵',
      accent: '#ff5ea8',
      skill: 'STEALTH',
      skillAttr: 'guile',
      tagline: 'Nine-tenths of a person and all of a knife. Arrives after the alarm should have.',
      flavor: 'Recovery Division does not list your name, your birth system, or the number of ' +
              'operations that took your reflexes apart and put them back faster. What they list ' +
              'is a response time, and it is the shortest number on the roster.',
      base: { hp: 100, energy: 12, might: 2, sync: 2, guile: 5 },
      growth: { hp: 11, energy: 1, attr: 'guile' },
      perk: {
        name: 'BLINDSIDE',
        desc: 'Triple damage on a head shot, and a phase step that leaves the next round primed. ' +
              'Fragile in the open; lethal from an angle nobody covered.'
      },
    }
  };

  const ATTRS = [
    { key: 'might', label: 'MIGHT' },
    { key: 'sync',  label: 'SYNC'  },
    { key: 'guile', label: 'GUILE' }
  ];

  /* Build a fresh character record for a save slot. */
  function makeCharacter(name, classId) {
    const cls = CLASSES[classId];
    return {
      name: name,
      cls: classId,
      level: 1,
      xp: 0,
      hp: cls.base.hp,
      maxHp: cls.base.hp,
      energy: cls.base.energy,
      maxEnergy: cls.base.energy,
      shield: 0,
      might: cls.base.might,
      sync: cls.base.sync,
      guile: cls.base.guile,
      missions: 0,
      campaign: { unlocked: 1, cleared: {}, bestTime: {} },
      created: Date.now(),
      playtime: 0
    };
  }

  const xpForLevel = (lvl) => 40 + (lvl - 1) * 55;

  /* Returns a list of level-up notices, or an empty array. */
  function grantXp(ch, amount) {
    const notes = [];
    ch.xp += amount;
    while (ch.xp >= xpForLevel(ch.level)) {
      ch.xp -= xpForLevel(ch.level);
      ch.level++;
      const g = CLASSES[ch.cls].growth;
      ch.maxHp += g.hp;
      ch.hp = ch.maxHp;
      if (ch.level % 2 === 0) ch.maxEnergy += g.energy;
      ch[g.attr] += 1;
      notes.push(`RANK ${ch.level} — +${g.hp} vitals, +1 ${g.attr.toUpperCase()}`);
    }
    return notes;
  }

  const skillMod = (ch) => ch[CLASSES[ch.cls].skillAttr];

  SF.classes = { CLASSES, ATTRS, makeCharacter, grantXp, xpForLevel, skillMod };
})(window.SF);
