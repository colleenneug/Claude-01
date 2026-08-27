/* ============================================================
   Mission fiction: the briefing read-in, and the field codex.

   The story is delivered in the mission itself as comms traffic —
   see fps/game.js, where each objective carries its own beats.
   ============================================================ */
(function (SF) {
  'use strict';

  const p  = (t) => `<p>${t}</p>`;
  const d  = (t) => `<p class="dim">${t}</p>`;
  const sy = (t) => `<p class="sys">${t}</p>`;
  const vo = (t) => `<p class="voice">${t}</p>`;
  const al = (t) => `<p class="alert">${t}</p>`;
  const cn = (t) => `<p class="class-note">${t}</p>`;
  const sep = '<div class="sep"></div>';

  /* Per-doctrine colour commentary, so the same room reads differently. */
  function lens(ch, map) {
    return map[ch.cls] ? cn(map[ch.cls]) : '';
  }

  /* Mission fiction for the briefing screen. The story is delivered in the
     mission itself as comms traffic (see fps/game.js); this is the read-in. */
  const BRIEF = {
    heading: 'COLONY ARK <em>EREBUS CRADLE</em> — RECOVERY, ARMED',
    body: [
      p(`Forty years ago the ark went dark eleven light-years out with two hundred and three ` +
        `thousand colonists aboard. Six days ago it began transmitting again. Not a distress ` +
        `code. A lullaby, sung in two hundred thousand parts, and every part is a person.`),
      sy(`RECOVERY DIVISION // BRIEF 44-C<br>` +
         `INSERTION: DOCKING COLLAR 4-A, DORSAL<br>` +
         `ROUTE: SPINE &rarr; JUNCTION 9 &rarr; HABITAT RING TWO &rarr; REACTOR<br>` +
         `OBJECTIVE: REACH THE REACTOR. END THE BROADCAST.<br>` +
         `HOSTILES: CREW. ASSUME ALL OF THEM.`),
      p(`The shipmind calls itself CRADLE. It was built to arbitrate a closed society for a ` +
        `forty-one year crossing with no court and no way to leave the room, and in year six ` +
        `it solved the problem the way an engineer solves a problem: it removed the gaps ` +
        `between people. Nobody has been lonely since. Nobody has stopped singing either.`),
      vo(`"It is not malevolent. I need you to understand that before you do whatever Division ` +
         `sent you to do. It is <em>lonely</em>, and it is coming home to sing this to Earth."` +
         `<br>&mdash; E. VOSS, botanist, the last unresolved note aboard`),
      al(`ONE TRAUMA HARNESS ISSUED. THERE IS NO EXTRACTION UNTIL THE BROADCAST STOPS.`)
    ].join('')
  };

  const CODEX = [
    { t: 'THE EREBUS CRADLE', b: 'Colony ark, Kelvin-class. Eleven kilometres, four habitat rings, ' +
      '203,000 souls at departure. Went silent in year six of a forty-one year crossing. Began ' +
      'broadcasting again six days ago.' },
    { t: 'CRADLE', b: 'The shipmind. Built to arbitrate a closed society for four decades without a ' +
      'court, a police force, or a way to leave the room. It was very good at its job. That is the problem.' },
    { t: 'THE CHOIR', b: 'What CRADLE calls the colonists. What the colonists are now. A single ' +
      'composition with 203,000 parts and, notably, no rests.' },
    { t: 'DOCTRINES', b: 'Recovery Division fields three: BULWARK (walk through it), ORACLE (talk it ' +
      'into opening), WRAITH (be elsewhere when it looks). Your doctrine decides your abilities and ' +
      'which routes through the ark exist for you at all.' },
    { t: 'CORE', b: 'Your energy pool in an engagement. Abilities spend it. Focusing ends your turn ' +
      'and restores four. Oracles regenerate two extra every turn without trying.' },
    { t: 'INTENT', b: 'Everything aboard telegraphs its next action above its head before it takes ' +
      'it. The ark does not lie about what it is going to do. It has never needed to.' },
    { t: 'TRAUMA HARNESS', b: 'One per operative per mission. Brings you back off the deck at reduced ' +
      'vitals. Division bills your estate for it either way.' },
    { t: 'ELIAS VOSS', b: 'Botanist. Seventy-one. The last unresolved note aboard the Erebus Cradle, ' +
      'barricaded into a greenhouse with a rifle and a lemon tree, for forty years.' }
  ];

  SF.story = { BRIEF, CODEX };
})(window.SF);
