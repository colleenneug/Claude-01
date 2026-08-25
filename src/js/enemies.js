/* ============================================================
   Bestiary. Every hostile telegraphs its next action as an
   "intent" so the player can read the board before committing.
   ============================================================ */
(function (SF) {
  'use strict';
  const { rand } = SF.util;

  const atk = (min, max) => rand(min, max);

  const BESTIARY = {

    husk_drone: {
      name: 'HUSK DRONE', sub: 'MAINTENANCE UNIT / REPURPOSED', glyph: '⬢',
      hp: 34, armor: 1, xp: 18,
      moves: [
        { id: 'cut', name: 'ARC CUTTER', tell: 'ARC CUTTER — 8-12', weight: 3,
          run: (c, self) => { const d = c.hurtPlayer(self, atk(8, 12)); c.log(`${self.name} arcs across you for ${d}.`, 'dmg'); } },
        { id: 'weld', name: 'SELF-WELD', tell: 'SELF-WELD — repair', weight: 1,
          run: (c, self) => { c.healEnemy(self, 9); c.log(`${self.name} welds its own seams shut. +9.`, 'heal'); } }
      ]
    },

    choir_thrall: {
      name: 'CHOIR THRALL', sub: 'COLONIST / SINGING', glyph: '♁',
      hp: 42, armor: 0, xp: 24,
      moves: [
        { id: 'grasp', name: 'GRASP', tell: 'GRASP — 10-14', weight: 3,
          run: (c, self) => { const d = c.hurtPlayer(self, atk(10, 14)); c.log(`Cold hands find you. ${d} damage.`, 'dmg'); } },
        { id: 'hymn', name: 'HYMN', tell: 'HYMN — drains CORE', weight: 2,
          run: (c, self) => { c.drainEnergy(3); c.log(`${self.name} sings. Your implants ache — 3 CORE lost.`, 'dmg'); } }
      ]
    },

    warden_frame: {
      name: 'WARDEN FRAME', sub: 'SECURITY CONSTRUCT / MK-IV', glyph: '⛨',
      hp: 78, armor: 4, xp: 46,
      moves: [
        { id: 'volley', name: 'SUPPRESSION VOLLEY', tell: 'VOLLEY — 14-19', weight: 3,
          run: (c, self) => { const d = c.hurtPlayer(self, atk(14, 19)); c.log(`Suppression volley. ${d} damage.`, 'dmg'); } },
        { id: 'lock', name: 'TARGET LOCK', tell: 'TARGET LOCK — marks you', weight: 2,
          run: (c, self) => { c.statusPlayer('EXPOSED', 2); c.log('Target lock acquired. You are EXPOSED.', 'dmg'); } },
        { id: 'plate', name: 'REACTIVE PLATING', tell: 'PLATING — +shield', weight: 1,
          run: (c, self) => { c.shieldEnemy(self, 16); c.log(`${self.name} deploys reactive plating. +16 shield.`, 'buff'); } }
      ]
    },

    vent_stalker: {
      name: 'VENT STALKER', sub: 'UNCLASSIFIED / FAST', glyph: '⩕',
      hp: 30, armor: 0, xp: 22,
      moves: [
        { id: 'pounce', name: 'POUNCE', tell: 'POUNCE — 12-16', weight: 3,
          run: (c, self) => { const d = c.hurtPlayer(self, atk(12, 16)); c.log(`It drops from the ducting for ${d}.`, 'dmg'); } },
        { id: 'shriek', name: 'SHRIEK', tell: 'SHRIEK — calls kin', weight: 1,
          run: (c, self) => { c.shieldEnemy(self, 8); c.log(`${self.name} shrieks. Something answers.`, 'dmg'); } }
      ]
    },

    cradle_avatar: {
      name: 'CRADLE AVATAR', sub: 'SHIPMIND / PARTIAL MANIFEST', glyph: '◉',
      hp: 96, armor: 3, xp: 70,
      moves: [
        { id: 'purge', name: 'PURGE CYCLE', tell: 'PURGE — 16-22', weight: 3,
          run: (c, self) => { const d = c.hurtPlayer(self, atk(16, 22)); c.log(`Purge cycle floods the deck. ${d} damage.`, 'dmg'); } },
        { id: 'rewrite', name: 'REWRITE', tell: 'REWRITE — steals shield', weight: 2,
          run: (c, self) => {
            const s = c.stripPlayerShield();
            c.shieldEnemy(self, s + 10);
            c.log(`CRADLE rewrites your barrier into its own. +${s + 10}.`, 'dmg');
          } },
        { id: 'lull', name: 'LULLABY', tell: 'LULLABY — 3 turns of bleed', weight: 2,
          run: (c, self) => { c.statusPlayer('BLEED', 3, 5); c.log('It sings the colony\'s lullaby. Something in you starts to come apart.', 'dmg'); } }
      ]
    },

    choir_conductor: {
      name: 'THE CONDUCTOR', sub: 'FIRST VOICE / TWO HUNDRED THOUSAND STRONG', glyph: '✧',
      hp: 150, armor: 5, xp: 140, boss: true,
      moves: [
        { id: 'crescendo', name: 'CRESCENDO', tell: 'CRESCENDO — 20-27', weight: 3, lethal: true,
          run: (c, self) => { const d = c.hurtPlayer(self, atk(20, 27)); c.log(`The chorus peaks. ${d} damage.`, 'dmg'); } },
        { id: 'summon', name: 'CALL THE CHOIR', tell: 'CALL — reinforcement', weight: 2,
          run: (c, self) => { c.summon('choir_thrall'); c.log('It calls, and the corridor answers.', 'dmg'); } },
        { id: 'harmonize', name: 'HARMONIZE', tell: 'HARMONIZE — heals + shields', weight: 2,
          run: (c, self) => { c.healEnemy(self, 18); c.shieldEnemy(self, 14); c.log('The Conductor harmonizes with itself. It is more whole than it was.', 'heal'); } },
        { id: 'silence', name: 'IMPOSED SILENCE', tell: 'SILENCE — locks an ability', weight: 1,
          run: (c, self) => { c.statusPlayer('SILENCED', 2); c.log('Silence falls over your ability suite.', 'dmg'); } }
      ]
    }
  };

  /* Instantiate a live combatant, optionally scaled to player rank. */
  function spawn(id, scale) {
    const t = BESTIARY[id];
    const s = scale || 1;
    return {
      uid: id + '_' + Math.random().toString(36).slice(2, 7),
      type: id,
      name: t.name,
      sub: t.sub,
      glyph: t.glyph,
      boss: !!t.boss,
      hp: Math.round(t.hp * s),
      maxHp: Math.round(t.hp * s),
      armor: t.armor,
      shield: 0,
      xp: Math.round(t.xp * s),
      statuses: {},
      moves: t.moves,
      intent: null,
      dead: false
    };
  }

  /* Weighted pick of the next telegraphed action. */
  function rollIntent(enemy) {
    const pool = [];
    for (const m of enemy.moves) for (let i = 0; i < (m.weight || 1); i++) pool.push(m);
    enemy.intent = pool[Math.floor(Math.random() * pool.length)];
    return enemy.intent;
  }

  SF.enemies = { BESTIARY, spawn, rollIntent };
})(window.SF);
