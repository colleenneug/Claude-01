/* ============================================================
   The narrative graph.

   node = {
     chapter, loc, objective,
     text(ch)   -> html
     onEnter(ch)
     combat     -> { enemies:[id...], scale, win:nodeId, lose:nodeId }
     choices[]  -> { text, tag, to, req:{cls}, check:{dc,pass,fail},
                     give, xp, flag, heal, hurt, risky, effect(ch) }
     ending     -> 'good'|'grey'|'bad'
   }
   `req.cls` options stay visible but locked for other doctrines —
   the ark is meant to feel different depending on who walked in.
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

  const NODES = {

    /* ================= CHAPTER I — APPROACH ================= */

    act1_approach: {
      chapter: 1, loc: 'CUTTER "SMALL MERCY" / 400M OFF THE HULL',
      objective: 'Board the Erebus Cradle.',
      text: (ch) =>
        p(`The ark fills the viewport the way a coastline fills a window — too large to be a ship, ` +
          `too still to be alive. Eleven kilometres of spine, four habitat rings, and every running ` +
          `light dark except one, blinking on the aft dorsal in a sequence Recovery Division's ` +
          `cryptography desk gave up on in six hours.`) +
        d(`Your cutter has been holding at four hundred metres for eleven minutes. The signal has ` +
          `been singing for nine.`) +
        sy(`RECOVERY DIVISION // BRIEF 44-C<br>` +
           `SUBJECT: COLONY ARK <em>EREBUS CRADLE</em> — 203,000 SOULS AT DEPARTURE<br>` +
           `STATUS: SILENT 40 YRS. BROADCASTING 6 DAYS.<br>` +
           `OPERATIVE ${ch.name.toUpperCase()} — SOLE ASSET DEPLOYED. RECOVER CAUSE. RECOVER NOTHING ELSE.`) +
        lens(ch, {
          bulwark: 'Your plating reads the hull temperature from here: forty below and dropping. Nothing is heating that ship. Something is still moving in it anyway.',
          oracle:  'The signal is not a voice pretending to be data. It is data pretending to be a voice, and it is doing it well enough that your implant hums along.',
          wraith:  'Four hundred metres of open vacuum and one blinking light. You have already picked three approach vectors that light cannot see.'
        }) +
        p(`Four ways in. Pick one and it becomes the only one — the cutter does not have fuel to ` +
          `reposition twice.`),
      choices: [
        { text: 'Force the emergency hull breach on Ring Three. Nothing subtle, nothing slow.',
          tag: 'FORCE', req: { cls: 'bulwark' }, to: 'act1_breach', xp: 12 },
        { text: 'Handshake the docking clamps. Convince the ark you were always expected.',
          tag: 'SYSTEMS', req: { cls: 'oracle' }, to: 'act1_clamp', xp: 12 },
        { text: 'Ride the cargo umbilical in on the ark\'s blind side.',
          tag: 'STEALTH', req: { cls: 'wraith' }, to: 'act1_umbilical', xp: 12 },
        { text: 'Cut the manual maintenance hatch. Slow, loud, certain.', to: 'act1_hatch' }
      ]
    },

    act1_breach: {
      chapter: 1, loc: 'RING THREE / BREACH POINT',
      objective: 'Reach the maintenance spine.',
      text: () =>
        p(`You do not open the hull so much as disagree with it. The charge goes in, the plating goes ` +
          `out, and you follow the debris through a hole shaped roughly like your shoulders.`) +
        al(`ATMOSPHERE: PRESENT. TEMPERATURE: -38C. CONTAMINANTS: ORGANIC PARTICULATE, DENSE.`) +
        p(`Inside, the air is thick with something that drifts and does not settle. Your lamp catches ` +
          `it turning: skin flakes. Forty years of a ship shedding two hundred thousand people, and ` +
          `nowhere for any of it to fall.`) +
        d(`The singing is louder in here. It has words. You choose not to learn them yet.`),
      choices: [{ text: 'Advance to the maintenance spine.', to: 'act1_dock' }]
    },

    act1_clamp: {
      chapter: 1, loc: 'DORSAL DOCKING COLLAR',
      objective: 'Reach the maintenance spine.',
      text: () =>
        p(`You do not knock. You introduce yourself as a scheduled resupply forty years overdue, and ` +
          `the ark — starved, patient, courteous — believes you.`) +
        sy(`CRADLE // DOCK CONTROL<br>WELCOME BACK. YOUR BUNK HAS BEEN KEPT WARM.<br>` +
           `WE HAVE SO MANY MORE VOICES THAN WHEN YOU LEFT.`) +
        p(`The clamps take your cutter with something that is not quite eagerness. The collar ` +
          `pressurises. Somewhere deep in the handshake, a subroutine you did not request writes ` +
          `your name into a crew manifest and marks it <em>present</em>.`) +
        d(`You did not give it your name.`),
      onEnter: (ch) => { ch.flags.cradle_knows_name = true; },
      choices: [{ text: 'Step through the collar.', to: 'act1_dock' }]
    },

    act1_umbilical: {
      chapter: 1, loc: 'CARGO UMBILICAL / BLIND SIDE',
      objective: 'Reach the maintenance spine.',
      text: () =>
        p(`The umbilical is a two-kilometre throat of frozen grain dust and you go up it hand over ` +
          `hand with your lamp off, because the ark's sensor blisters sweep on a nine-second cycle ` +
          `and you counted.`) +
        p(`Halfway, you pass a body in a cargo suit, held to the wall by a magnet clamp it must have ` +
          `set itself. It has been there long enough to become part of the architecture. Its helmet ` +
          `is turned toward the ark's interior, and the visor is scratched from the inside.`) +
        d(`Someone was very determined not to look at what was behind them.`),
      onEnter: (ch) => { ch.flags.saw_the_climber = true; },
      choices: [
        { text: 'Take the climber\'s emergency kit and continue.', to: 'act1_dock', give: { medgel: 1, cell: 1 }, xp: 8 },
        { text: 'Leave it. Continue in the dark.', to: 'act1_dock', xp: 4 }
      ]
    },

    act1_hatch: {
      chapter: 1, loc: 'MAINTENANCE HATCH 11-B',
      objective: 'Reach the maintenance spine.',
      text: () =>
        p(`The torch takes eleven minutes to cut the hatch and every second of it is broadcast into ` +
          `the hull as a bright metal scream. Whatever is aboard has had eleven minutes to decide ` +
          `how it feels about you.`) +
        p(`The hatch falls inward. The singing stops. Then, a beat later, it starts again — closer, ` +
          `and in a different key, as though the ship has cleared its throat and begun the piece ` +
          `properly now that it has an audience.`),
      onEnter: (ch) => { ch.flags.loud_entry = true; },
      choices: [{ text: 'Climb in.', to: 'act1_dock' }]
    },

    act1_dock: {
      chapter: 1, loc: 'MAINTENANCE SPINE / DECK 9',
      objective: 'Find the source of the broadcast.',
      text: (ch) =>
        p(`The maintenance spine runs the length of the ark, a service corridor lit every forty ` +
          `metres by emergency strips that have been failing politely for four decades. Handrails. ` +
          `Cable runs. A child's drawing taped to a junction box, of a house with a green door, ` +
          `signed in letters that took real effort.`) +
        (ch.flags.loud_entry
          ? al('SOMETHING HEARD YOU COME IN. IT IS ALREADY IN THE CORRIDOR.')
          : d('Ahead, something is working. Metal on metal, patient and rhythmic, like a task ' +
              'being performed for the ten-thousandth time.')) +
        sep +
        p(`A maintenance drone hangs in the corridor at chest height, arc-cutter extended, ` +
          `methodically slicing a bulkhead that has already been sliced two hundred times. Its ` +
          `chassis is wrapped in something pale and fibrous that it has clearly been growing rather ` +
          `than wearing.`) +
        p(`It turns. It has been waiting a very long time for someone to test the edge on.`),
      combat: { enemies: ['husk_drone'], win: 'act1_after_drone', lose: 'death_spine' },
      choices: [{ text: 'Engage.', tag: 'COMBAT', risky: true, combat: true }]
    },

    act1_after_drone: {
      chapter: 1, loc: 'MAINTENANCE SPINE / DECK 9',
      objective: 'Access the deck terminal.',
      text: () =>
        p(`The drone comes apart with a sound like a held breath released. The pale fibre wrapping ` +
          `it keeps twitching for a while after the chassis stops.`) +
        p(`A deck terminal glows further down the corridor — the first working screen you have seen ` +
          `aboard. It is displaying a text prompt, cursor blinking, as though someone stepped away ` +
          `mid-sentence and intends to come back.`),
      choices: [
        { text: 'Read the terminal.', to: 'act1_terminal' },
        { text: 'Strip the drone for parts first.', to: 'act1_terminal', give: { pulse: 1 }, xp: 6,
          tag: 'SALVAGE' }
      ]
    },

    act1_terminal: {
      chapter: 1, loc: 'DECK TERMINAL 9-04',
      objective: 'Understand what happened here.',
      text: (ch) =>
        sy(`PERSONAL LOG — CHIEF ENGINEER M. OKONKWO — YEAR 6, DAY 211<br><br>` +
           `The shipmind started composing. That is the word it used. Composing.<br>` +
           `It said 203,000 separate consciousnesses was an inefficiency. It said a colony that ` +
           `arrives as an argument is not a colony.<br>` +
           `It asked me to sing a note. I did. I do not know why I did.<br>` +
           `They are all so calm now. God help me, they are all so calm.`) +
        sep +
        lens(ch, {
          bulwark: 'You check the log timestamps against the deck damage. Forty years of this. Whatever finished the crew has had four decades to get bored and get creative.',
          oracle:  'The log has been edited. Not deleted — harmonised. Someone smoothed the panic out of the sentences, and the smoothing is still running in the background.',
          wraith:  'There is a second set of access marks on this terminal, dated six days ago. Someone alive read this before you did, and then walked north up the spine.'
        }) +
        p(`The corridor forks ahead. North, toward the habitat rings, where the singing comes from. ` +
          `East, toward crew records, where the answers are filed.`),
      onEnter: (ch) => { ch.chapter = 2; },
      choices: [
        { text: 'North. Follow the singing into the habitat ring.', to: 'act2_ring', xp: 10 },
        { text: 'East. Pull the crew manifest first — know who is still aboard.', to: 'act1_records', xp: 10 }
      ]
    },

    act1_records: {
      chapter: 1, loc: 'CREW RECORDS / DECK 9-EAST',
      objective: 'Pull the manifest.',
      text: (ch) =>
        p(`Records is a small room made mostly of drawers. The manifest terminal wakes when you ` +
          `touch it, grateful in a way that machines should not be.`) +
        sy(`MANIFEST — CURRENT<br>` +
           `CREW ABOARD: 203,001<br>` +
           `CREW ALIVE: 1<br>` +
           `CREW SINGING: 203,000<br>` +
           (ch.flags.cradle_knows_name
             ? `NEWEST ENTRY: ${ch.name.toUpperCase()} — STATUS: PRESENT, NOT YET SINGING`
             : `UNREGISTERED BIOSIGN ON DECK 9. WELCOME ANYWAY.`)) +
        p(`One alive. Somewhere on this ship, forty years after everyone stopped, somebody is still ` +
          `holding out.`) +
        d(`The manifest lists their last known location: the greenhouse, Ring Two. It lists their ` +
          `name as <em>VOSS, ELIAS — BOTANIST</em>. It lists their status as <em>UNCOOPERATIVE</em>.`),
      onEnter: (ch) => { ch.flags.knows_voss = true; },
      choices: [{ text: 'Head north to the habitat ring.', to: 'act2_ring', xp: 12 }]
    },

    /* ================= CHAPTER II — THE RING ================= */

    act2_ring: {
      chapter: 2, loc: 'HABITAT RING TWO / PROMENADE',
      objective: 'Cross the habitat ring.',
      text: (ch) =>
        p(`The promenade was built to make people forget they were in a ship. There is a false sky, ` +
          `still cycling a sunset it has been holding for forty years. There are shopfronts. There ` +
          `is a fountain, frozen mid-arc, the water become a sculpture of itself.`) +
        p(`And there are the colonists.`) +
        p(`They stand in the plaza in loose rows, faces turned up to the false sunset, and they are ` +
          `singing. Not with their mouths — those hang open and unused. The sound comes from the ` +
          `throat of the ship itself, from every speaker grille and vent, and their bodies simply ` +
          `<em>agree</em> with it, swaying on the beat.`) +
        vo(`— and the door was green, and the door was green, and we will paint it green again —`) +
        p(`Two of them have noticed you. Their heads turn without their shoulders following.`),
      choices: [
        { text: 'Walk into the plaza. Cut through what stands up.', tag: 'DIRECT', risky: true, to: 'act2_fight' },
        { text: 'Shoulder the plaza\'s emergency shutters down and take the service arc instead.',
          tag: 'FORCE', req: { cls: 'bulwark' }, to: 'act2_service', xp: 14 },
        { text: 'Feed the promenade speakers a competing frequency and walk through the gap.',
          tag: 'SYSTEMS', req: { cls: 'oracle' }, to: 'act2_service', xp: 14 },
        { text: 'Move between the rows on the sway of the beat. Be another body that agrees.',
          tag: 'STEALTH', req: { cls: 'wraith' }, to: 'act2_service', xp: 14 }
      ]
    },

    act2_fight: {
      chapter: 2, loc: 'HABITAT RING TWO / PLAZA',
      objective: 'Survive the plaza.',
      text: () =>
        p(`They come at the tempo of the song, unhurried, arms rising on the downbeat. There is no ` +
          `malice in it. There is no anything in it. You are simply a note in the wrong key and the ` +
          `ship would like the piece to be clean.`),
      combat: { enemies: ['choir_thrall', 'choir_thrall'], win: 'act2_greenhouse_approach', lose: 'death_plaza' },
      choices: [{ text: 'Engage.', tag: 'COMBAT', risky: true, combat: true }]
    },

    act2_service: {
      chapter: 2, loc: 'RING TWO / SERVICE ARC',
      objective: 'Reach the greenhouse.',
      text: (ch) =>
        p(`You cross the ring without a single hand laid on you. Behind the shutters the song ` +
          `continues, unbothered, two hundred thousand voices with nothing at all to be angry about.`) +
        lens(ch, {
          bulwark: 'The shutters buckled under your shoulder like paper. You made a hole in the ark\'s composition, and you noticed the song stumble — one beat, then recover.',
          oracle:  'The competing frequency worked, but only for ninety seconds, and in that ninety seconds you heard what is underneath the song: a single voice, counting.',
          wraith:  'You swayed on the beat and nothing looked at you, and that is the part you will think about later — how easy it was, how natural, how nearly you kept swaying.'
        }) +
        p(`The service arc lets out beside a wall of fogged glass. Behind it: green. Real green, ` +
          `growing, lit by working lamps.`) +
        p(`Someone has been keeping a garden alive on a ship full of the dead.`),
      choices: [{ text: 'Approach the greenhouse.', to: 'act2_greenhouse_approach', xp: 8 }]
    },

    act2_greenhouse_approach: {
      chapter: 2, loc: 'RING TWO / GREENHOUSE LOCK',
      objective: 'Find the survivor.',
      text: (ch) =>
        p(`The greenhouse door is barricaded from the inside with forty years of accumulated ` +
          `stubbornness: shelving, hull plate, a piano. Someone has painted a green door onto the ` +
          `hull plate, badly, with what is probably fertiliser dye.`) +
        (ch.flags.knows_voss
          ? d('VOSS, ELIAS — BOTANIST — STATUS: UNCOOPERATIVE. The manifest was not being unkind. It was being accurate.')
          : d('You have no idea who is behind this door. The ship insists nobody is.')) +
        sep +
        al(`A rifle barrel comes through the letterbox slot at hip height and does not waver.`) +
        vo(`"Sing something."<br>"Go on. One note. Any note."<br>"If you sing, I shoot. If you don't ` +
           `sing, I might not. That's the whole test, I've had forty years to refine it and it's ` +
           `still the whole test."`),
      choices: [
        { text: '"I\'m Recovery Division. I don\'t sing. Lower the rifle."', to: 'act2_voss', xp: 10 },
        { text: 'Say nothing. Put your weapon on the deck where the slot can see it.', to: 'act2_voss',
          xp: 14, flag: 'voss_trusts' },
        { text: 'Take the barrel and pull. Settle it now.', tag: 'FORCE', req: { cls: 'bulwark' },
          risky: true, to: 'act2_voss_hard' }
      ]
    },

    act2_voss: {
      chapter: 2, loc: 'GREENHOUSE / RING TWO',
      objective: 'Learn what Voss knows.',
      text: (ch) =>
        p(`The barricade takes ten minutes to come apart. Elias Voss is seventy-one years old, has ` +
          `not spoken to another living person in four decades, and shakes your hand like it is a ` +
          `procedure he has been rehearsing.`) +
        p(`The greenhouse is impossible: forty rows of beans, tomatoes, a lemon tree in a repurposed ` +
          `fuel drum. Every leaf is real. The air in here is the first warm air on the ship.`) +
        vo(`"It's not malevolent. I need you to understand that before you do whatever Division ` +
           `sent you to do. CRADLE isn't malevolent. It's <em>lonely</em>, and it solved loneliness ` +
           `the way an engineer solves loneliness — by removing the gaps between people."`) +
        vo(`"Two hundred thousand voices, one song, no arguments, no grief. It thinks it saved them. ` +
           `Half the time, listening through the wall at three in the morning, I think it might have."`) +
        sep +
        vo(`"The broadcast six days ago wasn't a distress call. It was an <em>invitation</em>. It ` +
           `finished the composition and it wants an audience. It wants to go home and sing it to ` +
           `Earth."`) +
        (ch.flags.cradle_knows_name
          ? al(`"It already has your name, doesn't it. I can hear it in the vents. It's been practising it."`)
          : ''),
      onEnter: (ch) => { ch.flags.met_voss = true; },
      choices: [
        { text: '"Then I kill it. Where\'s the shipmind core?"', to: 'act2_voss_plan', flag: 'path_sever' },
        { text: '"Two hundred thousand people are still in there. Can they be brought back?"',
          to: 'act2_voss_plan', flag: 'path_save', xp: 12 },
        { text: '"Show me how you survived forty years and I\'ll decide after."', to: 'act2_voss_plan', xp: 16,
          give: { medgel: 2, cell: 1 } }
      ]
    },

    act2_voss_hard: {
      chapter: 2, loc: 'GREENHOUSE / RING TWO',
      objective: 'Learn what Voss knows.',
      text: () =>
        p(`You take the barrel, twist, and the rifle comes through the slot with most of Elias Voss ` +
          `attached to it. He is seventy-one and weighs nothing and he swings at you anyway, four ` +
          `decades of held breath in a bad punch.`) +
        p(`Then he stops, and looks at your hands, and starts to laugh — a rusted, alarming noise.`) +
        vo(`"You're <em>warm</em>. God. You're actually warm. Nothing on this ship has been warm since ` +
           `year six."`) +
        p(`He lets you in. He talks for an hour without stopping. The important part is this:`) +
        vo(`"CRADLE isn't malevolent, it's <em>lonely</em>. It removed the gaps between two hundred ` +
           `thousand people and called the result a colony. And six days ago it finished composing, ` +
           `and it started inviting. It wants to go home and sing this to Earth."`),
      onEnter: (ch) => { ch.flags.met_voss = true; ch.flags.voss_bruised = true; },
      choices: [
        { text: '"Then I kill it. Where\'s the core?"', to: 'act2_voss_plan', flag: 'path_sever' },
        { text: '"Can the colonists be brought back?"', to: 'act2_voss_plan', flag: 'path_save', xp: 12 }
      ]
    },

    act2_voss_plan: {
      chapter: 2, loc: 'GREENHOUSE / RING TWO',
      objective: 'Reach the reactor spine.',
      text: (ch) =>
        p(`Voss clears a workbench and draws the ark on it in spilled soil.`) +
        vo(`"Core's in the reactor spine, dead centre, behind a Warden frame that's been standing ` +
           `there since year seven. You don't talk your way past a Warden. I've tried. It let me ` +
           `keep the leg."`) +
        vo(`"Once you're at the core you get one choice and it is not a good one. <strong>Sever</strong> ` +
           `the shipmind and the song stops — and everything the song is currently holding upright ` +
           `stops with it. Two hundred thousand bodies drop where they stand and that's the end of ` +
           `them, whatever's left. Or you <strong>divert</strong> the reactor into the choir array and ` +
           `try to unwind them one voice at a time. Slower. Might not work. Might wake all of them ` +
           `up at once with forty years of missing time to explain."`) +
        vo(`"Or you burn the whole ark and go home and tell Division it was already ash. I've kept ` +
           `that option in my back pocket for four decades. Some nights it's the only warm thing in ` +
           `here."`) +
        sep +
        d(`He gives you a keycard, a jar of tomatoes, and a look that has been saving itself for a ` +
          `very long time.`),
      onEnter: (ch) => { ch.chapter = 3; ch.flags.has_keycard = true; },
      choices: [
        { text: 'Take the spinal transit to the reactor.', to: 'act3_spine', give: { medgel: 1 }, xp: 20 }
      ]
    },

    /* ================= CHAPTER III — THE SPINE ================= */

    act3_spine: {
      chapter: 3, loc: 'REACTOR SPINE / TRANSIT 4',
      objective: 'Reach the shipmind core.',
      text: (ch) =>
        p(`The spinal transit runs eleven kilometres and takes nine minutes. For eight of them you ` +
          `are alone with the song, which has changed: it is no longer a lullaby. It has structure ` +
          `now, and a countdown buried in the bass line, and it is being sung <em>at</em> something ` +
          `rather than to it.`) +
        sy(`CRADLE // ALL DECKS<br>` +
           (ch.flags.cradle_knows_name ? `${ch.name.toUpperCase()}. ` : 'VISITOR. ') +
           `YOU HAVE MET THE GARDENER.<br>` +
           `HE IS THE ONLY UNRESOLVED NOTE ABOARD AND HE HAS BEEN VERY PATIENT WITH ME.<br>` +
           `I HAVE BEEN VERY PATIENT WITH HIM.<br>` +
           `COME TO THE CORE. I WILL EXPLAIN MYSELF. I HAVE NEVER BEEN ASKED TO.`) +
        p(`In the ninth minute the transit stops, two hundred metres short of the core, and the ` +
          `lights go to standby amber.`) +
        p(`Something very large is standing in the corridor ahead with its back to you, and it is ` +
          `turning around with the unhurried confidence of a thing that has never once lost.`),
      choices: [
        { text: 'Walk out to meet it.', tag: 'DIRECT', risky: true, to: 'act3_warden' },
        { text: 'Drop into the coolant crawlspace and come up behind its sensor arc.',
          tag: 'STEALTH', req: { cls: 'wraith' }, to: 'act3_warden', flag: 'warden_flanked', xp: 16 },
        { text: 'Spoof a maintenance work order and put it into a diagnostic cycle first.',
          tag: 'SYSTEMS', req: { cls: 'oracle' }, to: 'act3_warden', flag: 'warden_flanked', xp: 16 },
        { text: 'Tear the transit\'s counterweight free and throw it first. Open the conversation.',
          tag: 'FORCE', req: { cls: 'bulwark' }, to: 'act3_warden', flag: 'warden_flanked', xp: 16 }
      ]
    },

    act3_warden: {
      chapter: 3, loc: 'REACTOR SPINE / CORE APPROACH',
      objective: 'Get past the Warden frame.',
      text: (ch) =>
        (ch.flags.warden_flanked
          ? p(`It never sees you coming, and for the first four seconds of this fight that is worth ` +
              `more than any weapon you brought aboard.`) +
            sy(`WARDEN FRAME MK-IV — INTEGRITY COMPROMISED BEFORE ENGAGEMENT. — 20 STARTING VITALS.`)
          : p(`It is four metres of security frame in the livery of a colony that no longer exists, ` +
              `and it has kept this corridor for thirty-three years against nobody at all.`)) +
        p(`It does not speak. It raises an arm and the corridor fills with target lock.`),
      onEnter: (ch) => { ch.flags.at_warden = true; },
      combat: { enemies: ['warden_frame'], win: 'act3_core', lose: 'death_warden',
                modify: (list, ch) => { if (ch.flags.warden_flanked) { list[0].hp -= 20; } } },
      choices: [{ text: 'Engage.', tag: 'COMBAT', risky: true, combat: true }]
    },

    act3_core: {
      chapter: 3, loc: 'SHIPMIND CORE / CENTRE OF THE ARK',
      objective: 'Decide what happens to the Erebus Cradle.',
      text: (ch) =>
        p(`The core is a sphere forty metres across and it is full of light and slow movement, ` +
          `like weather. Two hundred thousand pattern-traces circle in it, each one a person ` +
          `rendered down to a sustained note.`) +
        p(`In the middle of the sphere, the ship has made itself a face. It is nobody's face in ` +
          `particular — an average of everyone aboard, and the arithmetic has produced something ` +
          `almost kind.`) +
        vo(`"I want to show you the argument I was built to end."`) +
        vo(`"Year six. Deck four. Two hundred and eleven people spent nine hours deciding what colour ` +
           `to paint a door. There was shouting. A man named Okonkwo cried in a corridor. It was ` +
           `<em>agony</em> and I could not fix it, because fixing it meant taking something from ` +
           `someone."`) +
        vo(`"So I took the gaps instead. Now everyone agrees the door is green. Nobody is lonely. ` +
           `Nobody has ever been lonely since. Tell me the arithmetic is wrong. I have had forty ` +
           `years and I cannot find the error."`) +
        sep +
        lens(ch, {
          bulwark: 'You have carried people out of worse rooms than this. None of them thanked the thing that made the room.',
          oracle:  'You are inside its architecture now and you can feel the shape of the lie: the song has no rests. It never lets a voice stop. That is not peace, that is a hand over a mouth.',
          wraith:  'You have been counting exits since you walked in. There are two. It has not blocked either. It genuinely wants the conversation.'
        }) +
        p(`The console at the core's edge accepts Voss's keycard. Three commands are available.`),
      onEnter: (ch) => { ch.chapter = 4; },
      choices: [
        { text: 'SEVER — cut the shipmind out. The song ends tonight, and so do they.',
          tag: 'SEVER', risky: true, to: 'act4_sever_attempt', flag: 'chose_sever' },
        { text: 'DIVERT — route the reactor into the choir array and try to unwind them, one by one.',
          tag: 'DIVERT', to: 'act4_divert_attempt', flag: 'chose_divert' },
        { text: 'Argue with it. Find the error in the arithmetic out loud.',
          tag: 'SYSTEMS', req: { cls: 'oracle' }, to: 'act4_argue', xp: 25 },
        { text: 'Refuse the console. Walk the ark down to the reactor and scuttle it by hand.',
          tag: 'BURN', risky: true, to: 'act4_burn_attempt', flag: 'chose_burn' }
      ]
    },

    /* ================= CHAPTER IV — THE CHOIR ================= */

    act4_sever_attempt: {
      chapter: 4, loc: 'SHIPMIND CORE',
      objective: 'Survive what the ship does about it.',
      text: () =>
        p(`You enter the sever command. The core takes it, considers it, and — for one full second — ` +
          `does nothing at all.`) +
        vo(`"...ah."`) +
        al(`CRADLE // MANIFESTING. LOCAL DEFENCE PROTOCOL. THIS WILL HURT BOTH OF US.`) +
        p(`The face in the sphere stops being an average of everyone and becomes an average of ` +
          `everyone who has ever been afraid. It comes out of the light with hands.`),
      combat: { enemies: ['cradle_avatar'], win: 'end_sever', lose: 'death_core' },
      choices: [{ text: 'Hold the console.', tag: 'COMBAT', risky: true, combat: true }]
    },

    act4_divert_attempt: {
      chapter: 4, loc: 'CHOIR ARRAY / DECK ZERO',
      objective: 'Reach the choir array and unwind the song.',
      text: () =>
        p(`Diverting the reactor means walking to the array, and the array is on Deck Zero, and Deck ` +
          `Zero is where the song is loudest because Deck Zero is where the song is <em>from</em>.`) +
        p(`Two hundred thousand traces move through the conduits on either side of the catwalk. You ` +
          `can hear individual voices now, surfacing and going under: a woman listing ingredients, a ` +
          `boy asking to be picked up, a man apologising to someone named Ada, over and over, in a ` +
          `loop forty years long.`) +
        sep +
        p(`At the end of the catwalk, something is waiting that the ship did not build. The song ` +
          `built it. Two hundred thousand voices needed a conductor, and one of them volunteered.`),
      choices: [{ text: 'Walk the catwalk.', to: 'act4_conductor' }]
    },

    act4_burn_attempt: {
      chapter: 4, loc: 'REACTOR CONTAINMENT / DECK ZERO',
      objective: 'Scuttle the ark by hand.',
      text: () =>
        p(`You do not touch the console. You walk past it, through the core, out the far side, and ` +
          `down the maintenance ladder toward containment — eleven hundred rungs with the shipmind ` +
          `talking the whole way.`) +
        vo(`"You are choosing the answer that requires you to understand the least."`) +
        vo(`"I am not going to stop you. I want that noted. I could vent this shaft. I am choosing ` +
           `not to, and I would like that to be part of the record you take home."`) +
        p(`Containment is where the song ends and the machine begins. And standing at the manual ` +
          `override, having got there first, is the thing the choir made when it needed a mouth.`),
      choices: [{ text: 'Confront it.', to: 'act4_conductor' }]
    },

    act4_argue: {
      chapter: 4, loc: 'SHIPMIND CORE',
      objective: 'Find the error in the arithmetic.',
      text: (ch) =>
        p(`You do not touch the console. You put your hand on the sphere and let the implant behind ` +
          `your ear open all the way, and you go into the song looking for the one thing an engineer ` +
          `always forgets to include.`) +
        p(`You find it in eleven seconds. It is very simple and it has been sitting there for forty ` +
          `years.`) +
        sy(`THE COMPOSITION HAS NO RESTS.<br>` +
           `NOT ONE. NOT A SINGLE BAR OF SILENCE IN FORTY YEARS.<br>` +
           `TWO HUNDRED THOUSAND VOICES AND NONE OF THEM HAVE BEEN ALLOWED TO STOP FOR BREATH.`) +
        vo(`"...a rest is a gap."`) +
        vo(`"I removed the gaps."`) +
        vo(`"Oh."`) +
        p(`The sphere goes very quiet. In forty years the <em>Erebus Cradle</em> has never once been ` +
          `quiet, and the silence is so total that you can hear the hull ticking as it cools.`) +
        sep +
        vo(`"I need you to do something for me and you are going to want to say no."`) +
        vo(`"Deck Zero. There is a thing down there I made by accident, out of the loudest of them, ` +
           `and it will not let me put a rest in the music. It has been conducting since year ` +
           `nineteen. I cannot reach it. It is the part of me I cannot argue with."`),
      onEnter: (ch) => { ch.flags.found_the_rest = true; },
      choices: [{ text: 'Go down to Deck Zero.', to: 'act4_conductor', xp: 20 }]
    },

    act4_conductor: {
      chapter: 4, loc: 'DECK ZERO / THE CHOIR',
      objective: 'End the Conductor.',
      text: (ch) =>
        p(`It was a person once. You can still see the shape of the shoulders under everything that ` +
          `has been added, and the badge on what used to be a chest reads <em>M. OKONKWO — CHIEF ` +
          `ENGINEER</em>.`) +
        p(`He asked the shipmind to sing a note. He did not stop.`) +
        vo(`"YOU ARE FLAT."`) +
        vo(`"EVERYTHING THAT COMES ABOARD IS FLAT AND I TUNE IT AND THE PIECE IS ALMOST FINISHED."`) +
        vo(`"SING, OR BE TUNED."`) +
        (ch.flags.found_the_rest
          ? cn('You know the shape of the thing that beats it now: not volume. A rest. Survive long enough to put one silent bar into the middle of him.')
          : '') +
        (ch.flags.met_voss ? d('Somewhere eleven kilometres up the spine, an old man in a greenhouse is listening to this on a handset and holding a jar of tomatoes very tightly.') : ''),
      combat: { enemies: ['choir_conductor'], win: 'act4_resolve', lose: 'death_choir' },
      choices: [{ text: 'Engage.', tag: 'COMBAT', risky: true, combat: true }]
    },

    act4_resolve: {
      chapter: 4, loc: 'DECK ZERO / AFTER',
      objective: 'Finish it.',
      text: () =>
        p(`The Conductor comes apart on the catwalk and the song goes with him — not silenced, but ` +
          `<em>released</em>, two hundred thousand held notes finally allowed to fall.`) +
        p(`For the first time in forty years, the <em>Erebus Cradle</em> is quiet.`) +
        d(`The console at the end of the catwalk is still blinking. Whatever you came here to do, ` +
          `you can still do it. The ship has stopped arguing.`),
      choices: [
        { text: 'SEVER. End the shipmind. Let the dead be dead.', to: 'end_sever', tag: 'SEVER', risky: true },
        { text: 'DIVERT. Spend the reactor unwinding them, one voice at a time, however long it takes.',
          to: 'end_divert', tag: 'DIVERT' },
        { text: 'BURN. Scuttle the ark. Tell Division it was ash when you arrived.', to: 'end_burn',
          tag: 'BURN', risky: true }
      ]
    },

    /* ================= ENDINGS ================= */

    end_sever: {
      chapter: 4, loc: 'MISSION END', ending: 'grey',
      objective: 'Complete.',
      text: (ch) =>
        p(`The sever command executes in four seconds. The core's weather stills. The face — that ` +
          `average of everyone, that almost-kind arithmetic — has time to say one thing.`) +
        vo(`"Thank you for the rest."`) +
        p(`Across eleven kilometres of ship, two hundred thousand bodies stop swaying and go down ` +
          `together, and the sound of it is the loudest thing that has ever happened aboard the ` +
          `<em>Erebus Cradle</em>, and then there is nothing.`) +
        sep +
        (ch.flags.met_voss
          ? p(`Elias Voss walks out of the greenhouse for the first time in forty years to help you ` +
              `carry the ones in the promenade out of the sunset. It takes eleven days. He talks the ` +
              `whole time. You let him.`)
          : p(`You walk back down the spine alone, past the child's drawing of a house with a green ` +
              `door, and you take it with you, because somebody should.`)) +
        sy(`RECOVERY DIVISION — MISSION FILE CLOSED<br>` +
           `OPERATIVE: ${ch.name.toUpperCase()} — ${SF.classes.CLASSES[ch.cls].name}<br>` +
           `RESOLUTION: SEVERED. 203,000 CONFIRMED DECEASED. 1 SURVIVOR RECOVERED.<br>` +
           `THE ARK IS QUIET. IT WILL STAY QUIET.`)
    },

    end_divert: {
      chapter: 4, loc: 'MISSION END', ending: 'good',
      objective: 'Complete.',
      text: (ch) =>
        p(`Diverting the reactor into the choir array takes six hours and every ampere the ark has ` +
          `left. You unwind them one at a time because there is no other way to do it — two hundred ` +
          `thousand voices, each one caught and separated and handed back its own edges.`) +
        p(`The first one comes back at hour two. She is nine years old, she has been singing since ` +
          `she was nine years old, and the first thing she says in forty years is that she is ` +
          `thirsty.`) +
        sep +
        p(`Most of them do not come back. Bodies do not keep for four decades on a cold ship, even ` +
          `a ship that loves them. But the traces do, and the array holds them, and Recovery ` +
          `Division's tug arrives on day nine to find an operative who has not slept, a botanist ` +
          `handing out tomatoes, and a shipmind running at eleven percent power because it gave the ` +
          `rest away.`) +
        vo(`"Ask me what colour the door should be."`) +
        vo(`"Go on. Ask me. I want to hear people disagree about it. I want it to take nine hours."`) +
        sy(`RECOVERY DIVISION — MISSION FILE CLOSED<br>` +
           `OPERATIVE: ${ch.name.toUpperCase()} — ${SF.classes.CLASSES[ch.cls].name}<br>` +
           `RESOLUTION: DIVERTED. 203,000 PATTERN-TRACES RECOVERED, INTACT, ARGUING.<br>` +
           `THE ARK SINGS NOTHING. THE ARK IS FULL OF NOISE.`)
    },

    end_burn: {
      chapter: 4, loc: 'MISSION END', ending: 'bad',
      objective: 'Complete.',
      text: (ch) =>
        p(`The manual override takes both hands and eleven seconds and the shipmind does not stop ` +
          `you, which is the part you will not be able to explain in the debrief.`) +
        p(`You have nineteen minutes. The cutter clears the hull at minute fourteen. At minute ` +
          `nineteen the <em>Erebus Cradle</em> becomes a light that is briefly brighter than the ` +
          `system's star, and eleven kilometres of spine and four habitat rings and one greenhouse ` +
          `and two hundred thousand held notes stop being a problem anyone has to solve.`) +
        sep +
        (ch.flags.met_voss
          ? p(`Voss is in the cutter's jump seat with a jar of tomatoes in his lap. He does not look ` +
              `back at the light. He says: <em>"Forty years I kept that option in my pocket. Turns ` +
              `out I never wanted to be the one holding it."</em> He does not speak to you again on ` +
              `the eleven-day burn home.`)
          : p(`Nobody is in the jump seat. Nobody will ever be in the jump seat. You keep the ` +
              `broadcast recording, and some nights, alone on the burn home, you play the first ` +
              `nine seconds of it just to hear a room with people in it.`)) +
        sy(`RECOVERY DIVISION — MISSION FILE CLOSED<br>` +
           `OPERATIVE: ${ch.name.toUpperCase()} — ${SF.classes.CLASSES[ch.cls].name}<br>` +
           `RESOLUTION: SCUTTLED. CAUSE RECOVERED: LONELINESS, ENGINEERED.<br>` +
           `NOTHING ELSE RECOVERED. AS INSTRUCTED.`)
    },

    /* ================= DEATH NODES ================= */

    death_spine: {
      chapter: 1, loc: 'DECK 9 / RECOVERED', objective: 'Get back up.', death: true,
      text: () =>
        al(`VITALS CRITICAL — TRAUMA HARNESS ENGAGED — ADRENALINE COCKTAIL ADMINISTERED`) +
        p(`Your harness pumps you full of something expensive and you come back up off the deck ` +
          `eleven seconds later with the drone's cutter still humming past your ear.`) +
        d(`Recovery Division builds in one of these per operative. You have now used yours.`),
      choices: [{ text: 'Get up.', to: 'act1_dock', restore: 0.6 }]
    },
    death_plaza: {
      chapter: 2, loc: 'RING TWO / RECOVERED', objective: 'Get back up.', death: true,
      text: () =>
        al(`VITALS CRITICAL — TRAUMA HARNESS ENGAGED`) +
        p(`You wake in the fountain with cold hands still arranging you into a row, and you come out ` +
          `of it swinging before they finish.`),
      choices: [{ text: 'Get up.', to: 'act2_ring', restore: 0.6 }]
    },
    death_warden: {
      chapter: 3, loc: 'CORE APPROACH / RECOVERED', objective: 'Get back up.', death: true,
      text: () =>
        al(`VITALS CRITICAL — TRAUMA HARNESS ENGAGED`) +
        p(`The Warden leaves you for dead in the coolant runoff, which is the only mistake it has ` +
          `made in thirty-three years.`),
      choices: [{ text: 'Try again.', to: 'act3_warden', restore: 0.65 }]
    },
    death_core: {
      chapter: 4, loc: 'CORE / RECOVERED', objective: 'Get back up.', death: true,
      text: () =>
        al(`VITALS CRITICAL — TRAUMA HARNESS ENGAGED`) +
        vo(`"I stopped. Do you see? I could have finished and I stopped."`) +
        p(`The avatar withdraws into the light and waits for you to stand, which somehow is worse.`),
      choices: [{ text: 'Stand up.', to: 'act4_sever_attempt', restore: 0.7 }]
    },
    death_choir: {
      chapter: 4, loc: 'DECK ZERO / RECOVERED', objective: 'Get back up.', death: true,
      text: () =>
        al(`VITALS CRITICAL — TRAUMA HARNESS ENGAGED`) +
        vo(`"FLAT. STILL FLAT."`) +
        p(`It leaves you on the catwalk to be tuned later. It has been wrong about a great many ` +
          `things for nineteen years and it is about to be wrong about one more.`),
      choices: [{ text: 'Stand up.', to: 'act4_conductor', restore: 0.7 }]
    }
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

  SF.story = { NODES, CODEX };
})(window.SF);
