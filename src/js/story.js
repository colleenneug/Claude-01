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
    heading: 'CATHEDRAL ARK <em>EREBUS CRADLE</em> — ASCENT, ARMED',
    body: [
      p(`Eleven kilometres of consecrated hull, holding station eleven light-years out, and at the ` +
        `top of it a light that has not gone out in forty years. Two hundred and three thousand ` +
        `people were promised they would never die. The Pale kept the promise. That is the problem.`),
      sy(`THE DEEP // FIRST DESCENT<br>` +
         `INSERTION: DOCKING COLLAR 4-A, DORSAL<br>` +
         `ROUTE: SPINE &rarr; JUNCTION 9 &rarr; HABITAT RING TWO &rarr; THE RELIQUARY<br>` +
         `OBJECTIVE: CLIMB THE ARK. PUT OUT THE FIRST LIGHT.<br>` +
         `HOSTILES: THE BLESSED. ASSUME ALL OF THEM.`),
      p(`You died on the lower decks with everyone else, and unlike everyone else you were not ` +
        `worth resurrecting. The Pale sorts the faithful from the surplus, and it sorted you. ` +
        `Something further down disagreed. It has been forty years and it has finished with you now.`),
      vo(`"You will meet things up there wearing the faces of people you knew, and they will be ` +
         `glad to see you, and they will not stop. Kill the spark or do not bother killing them."` +
         `<br>&mdash; CHOIRMASTER, who went first and did not come back whole`),
      al(`THE DEEP RETURNS YOU THREE TIMES. THE FIRST LIGHT IS NOT OWED THAT COURTESY, AND NEITHER ARE YOU IN ITS ROOM.`)
    ].join('')
  };

  const CODEX = [
    { t: 'THE EREBUS CRADLE', b: 'Cathedral ark, Kelvin-class. Eleven kilometres, four habitat rings, ' +
      '203,000 souls at departure and 203,000 still aboard, which is not the same as alive. Consecrated ' +
      'in year six of a forty-one year crossing.' },
    { t: 'THE PALE', b: 'The light at the top of the ark. It offers one thing and offers it absolutely: ' +
      'you will not die. It does not ask what you wanted, and it has never once let go of anything ' +
      'it was given.' },
    { t: 'THE DEEP', b: 'What answered from underneath when the Pale would not answer you. It does not ' +
      'promise anything. It raises you, it lets you go when you are done, and it considers that the ' +
      'more generous of the two offers.' },
    { t: 'PALEBEARERS', b: 'The blessed dead. Killing one buys you about five seconds, because the ' +
      'spark riding its shoulder is already rebuilding it. Kill the spark and the body stays a body.' },
    { t: 'SPARKS', b: 'Small, fast, and the actual enemy. Each one is a fragment of the first light ' +
      'with a person attached. It will not fight you. It does not have to.' },
    { t: 'DOCTRINES', b: 'The Deep raises three kinds: CARAPACE (walk through it), HOLLOW (unmake it), ' +
      'SHROUD (be elsewhere when it looks). Your doctrine decides your abilities and which routes up ' +
      'the ark exist for you at all.' },
    { t: 'DREAD', b: 'What the Deep pays you in. It rises as you kill and drains the moment you stop, ' +
      'and it is also the only thing keeping your wounds shut — there is no resting up in a corner ' +
      'any more. Fill the meter and it spends itself all at once.' },
    { t: 'DEVOUR', b: 'The Deep does not heal you. It lets you take what you kill. Push forward or ' +
      'bleed out; those are the two options and they have always been the two options.' },
    { t: 'CANDLE', b: 'A Palebearer somewhere above you who has decided you are worth talking to. ' +
      'Forty years alone in a garden with a rifle and a lemon tree will do that. She is waiting at ' +
      'the top and she is not going to move.' }
  ];

  SF.story = { BRIEF, CODEX };
})(window.SF);
