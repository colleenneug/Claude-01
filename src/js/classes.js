/* ============================================================
   The three doctrines.

   Each class differs on three axes:
     1. combat abilities (four each, all unique)
     2. a passive perk that changes the rules of a fight
     3. a narrative skill that unlocks class-gated story options
   ============================================================ */
(function (SF) {
  'use strict';

  /* Attribute keys: MIGHT drives Bulwark, SYNC drives Oracle, GUILE drives Wraith. */

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
        desc: 'All incoming damage reduced by 3. Story: FORCE options open doors nobody else can.'
      },
      abilities: [
        {
          id: 'slam', name: 'KINETIC SLAM', cost: 4, cd: 0, target: 'enemy',
          desc: 'Heavy strike. 30% chance to STAGGER — the target loses its next action.',
          run(c) {
            const dealt = c.damage(c.target, 12 + c.p.might * 2);
            c.log(`Kinetic Slam connects for ${dealt}.`, 'dmg');
            if (c.chance(0.3)) {
              c.status(c.target, 'STAGGER', 1);
              c.log(`${c.target.name} is staggered — its next action is lost.`, 'buff');
            }
          }
        },
        {
          id: 'aegis', name: 'AEGIS WALL', cost: 3, cd: 1, target: 'self',
          desc: 'Raise a kinetic barrier. Also grants FORTIFY: halves the next hit that breaks through.',
          run(c) {
            const amt = 18 + c.p.might * 3;
            c.addShield(c.p, amt);
            c.status(c.p, 'FORTIFY', 2);
            c.log(`Aegis Wall holds. +${amt} shield, FORTIFY active.`, 'buff');
          }
        },
        {
          id: 'riot', name: 'RIOT PULSE', cost: 5, cd: 2, target: 'all',
          desc: 'Shockwave hits every hostile and SUNDERS armour for 3 turns.',
          run(c) {
            for (const e of c.enemies) {
              const dealt = c.damage(e, 8 + c.p.might);
              c.status(e, 'SUNDER', 3);
              c.log(`Riot Pulse rakes ${e.name} for ${dealt}. Armour sundered.`, 'dmg');
            }
          }
        },
        {
          id: 'laststand', name: 'LAST STAND', cost: 8, cd: 5, target: 'self', ultimate: true,
          desc: 'ULTIMATE. Heal 25% of maximum vitals, gain 30 shield, and deal +50% damage for 2 turns.',
          run(c) {
            const h = Math.round(c.p.maxHp * 0.25);
            c.heal(h);
            c.addShield(c.p, 30);
            c.status(c.p, 'RESOLVE', 2);
            c.log(`LAST STAND. Vitals +${h}, shield +30, damage amplified.`, 'crit');
          }
        }
      ]
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
        desc: '+2 CORE regenerated every turn. Story: SYSTEMS options rewrite locks, doors and truths.'
      },
      abilities: [
        {
          id: 'spike', name: 'OVERLOAD SPIKE', cost: 3, cd: 0, target: 'enemy',
          desc: 'Voltage injected past the plating. Ignores shields entirely.',
          run(c) {
            const dealt = c.damage(c.target, 9 + c.p.sync * 2, { pierce: true });
            c.log(`Overload Spike burns through for ${dealt} — shielding irrelevant.`, 'dmg');
          }
        },
        {
          id: 'weave', name: 'NANITE WEAVE', cost: 4, cd: 1, target: 'self',
          desc: 'Repair vitals and scrub one hostile status effect.',
          run(c) {
            const amt = 16 + c.p.sync * 3;
            c.heal(amt);
            const cleaned = c.cleanse(c.p);
            c.log(`Nanite Weave restores ${amt}${cleaned ? ` and scrubs ${cleaned}` : ''}.`, 'heal');
          }
        },
        {
          id: 'breach', name: 'SYSTEMS BREACH', cost: 5, cd: 2, target: 'enemy',
          desc: 'GLITCH the target for 2 turns (its damage halved) and siphon 4 CORE.',
          run(c) {
            c.status(c.target, 'GLITCH', 2);
            c.gainEnergy(4);
            c.log(`${c.target.name} glitches — output halved. Siphoned 4 CORE.`, 'buff');
          }
        },
        {
          id: 'cascade', name: 'RECURSIVE CASCADE', cost: 9, cd: 5, target: 'all', ultimate: true,
          desc: 'ULTIMATE. Detonate every hostile system at once, then OVERCLOCK yourself for 3 turns.',
          run(c) {
            for (const e of c.enemies) {
              const dealt = c.damage(e, 14 + c.p.sync * 3, { pierce: true });
              c.log(`Cascade tears ${e.name} for ${dealt}.`, 'dmg');
            }
            c.status(c.p, 'OVERCLOCK', 3);
            c.log('RECURSIVE CASCADE. Overclock engaged — +4 CORE per turn.', 'crit');
          }
        }
      ]
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
        desc: 'Your first strike in any engagement is a guaranteed critical. Story: STEALTH options ' +
              'let you take the route nobody is watching.'
      },
      abilities: [
        {
          id: 'lunge', name: 'VIBRO LUNGE', cost: 3, cd: 0, target: 'enemy',
          desc: 'Fast monofilament strike. 25% critical for double damage.',
          run(c) {
            const crit = c.chance(0.25);
            const dealt = c.damage(c.target, 10 + c.p.guile * 2, { crit });
            c.log(crit ? `CRITICAL — Vibro Lunge opens ${c.target.name} for ${dealt}.`
                       : `Vibro Lunge cuts for ${dealt}.`, crit ? 'crit' : 'dmg');
          }
        },
        {
          id: 'phase', name: 'PHASE STEP', cost: 2, cd: 1, target: 'self',
          desc: 'Slip the next attack entirely (EVASION) and leave PRIMED — your next strike auto-crits.',
          run(c) {
            c.status(c.p, 'EVASION', 2);
            c.status(c.p, 'PRIMED', 3);
            c.log('Phase Step. You are somewhere the room has not caught up to yet.', 'buff');
          }
        },
        {
          id: 'toxin', name: 'NEURO TOXIN', cost: 4, cd: 2, target: 'enemy',
          desc: 'Stacking poison: 6 damage per turn for 3 turns, ignores armour and shields.',
          run(c) {
            c.status(c.target, 'TOXIN', 3, 6);
            c.log(`Neuro Toxin seeded in ${c.target.name}. It will not stop working.`, 'buff');
          }
        },
        {
          id: 'erase', name: 'MARK & ERASE', cost: 8, cd: 5, target: 'enemy', ultimate: true,
          desc: 'ULTIMATE. Massive strike; doubled against wounded targets, lethal below 20% vitals.',
          run(c) {
            const t = c.target;
            const ratio = t.hp / t.maxHp;
            if (ratio <= 0.2) {
              const dealt = c.damage(t, t.hp + 999, { crit: true, pierce: true });
              c.log(`ERASED. ${t.name} is unmade (${dealt}).`, 'crit');
              return;
            }
            const base = 10 + c.p.guile * 3;
            const dealt = c.damage(t, ratio < 0.5 ? base * 2 : base, { crit: true });
            c.log(`MARK & ERASE lands for ${dealt}.`, 'crit');
          }
        }
      ]
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
      statuses: {},
      items: { medgel: 2, pulse: 1, cell: 1 },
      node: 'act1_approach',
      flags: {},
      chapter: 1,
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
