/* ============================================================
   The three doctrines of the Deep.

   Each class differs on three axes:
     1. its weapon (see fps/weapons.js — one per doctrine, all distinct)
     2. its field ability (grave ward / crystallize / devour step)
     3. a passive perk that changes how a firefight plays out

   The internal ids (bulwark / oracle / wraith) are the ones written into
   every saved dossier and used as keys in weapons.js, gear.js and
   dossier.js. They are deliberately left alone: only what the player
   reads changes.
   ============================================================ */
(function (SF) {
  'use strict';

  /* Attribute keys: MIGHT drives Carapace, SYNC drives Hollow, GUILE drives
     Shroud. They set starting vitals and grow on promotion. Keys are load-
     bearing for old saves; only the labels below are new. */

  const CLASSES = {

    /* ------------------------------------------------------------------ */
    bulwark: {
      id: 'bulwark',
      name: 'CARAPACE',
      role: 'GRAVE DOCTRINE',
      glyph: '✦',
      accent: '#e0556b',
      skill: 'FORCE',
      skillAttr: 'might',
      tagline: 'Buried in your armour and dug back up wearing it. Walks first, because nothing else will.',
      flavor: 'They put you in the ground in your plate and the Deep came up through the seams to ' +
              'find you. What grew back is not quite metal and not quite bone, and it does not ' +
              'come off. You have not been warm since, and you have not needed to be.',
      base: { hp: 124, energy: 10, might: 4, sync: 1, guile: 1 },
      growth: { hp: 14, energy: 1, attr: 'might' },
      perk: {
        name: 'GRAVE PLATING',
        desc: 'Grown armour absorbs 22% of all incoming damage. The slowest doctrine on its feet, ' +
              'and the only one that can stand in a corridor and trade with something immortal.'
      },
    },

    /* ------------------------------------------------------------------ */
    oracle: {
      id: 'oracle',
      name: 'HOLLOW',
      role: 'CIPHER DOCTRINE',
      glyph: '◈',
      accent: '#9d7bff',
      skill: 'CIPHER',
      skillAttr: 'sync',
      tagline: 'Emptied out so something older had a place to sit. Speaks the cold language fluently.',
      flavor: 'There is a shape missing from the middle of you, and the Deep keeps it filled. It ' +
              'taught you the syntax the Pale uses to hold matter together, which means it also ' +
              'taught you where to put the comma that stops it. Some of it argues. It loses.',
      base: { hp: 94, energy: 14, might: 1, sync: 5, guile: 2 },
      growth: { hp: 9, energy: 2, attr: 'sync' },
      perk: {
        name: 'THE COLD SYNTAX',
        desc: 'Null bolts pass through a target and into whatever stood behind it. Deepest magazine ' +
              'and the flattest recoil on the roster.'
      },
    },

    /* ------------------------------------------------------------------ */
    wraith: {
      id: 'wraith',
      name: 'SHROUD',
      role: 'UMBRAL DOCTRINE',
      glyph: '✵',
      accent: '#7de3a8',
      skill: 'STEALTH',
      skillAttr: 'guile',
      tagline: 'Never finished coming back. Arrives before the light does and leaves before it notices.',
      flavor: 'The Deep raised you the way you raise a held breath, and some of you stayed down ' +
              'there. What walks around up here is thin enough to step between one moment and the ' +
              'next. The Pale cannot bless what it cannot find.',
      base: { hp: 100, energy: 12, might: 2, sync: 2, guile: 5 },
      growth: { hp: 11, energy: 1, attr: 'guile' },
      perk: {
        name: 'BLINDSIDE',
        desc: 'Triple damage on a head shot, and a devour step that leaves the next round primed. ' +
              'Fragile in the open; lethal from an angle nobody blessed.'
      },
    }
  };

  const ATTRS = [
    { key: 'might', label: 'GRAVE'  },
    { key: 'sync',  label: 'CIPHER' },
    { key: 'guile', label: 'GUILE'  }
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
    const labelFor = (key) => (ATTRS.find((a) => a.key === key) || { label: key.toUpperCase() }).label;
    ch.xp += amount;
    while (ch.xp >= xpForLevel(ch.level)) {
      ch.xp -= xpForLevel(ch.level);
      ch.level++;
      const g = CLASSES[ch.cls].growth;
      ch.maxHp += g.hp;
      ch.hp = ch.maxHp;
      if (ch.level % 2 === 0) ch.maxEnergy += g.energy;
      ch[g.attr] += 1;
      notes.push(`RANK ${ch.level} — +${g.hp} vitals, +1 ${labelFor(g.attr)}`);
    }
    return notes;
  }

  const skillMod = (ch) => ch[CLASSES[ch.cls].skillAttr];

  SF.classes = { CLASSES, ATTRS, makeCharacter, grantXp, xpForLevel, skillMod };
})(window.SF);
