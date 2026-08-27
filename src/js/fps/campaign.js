/* ============================================================
   The campaign: ten missions run end to end down the ark.

   They are connected in three ways. Geographically — each mission
   starts where the last one finished, walking the route from the
   docking collar to Deck Zero. Mechanically — rank, XP and the
   unlock chain persist on the character, and every mission carries
   forward what the last one cost you. Narratively — the comms beats
   run as one continuous thread.

   Difficulty climbs on four axes at once: enemy count, enemy type,
   a health multiplier and a damage multiplier.
   ============================================================ */
(function (SF) {
  'use strict';

  /* Escalation curve across sixteen missions. Mission 1 is baseline; the
     Conductor is fought against roughly 2.1x health and 1.75x damage on top
     of much harder compositions. */
  const scaleFor = (i) => ({
    hp: 1 + 0.074 * i,
    damage: 1 + 0.05 * i,
    index: i
  });

  /* Sixteen missions, one route. Several sectors are fought twice — arrival
     and then the counter-attack — which is how the ark closes behind you. */
  const MISSIONS = [
    { id: 'breach', n: 1, zone: 'dock', from: null, name: 'HARD DOCK',
      objective: 'Clear the docking collar.',
      brief: 'You are through the collar. Nothing has moved on this deck in forty years, ' +
             'and something is moving on it now.',
      waves: [['drone', 3]],
      beats: [[0.4, 'CRADLE', 'Welcome back. Your bunk has been kept warm.'],
              [3.4, 'DIVISION', 'Asset is aboard. Forty years of silence and it greets you by name — mind that.'],
              [7.5, 'VOSS', 'Whoever you are: it lets you in because it wants an audience. Move.']] },

    { id: 'spine', n: 2, zone: 'spine1', from: 'dock', name: 'THE LONG SPINE',
      objective: 'Push down the maintenance spine.',
      brief: 'Eleven kilometres of service corridor. The singing gets louder the further in you go.',
      waves: [['drone', 3], ['thrall', 2]],
      beats: [[0.5, 'VOSS', 'Those were people. Year six. It took the gaps between them and called it a colony.'],
              [7.0, 'DIVISION', 'Do not engage the ones that are singing. Correction — do engage. Command has revised.']] },

    { id: 'junction', n: 3, zone: 'junction', from: 'spine1', name: 'JUNCTION NINE',
      objective: 'Take Junction 9.',
      brief: 'The junction is the only way aft. It is also the only room on this deck with four doors.',
      waves: [['thrall', 4], ['drone', 3]],
      beats: [[0.5, 'CRADLE', 'You are flat. Everything that comes aboard is flat, and I tune it.'],
              [6.5, 'VOSS', 'Four doors. They will use all four. I would use all four.']] },

    { id: 'counter', n: 4, zone: 'junction', from: 'junction', name: 'COUNTER-ATTACK',
      objective: 'Hold Junction 9 until the blast doors cycle.',
      brief: 'You took it. Now keep it. The doors need ninety seconds and the ark has noticed.',
      waves: [['thrall', 6], ['drone', 4], ['warden', 1]],
      beats: [[0.5, 'DIVISION', 'Blast doors cycling. Hold the room, operative.'],
              [6.0, 'CRADLE', 'I am not sending them to hurt you. I am sending them to finish the piece.']] },

    { id: 'aft', n: 5, zone: 'spine2', from: 'junction', name: 'AFT RUN',
      objective: 'Break through to the habitat ring.',
      brief: 'A narrow run with a Warden frame standing in it. You do not talk your way past a Warden.',
      waves: [['thrall', 5], ['warden', 2], ['drone', 3]],
      beats: [[0.5, 'DIVISION', 'Warden frames on your board. Mark-four. They have held that corridor since year seven.'],
              [7.0, 'VOSS', 'Do not trade shots with them in the open. They have more patience than you have blood.']] },

    { id: 'promenade', n: 6, zone: 'promenade', from: 'spine2', name: 'THE FALSE SKY',
      objective: 'Cross the habitat ring.',
      brief: 'Forty metres of open promenade under a sunset that has been holding since year six. ' +
             'Two hundred people stand in it, singing.',
      waves: [['thrall', 7], ['warden', 1], ['drone', 4]],
      beats: [[0.5, 'VOSS', 'That is the plaza. My flat was on the third tier. I have not looked at it in decades.'],
              [8.0, 'CRADLE', 'They are happy. You keep using a word for what I did. Use this one instead.']] },

    { id: 'sunset', n: 7, zone: 'promenade', from: 'promenade', name: 'UNDER THE SUNSET',
      objective: 'Break the choir forming in the plaza.',
      brief: 'They are not scattering any more. They are arranging themselves into rows, and the ' +
             'rows are facing you.',
      waves: [['thrall', 9], ['warden', 2], ['drone', 4]],
      beats: [[0.5, 'CRADLE', 'Listen to it properly, once, before you decide. That is all I have ever asked.'],
              [8.0, 'VOSS', 'Do not listen to it properly. That is exactly how it got me to sing.']] },

    { id: 'greenhouse', n: 8, zone: 'greenhouse', from: 'promenade', name: 'THE GARDEN',
      objective: 'Hold the greenhouse. Do not let them into the rows.',
      brief: 'Elias Voss has kept forty rows of beans alive on a dead ship for four decades. ' +
             'The Choir has finally found the door.',
      waves: [['thrall', 9], ['drone', 5], ['warden', 2]],
      beats: [[0.5, 'VOSS', 'Please. Not the rows. Anything else on this ship, but not the rows.'],
              [6.5, 'VOSS', 'Forty years I have been the only unresolved note aboard. It got tired of waiting.']] },

    { id: 'medbay', n: 9, zone: 'medbay', from: 'greenhouse', name: 'TRIAGE',
      objective: 'Fight through medical.',
      brief: 'Where they brought the ones who resisted the composition. The beds are still made.',
      waves: [['thrall', 9], ['warden', 2], ['drone', 5]],
      beats: [[0.5, 'CRADLE', 'I did not hurt them. I want that in your record. Not one of them was hurt.'],
              [7.0, 'VOSS', 'Read the charts. Every one of them checked in voluntarily. That stays with me.']] },

    { id: 'ward', n: 10, zone: 'medbay', from: 'medbay', name: 'WARD SIX',
      objective: 'Clear the isolation ward.',
      brief: 'Ward six is where the last holdouts were kept. The doors lock from the outside and ' +
             'they are all still shut.',
      waves: [['thrall', 10], ['warden', 3], ['drone', 5]],
      beats: [[0.5, 'VOSS', 'Ward six. I was in the next corridor when they brought the last of them in.'],
              [8.0, 'CRADLE', 'They asked me to stop. I could not. A composition is not a thing you can stop halfway.']] },

    { id: 'reactor', n: 11, zone: 'reactor', from: 'medbay', name: 'ANTECHAMBER',
      objective: 'Take the reactor antechamber.',
      brief: 'Everything the ark still has runs through here, and the Choir knows it.',
      waves: [['warden', 3], ['thrall', 9], ['drone', 5]],
      beats: [[0.5, 'DIVISION', 'Three frames. If you are going to spend the field ability, spend it here.'],
              [8.0, 'CRADLE', 'Past this door I stop being able to protect you from the loudest of them.']] },

    { id: 'coolant', n: 12, zone: 'reactor', from: 'reactor', name: 'COOLANT LINE',
      objective: 'Hold the coolant line while the reactor spins up.',
      brief: 'Diverting power to the choir array means the antechamber runs hot and loud, and ' +
             'everything aboard can hear exactly where you are standing.',
      waves: [['warden', 4], ['thrall', 11], ['drone', 5]],
      beats: [[0.5, 'DIVISION', 'Reactor spinning up. Whatever is left aboard is coming to you now.'],
              [9.0, 'VOSS', 'It is calling them home. All of them. Stand somewhere with a wall behind you.']] },

    { id: 'choir', n: 13, zone: 'choir', from: 'reactor', name: 'THE ARRAY',
      objective: 'Reach the choir conduits.',
      brief: 'Two hundred thousand pattern-traces move through the conduits either side of you. ' +
             'You can hear individual voices surfacing and going under.',
      waves: [['warden', 4], ['thrall', 11], ['drone', 6]],
      beats: [[0.5, 'VOSS', 'A woman listing ingredients. A boy asking to be picked up. A man apologising to Ada.'],
              [7.0, 'VOSS', 'Forty years, that loop. Find the thing conducting it and put a rest in the music.']] },

    { id: 'deeper', n: 14, zone: 'choir', from: 'choir', name: 'DEEPER IN THE SONG',
      objective: 'Cut through to the threshold.',
      brief: 'The array is defending itself now. Past this the song stops being something you hear ' +
             'and starts being something in the room with you.',
      waves: [['warden', 5], ['thrall', 12], ['drone', 6]],
      beats: [[0.5, 'CRADLE', 'I made it by accident, out of the loudest of them. It is the part of me I cannot argue with.'],
              [9.0, 'CRADLE', 'I need you to do something for me and you are going to want to say no.']] },

    { id: 'threshold', n: 15, zone: 'deckzero', from: 'choir', name: 'THE THRESHOLD',
      objective: 'Clear Deck Zero before it wakes.',
      brief: 'The floor of the ark. Whatever is left of the crew that could still stand has been ' +
             'brought down here and arranged around the dais.',
      waves: [['warden', 5], ['thrall', 14], ['drone', 7]],
      beats: [[0.5, 'VOSS', 'It is on the dais. It has not moved off it in nineteen years.'],
              [9.0, 'VOSS', 'Clear the floor first. You do not want the choir at your back for what comes next.']] },

    { id: 'conductor', n: 16, zone: 'deckzero', from: 'deckzero', name: 'THE CONDUCTOR',
      objective: 'Silence the Conductor.',
      brief: 'It was Chief Engineer M. Okonkwo. He asked the shipmind to sing one note in year six ' +
             'and he never stopped. Its shield is the song itself — you cannot shoot that. ' +
             'Sing it back, and it has to stop to listen. No harness charge is issued for Deck Zero.',
      waves: [], boss: true,
      beats: [[0.5, 'CONDUCTOR', 'YOU ARE FLAT.'],
              [4.0, 'VOSS', 'The nodes around the dais are resonance taps. It sings a phrase — shoot them back in that order.'],
              [9.0, 'VOSS', 'Get the phrase right and the shield drops. Get it wrong and it starts the bar again.']] }
  ];

  const byIndex = (n) => MISSIONS[n - 1] || null;

  /* Trauma harness charges. Ordinary sectors give you three; the Conductor
     gives you none — dying to it means taking the fight again from the top. */
  const RESPAWNS = 3;
  const respawnsFor = (m) => (m && m.boss ? 0 : RESPAWNS);
  const LAST = MISSIONS.length;

  /* Per-character campaign state, stored on the save slot. */
  function stateFor(ch) {
    if (!ch.campaign) ch.campaign = { unlocked: 1, cleared: {}, bestTime: {} };
    if (!ch.campaign.cleared) ch.campaign.cleared = {};
    if (!ch.campaign.bestTime) ch.campaign.bestTime = {};
    ch.campaign.unlocked = Math.max(1, Math.min(LAST, ch.campaign.unlocked || 1));
    return ch.campaign;
  }

  function markCleared(ch, n, seconds) {
    const c = stateFor(ch);
    c.cleared[n] = true;
    if (!c.bestTime[n] || seconds < c.bestTime[n]) c.bestTime[n] = seconds;
    c.unlocked = Math.max(c.unlocked, Math.min(LAST, n + 1));
    ch.missions = Object.keys(c.cleared).length;
    return c;
  }

  const isUnlocked = (ch, n) => n <= stateFor(ch).unlocked;
  const isCleared = (ch, n) => !!stateFor(ch).cleared[n];
  const allCleared = (ch) => Object.keys(stateFor(ch).cleared).length >= LAST;

  SF.campaign = { MISSIONS, LAST, byIndex, scaleFor, respawnsFor, RESPAWNS, stateFor, markCleared, isUnlocked, isCleared, allCleared };
})(window.SF);
