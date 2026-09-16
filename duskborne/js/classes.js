/* ============================================================
   Species, classes and Hollow disciplines — the character-creation
   data model. Nothing here is Bungie's Destiny 2: this is an original
   setting (the Hollow, the Unseen, Lumen Wardens, the Sunken Throne)
   built to the same shape as the brief — three playable peoples, three
   classes, a starting discipline per class with two more to unlock.
   ============================================================ */
(function (DB) {
  'use strict';

  const SPECIES = [
    {
      id: 'vekthal', name: "Vek'thal", tag: 'The Scrapkin',
      colour: 0xc9a24a,
      blurb: 'Four-armed scavenger-raiders who stripped a thousand derelict '
           + 'hulls for parts before the Hollow ever spoke to them. Quick '
           + 'hands, quicker retreats.',
      passive: 'Salvage Instinct', passiveBlurb: '+10% ability energy regeneration.',
      statMod: { abilityRegen: 1.10 }
    },
    {
      id: 'cindrit', name: 'Cindrit', tag: 'The Gloam',
      colour: 0x7a2e3a,
      blurb: 'A hive-mind of insectoid zealots bound to a single Choir-mother, '
           + 'each of them already rehearsing the sermon the swarm will give '
           + 'over your body.',
      passive: 'Swarm Resilience', passiveBlurb: '+10% maximum vitals.',
      statMod: { health: 1.10 }
    },
    {
      id: 'warped', name: 'the Warped', tag: 'Hollow-Touched',
      colour: 0x5a3a8a,
      blurb: 'Once Lumen, once Light-sworn — until the Hollow reached through '
           + 'them and did not let go. What looks back at you now is mostly '
           + 'still them. Mostly.',
      passive: 'Raw Communion', passiveBlurb: '+10% Hollow ability damage.',
      statMod: { abilityDamage: 1.10 }
    }
  ];

  const DISCIPLINES = {
    rime: {
      id: 'rime', name: 'Rime', tag: 'Control',
      colour: 0x8fd8ff,
      blurb: 'The Hollow at its coldest: freeze the moment before it happens.'
    },
    weave: {
      id: 'weave', name: 'Weave', tag: 'Severance',
      colour: 0x5ee6a8,
      blurb: 'The Hollow as a thread: tether, sever, cross ground that was never yours to cross.'
    },
    umbral: {
      id: 'umbral', name: 'Umbral', tag: 'Ruin',
      colour: 0x9a5ef0,
      blurb: 'The Hollow as appetite: fear first, then the dark finishes the job.'
    }
  };

  const CLASSES = [
    {
      id: 'bastion', name: 'Bastion', tag: 'Aegis of the Hollow',
      colour: 0x8fd8ff, icon: '◆',
      blurb: 'Front of every line, last to fall back. A Bastion carries the '
           + 'fight the way a wall carries a roof.',
      discipline: 'rime',
      weapon: {
        id: 'gravebreaker', name: 'GRAVEBREAKER', kind: 'Heavy Carbine',
        damage: 24, headshotMultiplier: 1.8, fireInterval: 0.16,
        magSize: 28, reserveAmmo: 112, reloadTime: 1.7, pellets: 1, spreadDegrees: 0.4
      },
      ability: {
        id: 'rime_ward', name: 'Rime Ward', discipline: 'rime',
        blurb: 'Slam the Hollow into the ground: an overshield for you, a '
             + 'freezing pulse for everything close enough to have deserved it.',
        cooldown: 22, radius: 11, damage: 65, slow: 0.55, slowTime: 3.2, shield: 70
      },
      passive: 'Bulwark Plating', passiveBlurb: 'Absorbs 20% of incoming damage.',
      damageResist: 0.20
    },
    {
      id: 'occultist', name: 'Occultist', tag: 'Voice of the Hollow',
      colour: 0x9a5ef0, icon: '✦',
      blurb: 'The Hollow speaks through an Occultist first, and it never '
           + 'stops mid-sentence. Neither should you.',
      discipline: 'umbral',
      weapon: {
        id: 'needler', name: 'HOLLOW NEEDLER', kind: 'Induction Carbine',
        damage: 15, headshotMultiplier: 2.0, fireInterval: 0.085,
        magSize: 36, reserveAmmo: 144, reloadTime: 1.5, pellets: 1, spreadDegrees: 0
      },
      ability: {
        id: 'umbral_grasp', name: 'Umbral Grasp', discipline: 'umbral',
        blurb: 'A cone of dread wide enough to swallow a fireteam’s worth '
             + 'of Lumen, each of them draining for three long seconds after.',
        cooldown: 18, range: 16, coneDegrees: 50, damage: 55, dot: 12, dotTime: 3
      },
      passive: 'Ghost In The Wire', passiveBlurb: 'Deepest magazine, flattest recoil.',
      damageResist: 0
    },
    {
      id: 'wraithblade', name: 'Wraithblade', tag: 'Edge of the Hollow',
      colour: 0x5ee6a8, icon: '▲',
      blurb: 'Gone before the shot lands, already behind you by the time '
           + 'you’ve noticed. A Wraithblade wins the fight by ending it early.',
      discipline: 'weave',
      weapon: {
        id: 'thornedge', name: 'THORNEDGE', kind: 'Suppressed SMG',
        damage: 13, headshotMultiplier: 3.0, fireInterval: 0.075,
        magSize: 30, reserveAmmo: 150, reloadTime: 1.3, pellets: 1, spreadDegrees: 0.2
      },
      ability: {
        id: 'weave_dash', name: 'Weave Dash', discipline: 'weave',
        blurb: 'Cross twelve metres of ground in a blink, cutting a line of '
             + 'Hollow damage through anything standing in it, untouchable '
             + 'the whole way across.',
        cooldown: 16, distance: 12, damage: 90, invulnTime: 0.5
      },
      passive: 'Blindside', passiveBlurb: 'Fragile in the open, lethal from an angle.',
      damageResist: -0.10
    }
  ];

  function species(id) { return SPECIES.find(function (s) { return s.id === id; }); }
  function klass(id) { return CLASSES.find(function (c) { return c.id === id; }); }
  function discipline(id) { return DISCIPLINES[id]; }

  DB.classes = { SPECIES: SPECIES, CLASSES: CLASSES, DISCIPLINES: DISCIPLINES,
                 species: species, klass: klass, discipline: discipline };
})(window.DB || (window.DB = {}));
