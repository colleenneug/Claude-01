# THE DEEP CHOIR // Put Out The Light

A first-person shooter set aboard a consecrated colony ark, running in a browser on
WebGL. No build step, no network required — open `index.html` and play.

You are on the wrong side of it. Forty years ago the cathedral ark *Erebus Cradle* was
promised that nobody aboard would ever die, and the light at the top of it kept the
promise on all 203,000 of them. You were the part it did not judge worth keeping.
Something further down disagreed, dug you up, and sent you back aboard to climb eleven
kilometres of blessed hull and put the light out.

---

## Running it

```
# simplest
open index.html          # macOS   (xdg-open on Linux, or just double-click)

# or serve it, if you prefer
python3 -m http.server 8000   # then visit http://localhost:8000
```

Everything is plain HTML/CSS/JS loaded with classic `<script>` tags, so `file://` works
directly — no ES-module CORS problems and nothing to install. The one dependency is
three.js r147, vendored into `vendor/` (the last release shipping a UMD build, which is
what keeps `file://` working). Nothing is fetched at runtime except the two web fonts.

Requires WebGL2 and a real GPU. On a software rasteriser it will render correctly but
far below playable frame rates.

### Controls

| | |
|---|---|
| `W` `A` `S` `D` | Move |
| Mouse / `←` `→` | Turn |
| `Shift` | Sprint |
| `Ctrl` / `C` | Crouch |
| `Space` | Jump |
| Left mouse | Fire |
| Right mouse | Aim down sights |
| `R` | Reload |
| `Q` / `E` | Field ability |
| `Esc` | Pause |

Click **ENGAGE** to capture the pointer. Where pointer lock is unavailable — an embedded
frame that does not allow it — the game falls back to unlocked mouse look. In that mode
the cursor runs out of screen before you finish a turn, so holding it near the left or
right edge keeps rotating; the arrow keys turn too, in either mode. A full 360° is always
reachable.

## Co-op

The game ships with its own server. It serves the game *and* runs the multiplayer relay
on one port, with no dependencies:

```
node server/server.js          # then open http://localhost:8080
PORT=9000 node server/server.js
```

Open **SQUAD LINK** from the campaign screen, hit *Open a room*, and give the four-letter
code to your squad — up to four operatives per insertion. When the host starts a mission,
everyone in the room drops into it.

**Authority.** The first player in a room hosts: it simulates every hostile and broadcasts
world snapshots at about 12 Hz, and everyone else renders those and reports the damage they
deal back for the host to apply. Movement is always owned by the client doing the moving,
and remote bodies are interpolated a fraction of a second behind the wire so they glide
rather than teleport. If the host leaves, the next player is promoted automatically.

`server/ws.js` is a small RFC 6455 implementation — handshake, framing, ping/pong, close —
so co-op does not cost the project its "clone it and run it" property. There is no
`npm install`.

Co-op needs a server, so it is unavailable in the published single-file build (a static
page has nothing to connect to). Everything else in the game runs identically there.

### Single-file build

`dist/the-deep-choir.html` is the whole game inlined into one page — every
stylesheet and script, no external requests except the Google Fonts pair. Use it
when the game has to travel as a single file (hosting it somewhere, emailing it,
opening it off a USB stick). Regenerate it after changing anything under `src/`:

```
python3 tools/bundle.py
```

---

## Rendering

There are no texture or model assets in this repository. Every surface is generated at
load time on a 2D canvas — an albedo pass, a height pass converted to a tangent-space
normal map with a Sobel filter, and a roughness pass — which is what makes the hull read
as riveted plating and the deck as grating when a light moves across it. Enemies and the
weapon viewmodel are procedural geometry.

Lights are pooled. three keys its shader programs partly on how many lights are in the
scene, so adding or removing one recompiles every material — doing that per bullet impact
stalls the frame badly. Instead the scene gets a fixed set of point lights once, and each
frame the nearest emitters (ceiling strips, impacts, enemy halos, projectiles) are
assigned to them. Tracers, sparks and projectiles are pre-allocated and recycled, and
pre-warmed during loading, so sustained fire allocates nothing at all.

three's `EffectComposer` ships only as an ES module, so the post chain is hand-rolled:
render to a target, threshold the bright pixels, blur them separably at half resolution,
then composite with bloom, edge chromatic aberration, film grain, vignette and a damage
pulse. Lighting is physically based with ACES filmic tone mapping, exponential fog and a
shadow-casting key light.

## The menu interface

Outside a mission the native pointer is hidden and replaced by a targeting reticle that
reports what it is over:

| Cursor state | Meaning |
|---|---|
| Cyan ring, small | Idle — nothing under the pointer |
| Amber, expanded, labelled | Over something interactive; the label names the action |
| Red | Destructive or dangerous (erasing a dossier, entering a fight) |
| Dimmed, no glow | Locked — the option exists but not for your doctrine |
| Filling arc | **Hold to commit.** Erasing a character requires a sustained press, not a click |

Supporting the reticle: a drifting starfield, CRT scanlines and sweep, a perspective grid
floor, panel corner brackets, glitch-split titles, and a fully procedural sound bank —
every bleep, gunshot, impact and fanfare is synthesised in the Web Audio API at runtime,
so there are no audio assets in the repository either.

---

## Dread, and why you cannot hide

The Deep pays in **dread**. It rises on every kill — more for a head shot, more again for
a shatter — and it starts bleeding away three seconds after you stop killing. Fill the
meter and it spends itself all at once as **Rapture**: eight seconds at +35% damage and
+15% movement, and then an empty bar.

Dread also does what a medkit used to. There is no out-of-combat regeneration in this
game any more; **devour** heals you when you kill, scaled by the dread you are carrying,
and the only thing left for the walk between fights is a trickle that stops at a third of
your health. Backing into a corner to heal up is not a strategy the dark side offers. The
armour's regeneration affix scales devour rather than the trickle, so old gear still means
something.

## The three doctrines

Your class decides your weapon, your field ability and a passive that changes how a
firefight plays out. There is no swapping in the field.

### ✦ CARAPACE — *Grave Doctrine*
**GRAVEMAUL scattergun** — 8 shards × 17 damage, 75 rpm, 6-round magazine. Devastating
inside ten metres, useless past thirty.
- **Field ability — Grave Ward:** 60 points of overshield, +15 more for every body within
  nine metres, to a cap of four. A barrier worth having requires standing in the middle
  of them.
- **Passive — Grave Plating:** absorbs 22% of all incoming damage. The only doctrine that
  can stand in a corridor and trade with something immortal.

### ◈ HOLLOW — *Cipher Doctrine*
**NULL LANCE cipher rifle** — 26 damage, 320 rpm, 24-round magazine, and rounds
**pierce**: a bolt passes through its target and into whatever stood behind it.
- **Field ability — Crystallize:** damages everything within thirteen metres and freezes
  it solid for four seconds. Frozen bodies do not move and do not shoot.
- **Passive — The Cold Syntax:** deepest magazine and the flattest recoil on the roster.

### ✵ SHROUD — *Umbral Doctrine*
**WHISPER suppressed carbine** — 15 damage, 640 rpm, 32-round magazine, **×3 on a head
shot**.
- **Field ability — Devour Step:** blink forward, briefly untargetable, next round primed
  for double damage, and it feeds you on the way through.
- **Passive — Blindside:** fragile in the open, lethal from an angle nobody blessed.

## Freezing and shattering

Anything Hollow freezes is **primed**, not merely stopped. The next hit on a frozen body
shatters it for **2.4× damage** and throws 34 damage into everything within four and a
half metres. A shatter cannot set off another shatter — the splash is guarded against
recursion — so a frozen crowd is one large payoff rather than an unbounded chain.

## Palebearers

From mission 7 the ark starts fielding **Palebearers**, and killing one does not kill it.
The body drops and a **Spark** comes out — small, fast, orbiting its bearer, with no
attack of its own. You have five seconds. Kill the spark and the body stays a body; leave
it and the Palebearer stands back up at 60% health, once. The downed body is not a valid
target while the spark is up, so the spark is the whole fight.

They arrive one at a time, reach two by Ward Six and three by the threshold. Both states
ride the co-op snapshot, so a squad sees the same body go down, the same spark come out,
and the same thing get back up.

---

## The campaign

Sixteen missions, run end to end down the length of the ark, and connected three ways.

**Geographically** — the route is one continuous ship. Each mission starts at the near
edge of its own sector, where the previous one finished: docking collar → maintenance
spine → Junction 9 → the Sunguard run → habitat ring → greenhouse → medical → reactor
antechamber → the reliquary → the dais. Several sectors are fought twice — arrival, and
then the counter-attack — which is how the ark closes under you.

**Mechanically** — rank, XP and the unlock chain persist on the character. Clearing a
mission unlocks the next; the campaign screen shows the whole route with what is cleared,
what is next and what is still locked.

**Narratively** — the comms beats run as one thread across all sixteen. **The Pale**
(the light at the top, which wants you back), **the Deep** (what raised you), the
**Choirmaster** who went up before you, and **Candle** — a Palebearer who took the Pale
in year six and has spent forty years in a greenhouse refusing to keep it — talk over you
throughout. The argument they are having resolves in the last mission.

Difficulty climbs on four axes at once — enemy count, which types appear, a health
multiplier and a damage multiplier:

| Mission | Hostiles | Health ×| Damage ×|
|---|---|---|---|
| 1 · Hard Dock | 3 | 1.00 | 1.00 |
| 4 · Counter-Attack | 11 | 1.22 | 1.15 |
| 7 · Under the Dawn | 15 | 1.44 | 1.30 |
| 10 · Ward Six | 20 | 1.67 | 1.45 |
| 13 · The Reliquary | 23 | 1.89 | 1.60 |
| 15 · The Threshold | 29 | 2.04 | 1.70 |
| 16 · The First Light | boss | 2.11 | 1.75 |

Palebearer counts are in that hostile total, but each of them is two kills — the body and
the spark — so the back half of the campaign is heavier than the numbers suggest.

## Ammunition

Ammo kits are scattered through every sector — thirty-odd across the ark, two to five per
room, placed against the level's own collider list so none of them end up inside a wall.
Walk over one for two magazines' worth of reserve. They re-form about half a minute after
being taken, so a long fight cannot dry you out, and your reserve has a ceiling: at full
capacity a kit is left where it is rather than wasted.

## Dying

The Deep will put you back on your feet **three times** in an ordinary sector, shown as
pips beside your vitals. Lose your vitals and a charge brings you back at the sector entry a couple of
seconds later, at full health, with a magazine loaded and a brief grace period — the
hostiles you already killed stay dead. Spend all three and the mission is over.

**The reliquary floor gets none.** The last mission is the one with no return: the pip
meter is not even displayed, and losing your vitals there means taking the fight again
from the top.

## The First Light

The final mission is a boss with a shield you cannot shoot off, because the shield is its
own light. A health bar runs along the bottom of the screen with a segment per phase;
while the shield holds, the bar reads as hatched and unavailable and every round you put
into it does nothing.

To break it you solve a puzzle, inverted. Four reliquary nodes stand around the dais,
each with its own colour and pitch. The First Light illuminates a phrase across them —
**give it back backwards, shooting the nodes in reverse order**. The last note it showed
you is the first one it wants. Get it right and the shield drops long enough to hurt it.
Get one wrong and it starts the bar again, and takes a swing at you for the interruption.

There are four phases, and each is a longer phrase played faster: three notes, then four,
then five, then six. Miss your damage window and it re-shields with the harder phrase
anyway. It summons the blessed, and it opens up with an area attack that fills the arena
whether the shield is up or not — the puzzle is meant to be solved under fire, not
standing still.

**And then it gets back up.** Killing the body only lets the light out of it: a spark
comes loose and runs, drifting on a wide arc around the dais that tightens as you hurt
it, with no attack of its own. Hitting it is the difficulty. Nothing on this ship is
finished the first time you kill it, including this.

---

## Gear, rarity and power

Every cleared mission pays out salvage. Items roll on a five-tier ladder — **common,
uncommon, rare, epic, exotic** — and the tier sets both a flat stat multiplier and how
many affixes the item carries. The odds tilt with how deep you are: mission 1 rolls about
55% common and 1% exotic, mission 16 rolls 18% common and 13% exotic. The First Light never
pays out junk.

Weapons multiply your doctrine's issued gun rather than replacing it — damage, magazine,
rate of fire, reload and spread — so a SHROUD always carries a WHISPER, but an exotic one
hits nearly twice as hard. Armour fills three slots (helm, plate, greaves) and grants
vitals, damage resistance and regeneration. A straight upgrade equips itself so the reward
lands immediately; everything else is swapped in the armoury.

**Power** is the single number that summarises an operative: rank plus everything
equipped. Raising it is the point — rank comes from XP, the rest comes from salvage.

## Your operative

The armoury has a live 3D preview of the character you are actually building — drag to
turn them around. Skin tone, hair style and hair colour are yours to set, and equipped
armour is plated in its own rarity colour, so an exotic helm reads as gold across the
room. Hair sits under a helmet when one is equipped, as it would.

## Orbital destinations

Clearing mission 6 opens two worlds the cutter can reach. They are patrol zones, not
missions: you drop in, and what you do there is up to you. Nothing pushes you along and
nothing ends the trip except you — stand on the landing pad and hold `F` whenever you want
to leave.

**They are big.** Each zone is 620 m in radius — 1.21 km², about 1.24 km across, a hundred
times the ground area of a mission sector. Roughly 1,900 pieces of cover are scattered
across it, drawn as three instanced meshes rather than nineteen hundred draw calls, and
collision runs through a spatial hash so a hostile tests a handful of nearby rocks instead
of the whole world. Sprint speed is raised on patrol, which puts a crossing at about eighty
seconds. `ZONE_R` in `fps/planets.js` is the single number that sets all of it.

**A living population.** Hostiles are streamed rather than placed: sixteen live in a band
55–150 m around you, anything left more than 260 m behind is retired, and the band refills
around wherever you walk to. The zone stays inhabited without ever simulating a kilometre
of empty ground.

**Different enemies on each world.** The desert belongs to **The Pale Order** — censers,
ordinals, and reliquary frames. The ice belongs to **The Lamplit** — embers, lamplighters,
and beacon wardens. Nothing crosses over. Both are holdings the light reached before the
ark did and never left.

**Public events.** Every minute or so an event fires somewhere in the zone. It is
announced, it drops a beam marker you can see from across the map, the panel tracks its
timer and progress, and it runs whether or not you go to it:

| Event | What it is |
|---|---|
| **Reconsecration** | They are relighting a site in force. Put it back out. |
| **High-Value Heretic** | An elite is in the open with an escort. Kill it. |
| **Snuff the Relay** | Hold ground on the relay until the lamp goes out, while they keep coming. |

Finishing one pays salvage and XP on the spot — the reward rolls against the event's
difficulty, and elites roll higher. Failing one costs nothing but the time.

**With friends.** Everything above is shared: the host owns the population and the events
and broadcasts them, so your squad sees the same event marker, fights the same hostiles,
and everyone is paid when it completes.

## Three character slots

The crew registry holds exactly three bays, persisted to `localStorage`. Each dossier
carries its own name, doctrine, rank, XP and its own campaign progress — how far down the
ark that operative has got — and can be deployed and erased independently. Rank carries
across missions; XP is awarded for kills and banked on both success and failure. Storage failures (private windows, blocked site data) degrade to
a session-only game rather than crashing.

Erasing a bay is deliberately awkward: press and **hold** the ERASE control until the
cursor's arc completes.

---

## Layout

```
index.html                markup and screen scaffolding
vendor/three.min.js       three.js r147 (UMD), the only dependency
src/css/base.css          palette, cursor rig, ambience, shared chrome
src/css/screens.css       boot, title, registry, enlistment, codex, briefing
src/css/fps.css           in-mission HUD, engage gate, pause, debrief
src/js/util.js            helpers, toasts, modal
src/js/audio.js           procedural Web Audio sound bank
src/js/cursor.js          the menu reticle, hover states, hold-to-commit
src/js/fx.js              menu starfield
src/js/classes.js         the three doctrines and levelling
src/js/storage.js         the three save slots
src/js/story.js           briefing fiction and field codex
src/js/fps/campaign.js    the sixteen missions, escalation curve, unlock chain
src/js/fps/materials.js   procedural PBR textures (albedo / normal / roughness)
src/js/fps/lights.js      fixed-size light pool (constant scene light count)
src/js/fps/engine.js      renderer, tone mapping, hand-rolled post chain
src/js/fps/level.js       ship interior, collision boxes, lighting, props
src/js/fps/player.js      pointer-lock look, movement physics, collision
src/js/fps/weapons.js     the three weapons, hitscan, recoil, ADS, abilities
src/js/fps/ai.js          hostiles, steering, freeze/shatter, palebearers, sparks
src/js/fps/hud.js         crosshair, vitals, dread, ammo, comms, kill feed, boss bar
src/js/fps/boss.js        the First Light: shield phases, reversed phrase, its spark
src/js/fps/game.js        mission loop, dread and devour, objectives, player condition
src/js/ui.js              menus and the handoff into a mission
src/js/main.js            entry point
tools/bundle.py           inlines the above into one self-contained page
dist/the-deep-choir.html  the generated single-file build
```
