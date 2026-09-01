/* ============================================================
   Things that are only for looking at.

   Chits buy two kinds. A LIVERY repaints the suit and its trim, and
   shows everywhere your operative is drawn — third person, the
   dossier, and the rider on a frame. A FRAME PLATE repaints the runner
   itself, which otherwise takes its colour from its rarity.

   Nothing here changes a number. That is the point: a soft currency
   needs somewhere to go that does not make you stronger, or it is just
   a slower way of buying power.

   Two of each are not for sale. You get those for doing something.
   ============================================================ */
(function (SF) {
  'use strict';

  /* suit: the body. trim: the flashes and the plate under the name. */
  const LIVERIES = [
    { id: 'issue',    name: 'DIVISION ISSUE',  suit: null,      trim: null,
      price: 0, note: 'What you were given. Takes your class colour.' },
    { id: 'ash',      name: 'ASH GREY',        suit: '#5b6068', trim: '#9aa4b0', price: 300 },
    { id: 'oxide',    name: 'OXIDE',           suit: '#7d3a2a', trim: '#ffb454', price: 420 },
    { id: 'deepwater', name: 'DEEP WATER',     suit: '#1f3a5c', trim: '#5eeaff', price: 420 },
    { id: 'moss',     name: 'GARDEN MOSS',     suit: '#2f4a32', trim: '#7dff9b', price: 520 },
    { id: 'bone',     name: 'BONE AND BLACK',  suit: '#cdc6b6', trim: '#20242c', price: 700 },
    { id: 'violet',   name: 'LONG VIOLET',     suit: '#3d2a55', trim: '#b98cff', price: 700 },
    { id: 'signal',   name: 'SIGNAL ORANGE',   suit: '#8a4410', trim: '#ff8a3d', price: 900 },
    /* earned, not bought */
    { id: 'conductor', name: "THE CONDUCTOR'S", suit: '#241a2e', trim: '#ff5ea8',
      price: null, earn: 'CLEAR THE CONDUCTOR', has: (ch) => !!(ch.campaign.cleared || {})[16] },
    { id: 'ninetail', name: 'NINE CONTRACTS',  suit: '#0f1a24', trim: '#eaf4ff',
      price: null, earn: 'SETTLE NINE CONTRACTS', has: (ch) => (ch.contractsDone || 0) >= 9 }
  ];

  /* shell: the frame's body. glow: the lit strip and the plume. */
  const PLATES = [
    { id: 'stock',   name: 'STOCK PLATE',    shell: null,      glow: null,
      price: 0, note: 'Bare frame. Lit in the colour of its rarity.' },
    { id: 'chalk',   name: 'CHALK',          shell: '#c9cfd6', glow: '#dfefff', price: 350 },
    { id: 'ember',   name: 'EMBER',          shell: '#3a2018', glow: '#ff7a3d', price: 480 },
    { id: 'tidal',   name: 'TIDAL',          shell: '#1b3140', glow: '#3fd8ff', price: 480 },
    { id: 'wasp',    name: 'WASP',           shell: '#2a2618', glow: '#ffd34d', price: 640 },
    { id: 'nightrun', name: 'NIGHT RUN',     shell: '#12151b', glow: '#ff5ea8', price: 880 },
    { id: 'longhaul', name: 'LONG HAUL',     shell: '#4a4034', glow: '#9fb0c0',
      price: null, earn: 'RIDE 20 KM', has: (ch) => (ch.metresRidden || 0) >= 20000 }
  ];

  const SETS = [
    { id: 'livery', name: 'OPERATIVE LIVERY', list: LIVERIES,
      line: 'Repaints the suit. Seen in third person, on a frame, and by your fireteam.' },
    { id: 'plate', name: 'FRAME PLATE', list: PLATES,
      line: 'Repaints the runner. Overrides the colour it takes from its rarity.' }
  ];

  function ensure(ch) {
    if (!ch.wardrobe) ch.wardrobe = { livery: ['issue'], plate: ['stock'] };
    for (const set of SETS) if (!Array.isArray(ch.wardrobe[set.id])) ch.wardrobe[set.id] = [];
    if (!ch.worn) ch.worn = { livery: 'issue', plate: 'stock' };
    // the free defaults are always owned, whatever an older save says
    if (ch.wardrobe.livery.indexOf('issue') < 0) ch.wardrobe.livery.push('issue');
    if (ch.wardrobe.plate.indexOf('stock') < 0) ch.wardrobe.plate.push('stock');
    return ch;
  }

  const setOf = (id) => SETS.find((s) => s.id === id);
  const itemOf = (setId, id) => (setOf(setId) || SETS[0]).list.find((x) => x.id === id) || null;

  function owns(ch, setId, id) {
    ensure(ch);
    const item = itemOf(setId, id);
    if (!item) return false;
    if (item.price === 0) return true;
    if (item.has) return !!item.has(ch);            // earned ones check the deed
    return ch.wardrobe[setId].indexOf(id) >= 0;
  }

  /* Why it cannot be had yet, in words. Null means it can. */
  function blocker(ch, setId, id) {
    const item = itemOf(setId, id);
    if (!item) return 'UNKNOWN';
    if (owns(ch, setId, id)) return null;
    if (item.price == null) return item.earn || 'EARNED';
    const short = SF.economy.shortOf(ch, { chits: item.price });
    return short ? 'NEED ' + short.name : null;
  }

  function buy(ch, setId, id) {
    if (blocker(ch, setId, id)) return false;
    const item = itemOf(setId, id);
    if (item.price) {
      if (!SF.economy.spend(ch, { chits: item.price })) return false;
    }
    if (ch.wardrobe[setId].indexOf(id) < 0) ch.wardrobe[setId].push(id);
    return true;
  }

  function wear(ch, setId, id) {
    ensure(ch);
    if (!owns(ch, setId, id)) return false;
    ch.worn[setId] = id;
    return true;
  }

  /* What the renderers ask for. Both fall back to null, which means "use
     whatever you were going to use anyway". */
  function suitColours(ch) {
    ensure(ch);
    const it = itemOf('livery', ch.worn.livery);
    return it && it.suit ? { suit: it.suit, trim: it.trim } : null;
  }

  function plateColours(ch) {
    ensure(ch);
    const it = itemOf('plate', ch.worn.plate);
    return it && it.shell ? { shell: it.shell, glow: it.glow } : null;
  }

  SF.cosmetics = {
    SETS, LIVERIES, PLATES,
    ensure, owns, blocker, buy, wear, itemOf, setOf, suitColours, plateColours
  };
})(window.SF);
