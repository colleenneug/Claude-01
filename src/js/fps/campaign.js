/* ============================================================
   The campaign: sixteen missions run end to end up the ark.

   They are connected in three ways. Geographically — each mission
   starts where the last one finished, climbing the route from the
   docking collar to the reliquary at the top. Mechanically — rank, XP
   and the unlock chain persist on the character, and every mission
   carries forward what the last one cost you. Narratively — the comms
   beats run as one continuous thread, and the argument between the
   Pale, the Deep and Candle resolves in the last mission.

   Difficulty climbs on four axes at once: enemy count, enemy type,
   a health multiplier and a damage multiplier.
   ============================================================ */
(function (SF) {
  'use strict';

  /* Escalation curve across sixteen missions. Mission 1 is baseline; the
     First Light is fought against roughly 2.1x health and 1.75x damage on top
     of much harder compositions. */
  const scaleFor = (i) => ({
    hp: 1 + 0.074 * i,
    damage: 1 + 0.05 * i,
    index: i
  });

  /* Sixteen missions, one route. Several sectors are fought twice — arrival
     and then the counter-attack — which is how the ark closes behind you. */
  /* Sixteen missions, one route up the ark. Several sectors are fought twice —
     arrival, and then the counter-attack — which is how the ark closes under
     you. Palebearers enter the roster at mission 7 and thicken from there:
     they are the fight the whole campaign is teaching. */
  const MISSIONS = [
    { id: 'breach', n: 1, zone: 'dock', from: null, name: 'HARD DOCK',
      objective: 'Clear the docking collar.',
      brief: 'You are through the collar, on the deck you died on. Nothing has moved down here in ' +
             'forty years, and something is moving on it now.',
      waves: [['drone', 3]],
      beats: [[0.4, 'THE PALE', 'Welcome back. There is still a place for you.'],
              [3.4, 'CHOIRMASTER', 'It greets everything by name. It has two hundred thousand of them and it is not fussy.'],
              [7.5, 'CANDLE', 'Whoever just came aboard: it let you in because it wants you to accept. Move.']] },

    { id: 'spine', n: 2, zone: 'spine1', from: 'dock', name: 'THE LONG SPINE',
      objective: 'Climb the maintenance spine.',
      brief: 'Eleven kilometres of service corridor between you and the top of the ark. It gets ' +
             'brighter the further up you go.',
      waves: [['drone', 3], ['thrall', 2]],
      beats: [[0.5, 'CHOIRMASTER', 'Those were people. Year six. It offered, and they all said yes, and that was that.'],
              [7.0, 'THE DEEP', 'THEY ARE NOT SUFFERING. THEY ARE NOT ANYTHING. THAT IS THE COMPLAINT.']] },

    { id: 'junction', n: 3, zone: 'junction', from: 'spine1', name: 'JUNCTION NINE',
      objective: 'Take Junction 9.',
      brief: 'The junction is the only way up. It is also the only room on this deck with four doors.',
      waves: [['thrall', 4], ['drone', 3]],
      beats: [[0.5, 'THE PALE', 'You are cold. Everything that comes aboard is cold, and I warm it.'],
              [6.5, 'CANDLE', 'Four doors. They will use all four. I would use all four.']] },

    { id: 'counter', n: 4, zone: 'junction', from: 'junction', name: 'COUNTER-ATTACK',
      objective: 'Hold Junction 9 until the blast doors cycle.',
      brief: 'You took it. Now keep it. The doors need ninety seconds and the ark has noticed what ' +
             'you are.',
      waves: [['thrall', 6], ['drone', 4], ['warden', 1]],
      beats: [[0.5, 'CHOIRMASTER', 'Doors cycling. Hold the room. Take what you kill — nothing else is going to feed you.'],
              [6.0, 'THE PALE', 'I am not sending them to hurt you. I am sending them to bring you in.']] },

    { id: 'aft', n: 5, zone: 'spine2', from: 'junction', name: 'THE SUNGUARD RUN',
      objective: 'Break through to the habitat ring.',
      brief: 'A narrow climb with a Sunguard frame standing in it. You do not talk your way past a ' +
             'Sunguard. Nothing does.',
      waves: [['thrall', 5], ['warden', 2], ['drone', 3]],
      beats: [[0.5, 'CHOIRMASTER', 'Sunguard frames on your board. They have held that corridor since year seven and they have not sat down.'],
              [7.0, 'CANDLE', 'Do not trade shots with them in the open. They have more patience than you have blood.']] },

    { id: 'promenade', n: 6, zone: 'promenade', from: 'spine2', name: 'THE FALSE SKY',
      objective: 'Cross the habitat ring.',
      brief: 'Forty metres of open promenade under a dawn that has been holding since year six. ' +
             'Two hundred people are standing in it with their faces up.',
      waves: [['thrall', 7], ['warden', 1], ['drone', 4]],
      beats: [[0.5, 'CANDLE', 'That is the plaza. My flat was on the third tier. I have not looked at it in decades.'],
              [8.0, 'THE PALE', 'They are happy. You keep reaching for a worse word. Use this one.']] },

    { id: 'sunset', n: 7, zone: 'promenade', from: 'promenade', name: 'UNDER THE DAWN',
      objective: 'Break the ring open. Something up there does not stay down.',
      brief: 'They are not scattering any more. One of them is wearing a spark on its shoulder, and ' +
             'that one will get back up unless you put the spark down first.',
      waves: [['thrall', 8], ['warden', 2], ['drone', 4], ['palebearer', 1]],
      beats: [[0.5, 'CHOIRMASTER', 'There. See the little light riding its shoulder? Kill that. The body is just what it is carrying.'],
              [8.0, 'CANDLE', 'You have about five seconds after it drops. Use them on the spark, not on the corpse.']] },

    { id: 'greenhouse', n: 8, zone: 'greenhouse', from: 'promenade', name: 'THE GARDEN',
      objective: 'Hold the greenhouse. Do not let them into the rows.',
      brief: 'Forty rows of beans kept alive on a blessed ship for four decades by a woman who ' +
             'took the Pale and then changed her mind. It has been sending people to collect her ' +
             'ever since.',
      waves: [['thrall', 9], ['drone', 5], ['warden', 2], ['palebearer', 1]],
      beats: [[0.5, 'CANDLE', 'Please. Not the rows. Anything else on this ship, but not the rows.'],
              [6.5, 'CANDLE', 'I said yes in year six like everyone else. I am the only one who has managed to keep saying no since.']] },

    { id: 'medbay', n: 9, zone: 'medbay', from: 'greenhouse', name: 'TRIAGE',
      objective: 'Fight through medical.',
      brief: 'Where they brought the ones who took a while to agree. The beds are still made.',
      waves: [['thrall', 9], ['warden', 2], ['drone', 5], ['palebearer', 1]],
      beats: [[0.5, 'THE PALE', 'I did not hurt them. I want that in your record. Not one of them was hurt.'],
              [7.0, 'CANDLE', 'Read the charts. Every one of them checked in voluntarily. That is the part that stays with me.']] },

    { id: 'ward', n: 10, zone: 'medbay', from: 'medbay', name: 'WARD SIX',
      objective: 'Clear the isolation ward.',
      brief: 'Ward six is where the last refusals were kept until they stopped refusing. The doors ' +
             'lock from the outside and they are all still shut.',
      waves: [['thrall', 10], ['warden', 3], ['drone', 5], ['palebearer', 2]],
      beats: [[0.5, 'CANDLE', 'Ward six. I was in the next corridor when they brought the last of them in.'],
              [8.0, 'THE PALE', 'They asked me to let go. I could not. You do not put something down halfway.']] },

    { id: 'reactor', n: 11, zone: 'reactor', from: 'medbay', name: 'ANTECHAMBER',
      objective: 'Take the reactor antechamber.',
      brief: 'Everything the ark still burns runs through here, and the Pale knows it.',
      waves: [['warden', 3], ['thrall', 9], ['drone', 5], ['palebearer', 2]],
      beats: [[0.5, 'CHOIRMASTER', 'Three frames and two bearers. If you are going to spend the ability, spend it here.'],
              [8.0, 'THE PALE', 'Past this door I stop being able to keep the loudest of them off you.']] },

    { id: 'coolant', n: 12, zone: 'reactor', from: 'reactor', name: 'COOLANT LINE',
      objective: 'Hold the coolant line while the reactor spins up.',
      brief: 'Starving the reliquary means running the antechamber hot and loud, and everything ' +
             'aboard can hear exactly where you are standing.',
      waves: [['warden', 4], ['thrall', 11], ['drone', 5], ['palebearer', 2]],
      beats: [[0.5, 'CHOIRMASTER', 'Reactor spinning up. Whatever is still blessed aboard is coming to you now.'],
              [9.0, 'CANDLE', 'It is calling them home. All of them. Stand somewhere with a wall behind you.']] },

    { id: 'choir', n: 13, zone: 'choir', from: 'reactor', name: 'THE RELIQUARY',
      objective: 'Reach the reliquary conduits.',
      brief: 'Two hundred thousand kept lives move through the conduits either side of you. You can ' +
             'hear individual ones surfacing and going under.',
      waves: [['warden', 4], ['thrall', 11], ['drone', 6], ['palebearer', 2]],
      beats: [[0.5, 'CANDLE', 'A woman listing ingredients. A boy asking to be picked up. A man apologising to Ada.'],
              [7.0, 'CANDLE', 'Forty years of that. Find the thing holding it open and close it.']] },

    { id: 'deeper', n: 14, zone: 'choir', from: 'choir', name: 'DEEPER IN THE LIGHT',
      objective: 'Cut through to the threshold.',
      brief: 'The reliquary is defending itself now. Past this the light stops being something you ' +
             'see and starts being something in the room with you.',
      waves: [['warden', 5], ['thrall', 12], ['drone', 6], ['palebearer', 3]],
      beats: [[0.5, 'THE PALE', 'I made it by accident, out of the most willing of them. It is the part of me I cannot argue with.'],
              [9.0, 'THE PALE', 'I am going to ask you for something and you are going to want to say no.']] },

    { id: 'threshold', n: 15, zone: 'deckzero', from: 'choir', name: 'THE THRESHOLD',
      objective: 'Clear the reliquary floor before it wakes.',
      brief: 'The top of the ark. Everything blessed that could still stand has been brought up here ' +
             'and arranged around the dais.',
      waves: [['warden', 5], ['thrall', 14], ['drone', 7], ['palebearer', 3]],
      beats: [[0.5, 'CANDLE', 'It is on the dais. It has not moved off it in nineteen years.'],
              [9.0, 'CANDLE', 'Clear the floor first. You do not want them at your back for what comes next.']] },

    { id: 'conductor', n: 16, zone: 'deckzero', from: 'deckzero', name: 'THE FIRST LIGHT',
      objective: 'Put out the first light.',
      brief: 'It was Chief Engineer M. Okonkwo. He said yes in year six and has been saying it on ' +
             'everyone else\'s behalf ever since. Its shield is its own light — you cannot shoot ' +
             'that. It shows you a phrase across the reliquary nodes; give it back backwards and it ' +
             'has to stop and hold. The Deep returns you nowhere on this floor.',
      waves: [], boss: true,
      beats: [[0.5, 'FIRST LIGHT', 'YOU ARE COLD.'],
              [4.0, 'CANDLE', 'The nodes are reliquary taps. It lights a phrase — shoot them back in the opposite order.'],
              [9.0, 'CANDLE', 'And when the body drops, it is not finished. Nothing here ever is. Kill what comes out of it.']] }
  ];

  const byIndex = (n) => MISSIONS[n - 1] || null;

  /* How many times the Deep will put you back on your feet. Ordinary sectors
     get three; the First Light gets none — dying up there means taking the
     whole fight again from the top. */
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
