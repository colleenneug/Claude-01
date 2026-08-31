/* ============================================================
   What the Division pays in.

   Two currencies, deliberately unlike each other:

     CHITS   the everyday one. Every mission, every contract, every
             crate pays some. You are meant to always have a few and
             always be about to spend them.

     PRIME   a pre-collapse component that still works. Nobody makes
             these any more; they are pulled out of survey vaults and
             off the things that are hard to kill. Scarce on purpose,
             and the only thing that will bolt a part onto a frame.

   The point of the pair is that chits answer "what shall I buy" and
   prime answers "what is worth upgrading" — a decision you would not
   have to make if one currency did both.
   ============================================================ */
(function (SF) {
  'use strict';

  const CURRENCIES = [
    { id: 'chits', name: 'CHITS', short: 'CH', colour: '#ffb454',
      line: 'Division scrip. Everything pays it; the store takes it.' },
    { id: 'prime', name: 'PRIME', short: 'PR', colour: '#b98cff',
      line: 'Pre-collapse components. Nobody makes them. The bench needs one per part.' }
  ];

  /* Prices, in one place so the whole economy can be read at a glance. */
  const PRICE = {
    fitPart:     { prime: 1 },                    // the bench, per part fitted
    strip:       { chits: 60 },                   // pulling parts back off a frame
    part:        { chits: 140 },                  // one part, bought outright
    partBundle:  { chits: 600 },                  // five of them, at a discount
    salvageCase: { chits: 450 },                  // two rolls of gear
    primeTrade:  { chits: 1200 },                 // chits into prime, badly
    reroll:      { chits: 90 }                    // a fresh contract board
  };

  /* What things pay. */
  const PAY = {
    /* Every third mission turns up a prime. Vaults and black-seal contracts
       are the real source, but both are behind mission six — without this a
       new operative could not fit a single part until then. */
    mission:  (n) => ({ chits: 120 + n * 22, prime: n % 3 === 0 ? 1 : 0 }),
    patrol:   (events) => ({ chits: 90 + events * 60 }),
    cache:    () => ({ chits: 35 }),
    vault:    () => ({ chits: 90, prime: 1 }),
    event:    (tier) => ({ chits: 70 * tier }),
    boss:     () => ({ chits: 900, prime: 3 }),
    contract: (tier) => ({ chits: 150 + tier * 180, prime: tier >= 2 ? 1 : 0 }),
    /* The Rook pays in chits by weight, and occasionally turns up a prime in
       something epic or better. */
    breakdown: (items) => {
      let chits = 0, prime = 0;
      for (const it of items) {
        const rank = SF.gear.rarityRank(it.rarity);
        chits += 12 + rank * 14;
        if (rank >= 3 && Math.random() < 0.18) prime++;
      }
      return { chits, prime };
    }
  };

  /* A new operative is issued enough to use the bench once and buy something
     small — an economy you cannot touch for six missions teaches nothing. */
  function ensure(ch) {
    if (!ch.purse) ch.purse = { chits: 250, prime: 2 };
    for (const c of CURRENCIES) if (typeof ch.purse[c.id] !== 'number') ch.purse[c.id] = 0;
    return ch;
  }

  const balance = (ch, id) => (ensure(ch), ch.purse[id] || 0);

  function earn(ch, bundle) {
    ensure(ch);
    if (!bundle) return ch;
    for (const k of Object.keys(bundle)) {
      if (ch.purse[k] == null) continue;
      ch.purse[k] += bundle[k] || 0;
    }
    return ch;
  }

  /* Can this be paid for? Returns null if yes, or the currency short of it. */
  function shortOf(ch, cost) {
    ensure(ch);
    if (!cost) return null;
    for (const k of Object.keys(cost)) {
      if ((ch.purse[k] || 0) < cost[k]) return CURRENCIES.find((c) => c.id === k) || null;
    }
    return null;
  }

  const canAfford = (ch, cost) => !shortOf(ch, cost);

  function spend(ch, cost) {
    if (!canAfford(ch, cost)) return false;
    for (const k of Object.keys(cost)) ch.purse[k] -= cost[k];
    return true;
  }

  /* "140 CHITS" / "600 CHITS · 1 PRIME" — the same string everywhere. */
  function priceText(cost) {
    if (!cost) return 'FREE';
    return Object.keys(cost)
      .filter((k) => cost[k])
      .map((k) => {
        const c = CURRENCIES.find((x) => x.id === k);
        return cost[k] + ' ' + (c ? c.name : k.toUpperCase());
      })
      .join(' · ');
  }

  const byId = (id) => CURRENCIES.find((c) => c.id === id);

  SF.economy = {
    CURRENCIES, PRICE, PAY,
    ensure, balance, earn, spend, canAfford, shortOf, priceText, byId
  };
})(window.SF);
