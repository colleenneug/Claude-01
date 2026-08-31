/* ============================================================
   Contracts.

   Work you pick up in the station and finish anywhere else. A
   contract is a counter and a payout: take up to four, go out, come
   back when they are full. They are the reason to visit the Cradle
   between missions rather than dropping straight back into one.

   Progress is reported from wherever it happens — the mission loop
   calls note() as things occur — and lives on the character, so it
   survives leaving and coming back.
   ============================================================ */
(function (SF) {
  'use strict';

  const MAX_ACTIVE = 4;

  /* Each contract counts one thing. Tiers scale the target and the payout
     together, and the tier you are offered rises with your rank. */
  const TEMPLATES = [
    { id: 'kills',   name: 'THINNING',        track: 'kills',
      text: 'Put down {n} hostiles.',            tiers: [25, 45, 70] },
    { id: 'heads',   name: 'PRECISION',       track: 'heads',
      text: 'Land {n} precision hits.',          tiers: [15, 30, 50] },
    { id: 'crates',  name: 'SCAVENGER',       track: 'crates',
      text: 'Crack {n} crates on the destinations.', tiers: [3, 6, 10] },
    { id: 'events',  name: 'FIRST RESPONDER', track: 'events',
      text: 'Resolve {n} public events.',        tiers: [1, 2, 4] },
    { id: 'ride',    name: 'LONG HAUL',        track: 'ride',
      text: 'Cover {n} metres on a runner.',     tiers: [1500, 3500, 7000] },
    { id: 'missions', name: 'THE LONG WAY',    track: 'missions',
      text: 'Clear {n} missions aboard the ark.', tiers: [1, 2, 4] },
    { id: 'ability', name: 'CHANNELLED',       track: 'ability',
      text: 'Use your class ability {n} times.', tiers: [6, 12, 20] },
    { id: 'nodes',   name: 'PERFECT PITCH',    track: 'nodes',
      text: 'Answer {n} resonance nodes correctly.', tiers: [4, 8, 14] }
  ];

  const TIER_NAMES = ['STANDING', 'SEALED', 'BLACK SEAL'];
  const TIER_COLOUR = ['#9fb0c0', '#5eeaff', '#ffb454'];

  const byId = (id) => TEMPLATES.find((t) => t.id === id);

  let seq = 0;

  function make(tid, tier) {
    const t = byId(tid);
    const need = t.tiers[tier];
    return {
      uid: 'c' + (++seq) + '-' + Date.now().toString(36),
      tid: tid, tier: tier, track: t.track, name: t.name,
      text: t.text.replace('{n}', need),
      need: need, have: 0, claimed: false
    };
  }

  const BOARD_SIZE = 5;

  /* The board always has something on it. Taking one tops it back up from
     whatever you are not already carrying, so it never runs dry — a board
     that empties as you work is a board you stop visiting. */
  function restock(ch) {
    ensure(ch);
    const spoken = new Set(ch.bounties.map((b) => b.tid)
                     .concat(ch.board.map((b) => b.tid)));
    const maxTier = ch.level >= 12 ? 2 : ch.level >= 5 ? 1 : 0;
    for (const t of TEMPLATES) {
      if (ch.board.length >= BOARD_SIZE) break;
      if (spoken.has(t.id)) continue;
      ch.board.push(make(t.id, Math.min(maxTier, Math.floor(Math.random() * (maxTier + 1)))));
      spoken.add(t.id);
    }
    return ch.board;
  }

  function refreshBoard(ch) {
    ensure(ch);
    ch.board = [];
    return restock(ch);
  }

  function ensure(ch) {
    if (!ch.bounties) ch.bounties = [];
    if (!ch.board) ch.board = [];
    if (typeof ch.contractsDone !== 'number') ch.contractsDone = 0;
    return ch;
  }

  function accept(ch, uid) {
    ensure(ch);
    if (ch.bounties.length >= MAX_ACTIVE) return null;
    const i = ch.board.findIndex((b) => b.uid === uid);
    if (i < 0) return null;
    const b = ch.board.splice(i, 1)[0];
    ch.bounties.push(b);
    restock(ch);
    return b;
  }

  function abandon(ch, uid) {
    ensure(ch);
    const i = ch.bounties.findIndex((b) => b.uid === uid);
    if (i < 0) return false;
    ch.bounties.splice(i, 1);
    restock(ch);
    return true;
  }

  /* Report progress. Returns the contracts that just filled up, so the
     mission can say so. */
  function note(ch, track, amount) {
    if (!ch || !ch.bounties || !ch.bounties.length) return null;
    let filled = null;
    for (const b of ch.bounties) {
      if (b.track !== track || b.have >= b.need) continue;
      b.have = Math.min(b.need, b.have + (amount || 1));
      if (b.have >= b.need) (filled = filled || []).push(b);
    }
    return filled;
  }

  const complete = (b) => b.have >= b.need;
  const readyCount = (ch) => (ch.bounties || []).filter(complete).length;

  /* The payout: experience, runner parts, and one piece of salvage rolled a
     few tiers above where you are, so a black seal is worth crossing a zone
     for. */
  function reward(b) {
    return { xp: 140 + b.tier * 220, parts: 2 + b.tier * 2, dropTier: 7 + b.tier * 4 };
  }

  function claim(ch) {
    ensure(ch);
    const done = ch.bounties.filter(complete);
    if (!done.length) return null;
    let xp = 0, parts = 0;
    const drops = [];
    for (const b of done) {
      const r = reward(b);
      xp += r.xp;
      parts += r.parts;
      drops.push(...SF.gear.rollDrops(ch, r.dropTier, b.tier >= 2));
    }
    const bundle = SF.gear.rollParts(parts);
    SF.gear.grantParts(ch, bundle);
    SF.gear.grant(ch, drops);
    ch.contractsDone += done.length;
    const keptUids = new Set(done.map((b) => b.uid));
    ch.bounties = ch.bounties.filter((b) => !keptUids.has(b.uid));
    refreshBoard(ch);
    return { count: done.length, xp, parts: bundle, drops };
  }

  SF.bounties = {
    TEMPLATES, TIER_NAMES, TIER_COLOUR, MAX_ACTIVE,
    ensure, refreshBoard, restock, accept, abandon, note, claim, reward,
    complete, readyCount, byId
  };
})(window.SF);
