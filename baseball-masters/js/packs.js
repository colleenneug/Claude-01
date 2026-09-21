/* Baseball Masters — save data, Stubs currency, and card packs.
   A light nod to Diamond Dynasty: win games, earn Stubs, crack packs, build
   "My Team" out of whatever you pull. Everything lives in localStorage. */
(function (BM) {
  'use strict';
  const D = BM.data;
  const KEY = 'bm_save_v1';

  const PACKS = [
    { id: 'standard', name: 'Standard Pack', cost: 120, cards: 3,
      odds: { Common: 0.42, Bronze: 0.30, Silver: 0.18, Gold: 0.08, Diamond: 0.02 } },
    { id: 'premium', name: 'Premium Pack', cost: 300, cards: 4,
      odds: { Common: 0.18, Bronze: 0.30, Silver: 0.28, Gold: 0.18, Diamond: 0.06 } },
    { id: 'diamond', name: 'Diamond Pack', cost: 700, cards: 3,
      odds: { Common: 0, Bronze: 0.10, Silver: 0.32, Gold: 0.40, Diamond: 0.18 } }
  ];

  function defaultSave() {
    return {
      stubs: 500,
      packsOpened: 0,
      collection: [],           // array of card objects
      record: { w: 0, l: 0 },
      franchise: null,
      settings: { difficulty: 'veteran', innings: 9 }
    };
  }

  function load() {
    try {
      const raw = localStorage.getItem(KEY);
      if (!raw) return defaultSave();
      const s = JSON.parse(raw);
      return Object.assign(defaultSave(), s);
    } catch (e) { return defaultSave(); }
  }

  function save(s) {
    try { localStorage.setItem(KEY, JSON.stringify(s)); } catch (e) { /* storage unavailable */ }
  }

  function rollTier(odds) {
    const r = Math.random();
    let acc = 0;
    const order = ['Diamond', 'Gold', 'Silver', 'Bronze', 'Common'];
    for (const tier of order) {
      acc += odds[tier] || 0;
    }
    let roll = Math.random() * acc, sum = 0;
    for (const tier of order) {
      sum += odds[tier] || 0;
      if (roll <= sum) return tier;
    }
    return 'Common';
  }

  const TIER_QUALITY = { Common: 0.05, Bronze: 0.25, Silver: 0.5, Gold: 0.78, Diamond: 0.97 };
  const TIER_MIN_OVR = { Common: 0, Bronze: 64, Silver: 72, Gold: 80, Diamond: 88 };

  function makeCard(tier) {
    const rng = Math.random;
    const isPitcher = rng() < 0.38;
    let card;
    let guard = 0;
    do {
      const q = TIER_QUALITY[tier] + (rng() - 0.5) * 0.12;
      if (isPitcher) {
        card = D.makePitcher(rng, rng() < 0.7 ? 'SP' : 'RP', Math.max(0, Math.min(1, q)));
      } else {
        const pos = D.POSITIONS[Math.floor(rng() * D.POSITIONS.length)];
        card = D.makeBatter(rng, pos, Math.max(0, Math.min(1, q)));
      }
      guard++;
    } while (card.ovr < TIER_MIN_OVR[tier] && guard < 12);
    card.tier = D.tierOf(card.ovr);
    card.id = 'card_' + Date.now().toString(36) + Math.floor(rng() * 1e6).toString(36);
    card.pulledAt = Date.now();
    return card;
  }

  function openPack(save_, packId) {
    const pack = PACKS.find(p => p.id === packId);
    if (!pack || save_.stubs < pack.cost) return null;
    save_.stubs -= pack.cost;
    save_.packsOpened++;
    const pulled = [];
    for (let i = 0; i < pack.cards; i++) {
      const tier = rollTier(pack.odds);
      const card = makeCard(tier);
      pulled.push(card);
      save_.collection.push(card);
    }
    save(save_);
    return pulled;
  }

  /** Build a lineup/rotation out of the best cards the player owns, padding
   *  with generated fillers so the team is always fully staffed. */
  function myTeam(save_) {
    const rng = D.mulberry32(D.hash('my-team-filler'));
    const coll = save_.collection.slice();
    const batters = coll.filter(c => c.kind === 'batter').sort((a, b) => b.ovr - a.ovr);
    const pitchers = coll.filter(c => c.kind === 'pitcher').sort((a, b) => b.ovr - a.ovr);

    const lineup = [];
    const usedIds = new Set();
    for (const pos of D.POSITIONS) {
      let pick = batters.find(b => b.pos === pos && !usedIds.has(b.id));
      if (!pick) pick = batters.find(b => !usedIds.has(b.id));
      if (!pick) pick = D.makeBatter(rng, pos, 0.35);
      usedIds.add(pick.id || (pick.id = 'fill_' + pos + rng()));
      // Slot them at this position for lineup/fielding purposes even if the
      // card itself was pulled at a different spot.
      lineup.push(Object.assign({}, pick, { pos: pos, stats: D.newBatterStats() }));
    }
    const bench = [];
    for (let i = 0; i < 4; i++) bench.push(D.makeBatter(rng, D.POSITIONS[i % 8], 0.3));

    const rotation = [];
    const pUsed = new Set();
    for (let i = 0; i < 4; i++) {
      let pick = pitchers.find(p => !pUsed.has(p.id));
      if (!pick) pick = D.makePitcher(rng, 'SP', 0.35);
      pUsed.add(pick.id || (pick.id = 'fillp_' + i + rng()));
      rotation.push(Object.assign({}, pick, { pos: 'SP', stamLeft: 100, stats: D.newPitcherStats() }));
    }
    const bullpen = [];
    for (let i = 0; i < 4; i++) {
      bullpen.push(D.makePitcher(rng, i === 0 ? 'CL' : 'RP', 0.4));
    }

    const ovr = Math.round(
      (lineup.reduce((s, p) => s + p.ovr, 0) / 9) * 0.6 +
      (rotation.reduce((s, p) => s + p.ovr, 0) / 4) * 0.4);

    return {
      id: 'YOU', city: 'Your', nick: 'Legends', name: 'Your Legends',
      primary: '#f2c14e', secondary: '#171717',
      lineup: lineup, bench: bench, rotation: rotation, bullpen: bullpen, ovr: ovr
    };
  }

  BM.packs = { PACKS: PACKS, load: load, save: save, openPack: openPack, myTeam: myTeam, makeCard: makeCard };
})(window.BM = window.BM || {});
