/* Baseball Masters — data layer: rosters, pitch arsenals, stadiums, card tiers. */
(function (BM) {
  'use strict';

  // Deterministic RNG so a team's roster is identical every session.
  function mulberry32(seed) {
    let a = seed >>> 0;
    return function () {
      a = (a + 0x6D2B79F5) >>> 0;
      let t = Math.imul(a ^ (a >>> 15), 1 | a);
      t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
      return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
    };
  }
  function hash(str) {
    let h = 2166136261;
    for (let i = 0; i < str.length; i++) { h ^= str.charCodeAt(i); h = Math.imul(h, 16777619); }
    return h >>> 0;
  }

  // ---------------------------------------------------------------- pitches
  // breakX: positive = runs toward the pitcher's arm side, in feet at the plate.
  // breakZ: positive = "rises" (resists gravity), negative = drops.
  const PITCHES = {
    FF: { id: 'FF', name: '4-Seam Fastball', velo: 0,   bx: 0.15, bz: 0.85, color: '#ff5a5a' },
    SI: { id: 'SI', name: 'Sinker',          velo: -2,  bx: 0.90, bz: -0.35, color: '#ff9f43' },
    CT: { id: 'CT', name: 'Cutter',          velo: -4,  bx: -0.75, bz: 0.15, color: '#feca57' },
    SL: { id: 'SL', name: 'Slider',          velo: -9,  bx: -1.35, bz: -0.75, color: '#48dbfb' },
    CB: { id: 'CB', name: 'Curveball',       velo: -14, bx: -0.80, bz: -2.05, color: '#5f7fff' },
    CH: { id: 'CH', name: 'Changeup',        velo: -10, bx: 0.85, bz: -1.05, color: '#1dd1a1' },
    SP: { id: 'SP', name: 'Splitter',        velo: -8,  bx: 0.20, bz: -1.75, color: '#a29bfe' },
    KN: { id: 'KN', name: 'Knuckleball',     velo: -26, bx: 0.0,  bz: -0.60, color: '#dfe6e9', erratic: true }
  };
  const PITCH_ORDER = ['FF', 'SI', 'CT', 'SL', 'CB', 'CH', 'SP', 'KN'];

  // ---------------------------------------------------------------- names
  const FIRST = ['Marcus', 'Dante', 'Kai', 'Ronan', 'Elias', 'Nico', 'Julio', 'Trey', 'Bode', 'Ozzie',
    'Rafa', 'Cole', 'Jaylen', 'Hideo', 'Sammy', 'Luka', 'Ike', 'Cruz', 'Wes', 'Mateo',
    'Bryce', 'Yuji', 'Devon', 'Arlo', 'Tito', 'Griffin', 'Jonah', 'Rico', 'Shane', 'Abel',
    'Tomas', 'Vance', 'Emeka', 'Silas', 'Rhys', 'Omar', 'Kenji', 'Dax', 'Lorenzo', 'Curt'];
  const LAST = ['Ruiz', 'Okafor', 'Vance', 'Delgado', 'Tanaka', 'Whitlock', 'Baptiste', 'Kolar', 'Reyes',
    'Ferraro', 'Mbeki', 'Sandoval', 'Novak', 'Kingsley', 'Ortega', 'Halloran', 'Pruitt', 'Song',
    'Vasquez', 'Bellamy', 'Ivanov', 'Castellanos', 'Doyle', 'Nakamura', 'Ashford', 'Mercado',
    'Lindqvist', 'Boone', 'Aguilar', 'Strand', 'Cavanaugh', 'Diallo', 'Peralta', 'Yates',
    'Moreau', 'Escobar', 'Hargrove', 'Salas', 'Winfield', 'Beckett'];

  const POSITIONS = ['C', '1B', '2B', '3B', 'SS', 'LF', 'CF', 'RF', 'DH'];

  function tierOf(ovr) {
    if (ovr >= 88) return { name: 'Diamond', color: '#5ad7ff' };
    if (ovr >= 80) return { name: 'Gold', color: '#ffd24a' };
    if (ovr >= 72) return { name: 'Silver', color: '#c9d2dd' };
    if (ovr >= 64) return { name: 'Bronze', color: '#c98a52' };
    return { name: 'Common', color: '#8d98a6' };
  }

  function batterOverall(p) {
    return Math.round(p.contact * 0.3 + p.power * 0.26 + p.vision * 0.16 +
                      p.speed * 0.12 + p.fielding * 0.16);
  }
  function pitcherOverall(p) {
    return Math.round(p.velo * 0.3 + p.control * 0.34 + p.movement * 0.24 + p.stamina * 0.12);
  }

  function makeBatter(rng, pos, quality) {
    const r = (lo, hi) => Math.round(lo + rng() * (hi - lo));
    const bump = Math.round(quality * 10);
    const p = {
      kind: 'batter',
      name: FIRST[Math.floor(rng() * FIRST.length)] + ' ' + LAST[Math.floor(rng() * LAST.length)],
      pos: pos,
      bats: rng() < 0.31 ? 'L' : (rng() < 0.05 ? 'S' : 'R'),
      contact: Math.min(99, r(45, 82) + bump),
      power: Math.min(99, r(38, 88) + bump),
      vision: Math.min(99, r(40, 84) + bump),
      discipline: Math.min(99, r(38, 86) + bump),
      speed: Math.min(99, r(35, 90) + bump),
      fielding: Math.min(99, r(45, 88) + bump),
      arm: Math.min(99, r(45, 90) + bump)
    };
    p.ovr = batterOverall(p);
    p.tier = tierOf(p.ovr);
    p.stats = newBatterStats();
    return p;
  }

  function makePitcher(rng, role, quality) {
    const r = (lo, hi) => Math.round(lo + rng() * (hi - lo));
    const bump = Math.round(quality * 10);
    const arsenal = ['FF'];
    const pool = ['SL', 'CB', 'CH', 'SI', 'CT', 'SP', 'KN'];
    const count = role === 'SP' ? 3 + Math.floor(rng() * 2) : 2 + Math.floor(rng() * 2);
    while (arsenal.length < count + 1 && pool.length) {
      const idx = Math.floor(rng() * pool.length);
      arsenal.push(pool.splice(idx, 1)[0]);
    }
    const p = {
      kind: 'pitcher',
      name: FIRST[Math.floor(rng() * FIRST.length)] + ' ' + LAST[Math.floor(rng() * LAST.length)],
      pos: role,
      throws: rng() < 0.28 ? 'L' : 'R',
      velo: Math.min(99, r(50, 88) + bump),
      control: Math.min(99, r(45, 86) + bump),
      movement: Math.min(99, r(45, 88) + bump),
      stamina: role === 'SP' ? Math.min(99, r(68, 92) + bump) : Math.min(99, r(40, 62) + bump),
      arsenal: arsenal
    };
    p.topVelo = Math.round(86 + (p.velo / 99) * 16); // 86 - 102 mph
    p.ovr = pitcherOverall(p);
    p.tier = tierOf(p.ovr);
    p.stamLeft = 100;
    p.stats = newPitcherStats();
    return p;
  }

  function newBatterStats() {
    return { pa: 0, ab: 0, h: 0, hr: 0, rbi: 0, bb: 0, k: 0, r: 0, tb: 0 };
  }
  function newPitcherStats() {
    return { outs: 0, h: 0, er: 0, bb: 0, k: 0, hr: 0, pitches: 0 };
  }

  // ---------------------------------------------------------------- teams
  const TEAM_DEFS = [
    { id: 'BOL', city: 'Brooklyn', nick: 'Bolts',     primary: '#1e5cff', secondary: '#ffd24a' },
    { id: 'RDW', city: 'Redwood',  nick: 'Redwoods',  primary: '#0f7a4a', secondary: '#f2f2f2' },
    { id: 'MAG', city: 'Magnolia', nick: 'Monarchs',  primary: '#7d2ae8', secondary: '#ffb01f' },
    { id: 'HRB', city: 'Harbor',   nick: 'Hammers',   primary: '#d21f3c', secondary: '#101820' },
    { id: 'SND', city: 'Sonora',   nick: 'Sandcats',  primary: '#e07b1a', secondary: '#123b63' },
    { id: 'GLC', city: 'Glacier',  nick: 'Gales',     primary: '#0fb9d4', secondary: '#17263b' },
    { id: 'IRN', city: 'Ironport', nick: 'Anchors',   primary: '#37474f', secondary: '#ff6b35' },
    { id: 'VLT', city: 'Valletta', nick: 'Vipers',    primary: '#1b8a3a', secondary: '#111111' }
  ];

  function buildTeam(def) {
    const rng = mulberry32(hash(def.id + '-roster'));
    const quality = rng(); // 0..1 franchise strength
    const lineup = [];
    const used = POSITIONS.slice();
    for (let i = 0; i < 9; i++) {
      lineup.push(makeBatter(rng, used[i], quality * (i < 5 ? 1 : 0.6)));
    }
    // Bat the best hitters near the top, the way a real card would be set.
    lineup.sort((a, b) => (b.contact + b.power * 0.8) - (a.contact + a.power * 0.8));
    const bench = [];
    for (let i = 0; i < 4; i++) bench.push(makeBatter(rng, POSITIONS[i % 8], quality * 0.5));
    const rotation = [];
    for (let i = 0; i < 4; i++) rotation.push(makePitcher(rng, 'SP', quality * (1 - i * 0.12)));
    const bullpen = [];
    for (let i = 0; i < 4; i++) bullpen.push(makePitcher(rng, 'RP', quality * (0.9 - i * 0.1)));
    bullpen[0].pos = 'CL';

    return Object.assign({}, def, {
      name: def.city + ' ' + def.nick,
      lineup: lineup, bench: bench, rotation: rotation, bullpen: bullpen,
      ovr: Math.round(
        (lineup.reduce((s, p) => s + p.ovr, 0) / 9) * 0.6 +
        (rotation.reduce((s, p) => s + p.ovr, 0) / 4) * 0.4)
    });
  }

  function allTeams() { return TEAM_DEFS.map(buildTeam); }

  // ---------------------------------------------------------------- parks
  // Fence distance by spray angle. -45 = left-field line, +45 = right-field line.
  const STADIUMS = [
    { id: 'harbor', name: 'Harborlight Park',  lf: 318, lc: 372, cf: 401, rc: 368, rf: 325, wall: 8,  turf: '#2f7d3e' },
    { id: 'canyon', name: 'Canyon Yards',      lf: 347, lc: 388, cf: 415, rc: 383, rf: 340, wall: 12, turf: '#2b7439' },
    { id: 'cove',   name: 'Iron Cove Stadium', lf: 310, lc: 360, cf: 392, rc: 375, rf: 352, wall: 18, turf: '#347f43' },
    { id: 'dome',   name: 'The Hexadome',      lf: 330, lc: 375, cf: 400, rc: 375, rf: 330, wall: 10, turf: '#3a8a4a' }
  ];

  function fenceAt(stadium, sprayDeg) {
    const s = Math.max(-45, Math.min(45, sprayDeg));
    const pts = [
      { a: -45, d: stadium.lf }, { a: -22, d: stadium.lc }, { a: 0, d: stadium.cf },
      { a: 22, d: stadium.rc }, { a: 45, d: stadium.rf }
    ];
    for (let i = 0; i < pts.length - 1; i++) {
      if (s >= pts[i].a && s <= pts[i + 1].a) {
        const t = (s - pts[i].a) / (pts[i + 1].a - pts[i].a);
        return pts[i].d + (pts[i + 1].d - pts[i].d) * t;
      }
    }
    return stadium.cf;
  }

  BM.data = {
    mulberry32: mulberry32, hash: hash,
    PITCHES: PITCHES, PITCH_ORDER: PITCH_ORDER, POSITIONS: POSITIONS,
    TEAM_DEFS: TEAM_DEFS, STADIUMS: STADIUMS,
    allTeams: allTeams, buildTeam: buildTeam, fenceAt: fenceAt,
    makeBatter: makeBatter, makePitcher: makePitcher,
    tierOf: tierOf, batterOverall: batterOverall, pitcherOverall: pitcherOverall,
    newBatterStats: newBatterStats, newPitcherStats: newPitcherStats
  };
})(window.BM = window.BM || {});
