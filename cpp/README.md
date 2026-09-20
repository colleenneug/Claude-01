# Erebus Cradle — native game

A standalone C++/OpenGL desktop build combining the cinematic, physically
based renderer (see `../docs/NATIVE_RENDERER.md`) with an actual mission
loop: a physical player with collision, a hitscan weapon, hostiles with real
AI (idle → chase → attack → die), and missions loaded from **plain text
files under `content/`, not compiled in** — adding next month's mission or
boss is dropping a `.cfg` file into `content/missions/`, not a code change.

Between missions you fly a ship through **open space**: pick a save slot,
launch from the Cradle, fly to a world and land on it to start its mission.
A persistent profile — chits (currency), owned and equipped gear, completed
missions — lives in one of three save slots and carries across runs.

The story is the browser build's story. The Cradle is the ark *Erebus
Cradle*, and docking at it opens the **sixteen-mission campaign** the
browser build runs (`../src/js/fps/campaign.js`): one route from the docking
collar to Deck Zero, against the husk drones, choir thralls and warden
frames, ending on the Conductor. Same objectives, same briefings, same
CRADLE/DIVISION/VOSS comms thread, transcribed into `content/missions/`. It
opens one sector at a time — a sector is locked until the one before it is
cleared — and each zone has its own grade, so the route looks like it is
going somewhere: the promenade is a sunset that has been holding since year
six, the reactor runs hot and orange, Deck Zero is nearly black. The
planets stay alongside it as side destinations you can fly to at any time.

A brand-new record starts **on Earth**, in a bunk, in the dark. Kourou,
Block D, four hours before you ship out — and the thing Division recovered
off the ark woke up on the other side of the building first. You pick a
doctrine, and then you wake up with **nothing**: there is a sidearm in the
footlocker across the room, your issued weapon is on the armoury bench four
rooms away, and the route out runs bunks → corridor → armoury → muster hall
→ the blast door → the pad. Real hostiles in it, and each room's own stay
asleep until you enter it, so the building does not empty itself into the
corridor behind you while you are still looking for a weapon.

What you are doing shows as one line, top-left, that changes as you cross
the place — no AREA COMPLETE banner and no pause between rooms, because a
wall of them every ten metres turns a place into a corridor of checkpoints.
Three **cutscenes** frame it: waking up, stepping outside, and the lift at
the end. The browser build's read-in (`../src/js/story.js`, BRIEF 44-C)
plays over the whole thing as comms traffic rather than as a screen you
skip. A record that has cleared anything at all goes straight up.

**You do not have a ship yet.** That is the point of Earth: the transport
parked on the far pan is Division's, it is lit and ramp-down from the moment
you walk outside, and it is not yours. Clearing the pad is what gets you
collected — the closing cutscene is somebody coming down the ramp for you
and Kourou dropping away — and being collected is how you arrive at the
Cradle in the first place. Wash out of the block and you wake up at the top
of the block again, because there is nothing in orbit with your name on it
to go back to.

The pad is not the edge of the world any more, either: the site runs out to
four hundred metres of ground with hangars, assembly buildings and service
towers on it, hazard lights still blinking on top. It costs two dozen boxes
and it is what makes the closing shot work — the pan used to end at its own
perimeter wall, so a camera that climbed showed a launch site hanging in
mid-air over blue sky.

A record is created under one of **three doctrines** — BULWARK, ORACLE,
WRAITH — and the choice is the browser build's (`../src/js/classes.js`):
each one is an issued weapon, a field ability and a passive, and it is fixed
for that record's life. A Bulwark carries the MAUL-12 breaching shotgun,
absorbs 22% of everything it is hit with, and throws up a sixty-point
overshield. An Oracle carries the ARC LANCE, whose rounds pass through the
front rank into whatever stood behind it, and can stun a room with an EMP.
A Wraith carries the suppressed WHISPER, triples headshot damage, and phase
steps out of trouble with the next round primed.

**Scope, honestly stated:** three doctrines, six weapons, three armour
pieces, three cosmetics, seven enemy archetypes, twenty-two missions (the
sixteen-sector campaign plus six side contracts), five destinations —
including an ice world and a desert one. All of it is real, data-driven content under
`content/`, not hardcoded — a monthly drop of new gear or a new mission is
text files, not a code change (see *Content* below). What's still not here:
co-op/netcode (see the note at the bottom of *Roadmap*), the browser
build's class abilities beyond the phase step, its gear rolls, bounties,
chests, crew and dossier screens, and packaging as an actual installable
build. Everything that exists here is real, compiled, and was verified by
actually running it and reading back live game state — not eyeballed
(`tools/verify.sh`).

No texture, model, or asset files ship with this project — every material
shades procedurally from world position and normal (see
`shaders/pbr.frag`), the same "nothing to download, nothing to license"
approach the browser build takes with its canvas-baked textures.

## Building

Dependencies (Ubuntu/Debian package names):

```
sudo apt install cmake g++ pkg-config libglfw3-dev libglew-dev libglm-dev libgl1-mesa-dev
```

macOS (Homebrew): `brew install cmake glfw glew glm` — CMake will find
Apple's OpenGL framework automatically. Windows: install the same four
libraries via vcpkg (`vcpkg install glfw3 glew glm`) and point CMake at the
vcpkg toolchain file.

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/erebus_native                        # runs content/missions/patrol_dust_shelf.cfg
./build/erebus_native --mission colossus_dig_site   # or any other mission id
```

A mission id is a `.cfg` filename under `content/missions/`, without the
extension.

## Controls

The game opens on the **save slot screen**: three records, each its own
file (`save1.dat` … `save3.dat`) next to the executable.

| Input | Action |
|---|---|
| 1 / 2 / 3, or Up / Down | Pick a slot |
| Enter or Space | Load that slot and continue to the hub |
| D, then Y | Delete the selected slot (Y confirms, N cancels — a stray key press shouldn't wipe a record) |

An empty slot goes to **record creation** first — pick a doctrine. A record
that has no doctrine is a record that has not been created yet, however it
was reached, so even a scripted run stops here and asks rather than starting
a campaign with no weapon, no ability and no perk.

| Input | Action |
|---|---|
| 1 / 2 / 3, or Left / Right | Pick a doctrine |
| Enter | Confirm — you get a service sidearm and twelve rounds, and nothing else |
| Escape | Back to the slots |

Then you're in **open space**, in your ship:

| Input | Action |
|---|---|
| Mouse | Steer (the ship goes where it's pointed) |
| W / S | Thrust forward / reverse |
| A / D | Strafe |
| Space / Left Ctrl | Rise / drop |
| Left Shift | Boost |
| E | Land on the world you're near, or dock at the Cradle |

There are **people in it**. Five have posts you can walk up to — VOSS the
quartermaster, KAUR the flight officer, SHAW on contracts, THE ROOK
appraising, ODIL on muster — and the rest are crew, walking routes across
the three decks or leaning on a gallery rail watching the Earth go past. The
walkers are not decoration for its own sake: people you have to walk around
are what make a room feel used rather than like a lobby.

Docking at the Cradle puts you **inside it, on foot**. Three decks around an
open concourse, joined by four stair flights, ported from the browser build's
`../src/js/fps/station.js` — same layout, same module names, same deck
heights:

| Input | Action |
|---|---|
| WASD / mouse | Walk around (the mission movement, minus the gun) |
| E | Use whatever you are standing at |
| Q | Airlock — back to the ship |

Walk up to someone and press E to talk; E again for the next line, Escape to
leave. Hearing **Voss** out opens the armoury and **Kaur** the route —
they are the same Hub screen, with their name on it and the half you did not
come for dimmed (both stay usable: walking back down three decks for a rifle
you forgot would be a punishment, not a hub). **Shaw** opens it on the side
contracts. The Rook and Odil are conversations and nothing else, and say so
in as many words — this build has no duplicate gear to break down and no
second seat to muster, and a screen that did nothing would be worse than an
honest answer. Q, or the airlock at the end of the concourse, backs out to
the ship:

| Input | Action |
|---|---|
| 1 / 2 / 3 | Cycle weapon / armour / cosmetic — equips it if owned, buys-then-equips it if not and it's affordable, otherwise just moves the selection so you can see what you don't own yet |
| Tab | Cycle the selected mission |
| Enter or Space | Launch the selected mission |

In a mission:

| Input | Action |
|---|---|
| Mouse | Look |
| WASD, or the arrow keys | Move (physical: gravity, collision against the level) |
| Left Shift | Sprint |
| Left Ctrl or C | Crouch — and, held at speed, **slide** |
| Space | Jump. Out of a slide it keeps the speed you built, which is the point of sliding |
| Space, against a wall in mid-air | Kick off a **wall run** (hold a wall beside you at speed and you run along it) |
| Left click | Fire (hitscan) |
| R | Reload |
| Q or E | Your doctrine's field ability — barrier, EMP or phase step |
| Right mouse, held | Aim — narrows the FOV and brings depth of field in on the background |
| Escape | Release the mouse; left click re-captures it |
| Enter or Space, once the mission has ended | Return to the Hub (saves your profile) |

Those are the browser build's bindings and the browser build's numbers —
walk 4.6, sprint 9.4, crouch 2.3, and the whole slide table
(`../src/js/fps/player.js`). They are not independent: the slide's entry
threshold is a fraction of the walk speed and its floor and ceiling are set
against the sprint speed, so moving one without the others changes whether
sliding is worth doing at all. The one thing not carried across is the
browser's wall-run camera roll.

Your weapon is drawn in your hands and named next to the ammo counter,
because at a glance "six rounds" means something completely different on a
breaching shotgun than on a suppressed carbine. It is ordinary world
geometry placed on the camera's own basis rather than a separate view-space
pass — the whole renderer already works in world space, and a second pass
with its own projection would need its own copy of the shadows, the fog and
the tone map. Which silhouette gets built is one `shape =` line in the
weapon's own file — `pistol`, `smg`, `rifle`, `carbine`, `marksman`,
`shotgun`, `induction` — so a content drop that adds a fourth shotgun puts a
shotgun in your hands without touching the code, and a pistol reads as a
pistol at a glance rather than as a short rifle. Firing it kicks the view up
and off-centre and gives most of it back over the following fifth of a
second, throws a muzzle flash and a shake, and every round that connects
leaves sparks at the point it hit — sold in world geometry, like everything
else here.

The window title shows frame time, mission name, player HP, ammo, wave
progress and mission state, refreshed twice a second.

## Content: how a monthly drop actually works

Nothing about adding a mission touches C++. Two file types, both plain text
(`key = value` lines, `#` comments, blank lines ignored):

**`content/enemies/<id>.cfg`** — one archetype per file:

```
name = Marauder
hp = 110
speed = 2.5
damage = 14
attack_range = 22        # melee types use ~2, ranged types ~20+
attack_rate = 1.8        # seconds between attacks
radius = 0.5
height = 1.95
colour = 0.42, 0.46, 0.52
glow = 1.0, 0.71, 0.33    # visor / reactor emissive tint
ranged = true
flying = false           # true swaps the legs for a hovering body (see below)
elite = true             # wider build, heavier plating, shoulder-mounted cannon
xp = 40
```

`flying` and `elite` pick the *body*, not just a stat. Each archetype is
assembled from a few dozen primitives on a joint hierarchy, ported from the
browser build's rigs (`../src/js/fps/hostiles.js`): a walker has a jointed
spine, plates sloping off the shoulders into a collar, a head sunk into it
rather than parked on top, overlapping ribs down the front, a back pack that
vents, and either a forearm cannon or — if it is an elite — a shoulder mount.
A flyer has none of that: a core inside a split cowl, a ring of segments that
turns around it, one big optic, three fins, a thruster, and two manipulator
arms hanging below to give it a sense of scale. Four shared unit primitives
(box, sphere, cylinder, taper) are scaled per part, so a one-metre drone and
a three-metre elite cost the same upload.

The head sits at 93% of the archetype's `height` and the torso at 55%,
because that is where the hit spheres are (`Hostile::headCentre`). The
silhouette is built around those two points rather than the other way round
— a rig whose head is not where the head hitbox is means headshots land on
air.

**`content/missions/<id>.cfg`** — an arena size, any number of `wave` lines,
and an optional `boss` line (spawned once every regular wave is cleared,
with a health multiplier on top of the boss's own `enemies/*.cfg` stats):

```
name = THE FALSE SKY
campaign = 6                    # place in the route; omit for side content
zone = promenade                # sectors sharing a zone share a look
objective = Cross the habitat ring.
brief = Forty metres of open promenade under a sunset that has been holding since year six.
arena = 132

# The grade. Every one of these is optional and falls back to the dusty
# default, so a mission file can be three lines or the whole palette.
floor_colour = 0.26, 0.22, 0.18
sky_zenith = 0.075, 0.060, 0.105
sky_horizon = 0.62, 0.30, 0.20
fog_colour = 0.48, 0.26, 0.24
fog_density = 0.012
sun_colour = 1.00, 0.68, 0.42
sun_intensity = 3.2
reward = 105

wave thrall 7 24      # enemy id, count, spawn ring radius (metres)
wave warden 1 30
boss conductor 1.0    # optional; spawns once every regular wave is cleared,
                      # with a health multiplier on its enemies/*.cfg stats

# Story, staged as comms traffic:
#   comms <trigger> <delay> <speaker> | <line>
# triggers: deploy, half, cleared, boss, complete, failed
comms deploy 0.5 VOSS | That is the plaza. My flat was on the third tier.
```

A spawn ring is not decoration: a warden has 26 metres of reach
(`content/enemies/warden.cfg`), so it spawns at 30 — outside its own range,
inside its alert radius — and has to close rather than open fire from the
ring it spawned on.

**`content/planets/<id>.cfg`** — somewhere to fly to:

```
name = Glacius - Frozen Shelf
position = -2200, 620, 3400   # where it sits in open space
radius = 480
colour = 0.62, 0.78, 0.92     # the two colours continents mix between
colour2 = 0.30, 0.46, 0.64
cap = 0.55                    # how far the polar ice reaches, 0 = none
mission = glacius_ice_fields  # what landing here drops you into
# station = true              # the Cradle instead: docking opens the hub
```

A mission with `tutorial = true` cannot complete until its objectives are
done, however fast you shoot. It is a mission rather than a mode of its own,
so it gets the level, the weapon, the HUD and the comms thread for free.

`layout = <name>` swaps the procedural arena for a hand-built place
(`Site.h`, built in `Sites.cpp`): rooms with doors between them, which is
not something a scatter of cover on a walled field can express, and the
opening of this game is rooms. A site carries its own geometry, where the
player starts, where the hostiles stand and which trigger wakes them, what
weapons are lying about, the named boxes that fire a scene, and the ordered
objectives. `start_unarmed = true` means exactly that: empty hands, no ammo
counter, no viewmodel, nothing to fire.

Cutscenes are content:

```
scene wake 3.4 | -1.2, 0.45, -26.2 | -1.2, 1.9, -25.0 | Four hours before you ship out.
scene wake 3.2 | -1.2, 0.75, -26.1 | -1.0, 1.6, -22.0 | The alarm is sounding.
```

Each line is one held shot: how long, where the camera sits, what it looks
at, and the caption. Consecutive shots of the same scene ease into each
other, so a slow push in is two lines rather than a keyframe format, and a
hard cut is two shots that do not share a position. Two names are special:
`wake` plays on arrival, and `complete` plays when the mission is won.
Every other one fires from the trigger box of the same name. The world keeps
simulating underneath — the alarm keeps sounding and nothing walks into a
frozen room — but takes no input, so you cannot walk out of your own
establishing shot. Comms hold: a scene owns the whole frame, so a line that
came due behind the letterbox was a line nobody ever saw, and the block's
nine-second opening was swallowing the first two things Division says to
you. Space, Enter or Escape skips — a cutscene you have already seen is a
loading screen — and the skip is deliberately *not* "any key", because the
key that got you into the scene is usually still held down.

**`content/crew/<id>.cfg`** — somebody standing in the Cradle:

```
name = VOSS
title = QUARTERMASTER
line = Gear, parts, and the bench.
colour = 1.00, 0.71, 0.33
position = -37, 7, 2
facing = -90                  # degrees; a figure's front is local -Z
shop = gear                   # gear | route | contracts | none
desk = true                   # a lit post to stand behind
# board = true                # ...and a wall of lit slates, for contracts
say | You want something that will still be working when the shooting stops.
say | Everything on this bench came off something that stopped working.
```

`shop` is what separates a vendor from a conversation. `none` means talking
to them is the whole interaction, which is the honest answer for the posts
whose screens this build does not have.

**`content/classes/<id>.cfg`** — a doctrine:

```
name = BULWARK
role = AEGIS DOCTRINE
tagline = Armour grown from the hull of a dead ship. Walks first, always.
accent = 1.00, 0.71, 0.33
weapon = maul_12              # issued free with the record
ability = barrier             # barrier | breach | phase
ability_name = AEGIS BARRIER
ability_desc = Overshield that soaks the next wave of fire.
ability_cooldown = 16
perk_name = BULKHEAD PLATING
perk = Sealed armour absorbs 22% of all incoming damage.
hp = 124
damage_reduction = 0.22
```

The three abilities are the three the browser build has, and they are code
(`Game::useAbility`) rather than data, because each one does something
structurally different: a pool of temporary health, a stun applied to every
hostile in a radius, and a collision-resolved dash. `damage_reduction` here
and an armour piece's stack *multiplicatively* — 22% doctrine plus an
eventual 80% armour piece would otherwise reach immunity, and each layer
should shave a share of whatever got through the last one anyway.

**`content/weapons/<id>.cfg`**, **`content/armor/<id>.cfg`**,
**`content/cosmetics/<id>.cfg`** — equippable gear, all the same format:

A weapon carries its ballistics too, and they are what make a doctrine's
weapon feel like a different weapon rather than different numbers:
`pellets` (rays per trigger pull — a shotgun's `damage` is *per pellet*),
`spread` (the cone in radians, applied per pellet), `pierce` (the ray
carries on through a hostile into whatever stood behind it) and `range`
(past which the shot simply misses — a shotgun that reaches as far as a
rifle is a rifle). The pellet scatter is a deterministic function of which
shot and which pellet rather than a global RNG, because two identical runs
have to produce identical results for the headless suite to mean anything.

```
# content/weapons/marksman_carbine.cfg
name = Marksman Carbine
damage = 42
headshot_multiplier = 2.6
fire_interval = 0.32     # seconds between shots
reload_time = 2.0
mag_size = 12
cost = 220                # chits; 0 = starter gear, owned from a fresh profile
```

```
# content/armor/scout_rig.cfg
name = Scout Rig
hp_bonus = 20
damage_reduction = 0.05   # fraction of incoming damage absorbed
cost = 120
```

```
# content/cosmetics/ember.cfg
name = Ember
accent = 1.0, 0.65, 0.3    # recolours the HUD: health-full, ammo pips, crosshair
cost = 90
```

A mission can also pay out currency on its first completion, and carry its
own story as staged comms beats:

```
reward = 40    # chits, paid once — add this line to a mission.cfg

# comms <trigger> <delay-seconds> <speaker> | <line>
# triggers: deploy, half, cleared, boss, complete, failed
comms deploy 0.6 CRADLE CONTROL | DROP CONFIRMED. YOU'RE ON THE DUST SHELF.
comms half   0.4 CRADLE CONTROL | HALF THE NEST IS DOWN.
comms boss   0.3 VANGUARD ECHO  | HEAD SHOTS. DON'T STAND STILL.
```

The pipe separates the speaker from the line, so neither needs quoting and
the line can contain spaces and punctuation freely. Beats fire off real
mission progress, never a timer alone, and each trigger fires once per run.

Content is loaded once at startup from the `content/` directory next to the
executable (`EREBUS_CONTENT_DIR` overrides the path). A bad or missing
individual file is logged and skipped rather than aborting the whole load —
see `Content::loadAll` in `src/Content.cpp`.

## What's actually simulated

- **Profile** (`Profile.h/.cpp`): chits, owned/equipped weapon, armour and
  cosmetic, completed-mission list. Saved as a plain `key = value` file; a
  first run with no save file gets a fresh profile with starter gear
  already granted, never a "no save" error state. Starter gear is a service
  sidearm and twelve rounds — nothing else. Your doctrine's weapon is not
  issued at a desk, it is on the armoury bench in Block D, and walking over
  it is what puts it in your inventory. Three **save slots**
  (`save1.dat` … `save3.dat`) are picked on the startup screen, which shows
  each record's chits, missions cleared and equipped weapon, or EMPTY.
  `ProfileStore::exists` backs that distinction, since `load()` deliberately
  can't tell you — it hands back a playable profile either way.
  `EREBUS_SAVE_PATH` points at one explicit file and skips slot selection
  (what every headless test uses); `EREBUS_SLOT=<1-3>` picks a slot.
- **Space** (`Space.h/.cpp`): the ship, and the open space you fly it
  through. Worlds come from `content/planets/*.cfg` — a position, a radius,
  two surface colours, a polar cap size, and the mission you land into — so
  adding a destination is a text file like everything else. Planets are
  shaded from surface *direction* rather than world position (`shadePlanet`
  in `pbr.frag`): the metre-scale noise every other material uses smears
  into a few flat patches on a body five hundred units across, while
  direction-space noise gives continents sized as a fraction of the globe
  however big the globe is. Latitude drives the banding and the caps.
  Flight is deliberately arcade rather than
  Newtonian: velocity is damped toward the thrust direction, so releasing
  the key coasts to a stop and the ship goes where it's pointed. True
  frictionless flight means every nudge is permanent, which is miserable to
  actually fly. Bodies are solid — you stop at the surface and slide along
  it rather than passing through the middle of a planet, and the range
  that offers you the landing prompt is derived from that standoff
  distance (`engageRangeFor`) rather than picked separately — the two
  numbers drifting apart is exactly how the Cradle became impossible to
  dock at, with the ship held further out than the prompt could reach.
- **Station** ("THE CRADLE" — `Station.h/.cpp`): the place you come back to,
  and walk around in. Three decks — concourse at y 0, gallery ring at 7,
  upper ring and the cupola at 14 — around an atrium that is open through all
  three, so from the floor you can see the cupola and from the cupola you can
  see the floor. It is built as a list of boxes handed to
  `Level::buildFromParts`, which is how the browser build writes it too: a
  station is a set of rooms, and a room is easier to write as the space it
  occupies than as a centre plus a size. Deck plates are cut around their
  holes — the atrium, and a well over every stair flight — because a flight
  running under an unbroken slab stops you dead when your head reaches it,
  two thirds of the way up. The browser's two lifts are not here; every deck
  is reachable on foot, and a lift is a collider that moves, which collision
  built once per level would need rebuilding for.

  The room is lit by strips rather than lamps, because this renderer has one
  directional light and a probe and emissive geometry illuminates nothing.
  The strips read as the sources; the flat fill (`ambientFill`) carries the
  room, standing in for the bounce off a hundred metres of white panel.
- **Sites** (`Site.h`, `Sites.cpp`): hand-built places. Kourou Block D is the
  one that exists — bunks, corridor, armoury, muster hall, pad. Doorways are
  *cut* rather than modelled: a wall with a hole in it is two walls and a
  lintel, for the same reason the station's deck plates are cut around their
  wells. Every lintel is 2.6 metres up, which is unrealistically high on
  purpose: a player standing on a knee-high bench beside a door is 2.25
  metres tall, and a realistic 2.1-metre lintel makes them too tall to walk
  through their own armoury. Every bench and table has a lane through it for
  the same reason — those two, plus a footlocker that turned out to be a
  wall, were the whole of what went wrong while building it.
- **Cutscene** (`Cutscene.h/.cpp`): the scripted camera. See *Content* above
  for the format.
- **Crew** (`Crew.h/.cpp`): the people. Their figures are deliberately *not*
  the enemy rig — those are armoured frames, plated and jointed and visored;
  these are people in coats, built lighter and rounder and shaded as cloth
  rather than plate, because you should never have to look twice to tell a
  quartermaster from something that came off the ark. The furniture they
  stand behind belongs to the Station rather than to Crew: only what goes
  into the level's part list gets a collider, and a counter you can walk
  through is not a counter.
- **Hub** ("THE CRADLE" — `Hub.h/.cpp`): the between-mission loadout and
  destination picker — see *Controls* above. Cycling an unowned item buys
  it if it's affordable; `Hud::drawHub` lists every weapon, armour piece,
  shader and destination by name with its price and status (equipped /
  owned / affordable / out of reach), the selected row carrying a caret.
- **Player** (`Player.h/.cpp`): gravity, jump, sprint, crouch, slide, wall
  run, and substepped collision against the level so a fast move can't
  tunnel through a thin wall in one frame. Equipped armour raises `maxHp`
  and shaves a fraction off every hit taken (`Player::damageReduction`),
  applied in `Game::update`. The slide is the one move the rest is built
  around: crouch *at speed* takes whatever speed you arrived with, multiplies
  it, then bleeds it off — and jumping out keeps it. The wall run turns
  gravity down rather than off, so it is always a descent: it buys distance,
  not flight, and you cannot re-attach to the same face until you have
  touched something else.
- **Weapon** (`Weapon.h/.cpp`): hitscan against the level's colliders *and*
  every live hostile's head/body spheres — a crate genuinely blocks a shot
  to whatever's behind it. Magazine, reserve ammo, reload timer — all of it
  set from the equipped `WeaponDef` at mission start.
- **Hostiles** (`Hostile.h/.cpp`): a procedural armoured rig (shared
  geometry across every instance, regardless of size — see the file's
  header comment) with a state machine — idle until alerted, chase with
  perpendicular stuck-avoidance steering (direct pursuit alone deadlocks
  perfectly against an obstacle centred on the straight line to the
  player; see the comment on `Hostile::stuckT`), attack at range or in
  melee, death.
- **Level** (`Level.h/.cpp`): both the mission arenas and the station, because
  they need the same collision. Resolution does three things in order, and
  the order is the whole design: find what holds the cylinder up (the highest
  box top under its footprint that is no more than a step above its feet),
  push it out of anything that actually blocks (a box whose top is more than
  a step above the feet *and* whose bottom is below the head), then settle it
  onto the support — recomputed, because being pushed sideways can move it
  over a different box. That is what makes a stair a stair rather than a
  wall, and what lets a deck plate seven metres up be a floor to whoever is
  standing on it and a ceiling to whoever is walking under it.

  A walled arena is built fresh per mission from its
  `arena` size — from 102 metres at the docking collar to 192 on Deck Zero.
  Cover comes in three shapes, because they do three different jobs: a block
  you hide behind, a barricade you crouch behind and shoot over, and a pillar
  that takes a sightline away entirely. An arena of nothing but waist-high
  crates plays the same at eighty metres as at two hundred — every fight is
  still everyone shooting everyone. The count follows the *area*, not the
  side length: doubling the arena quadruples the ground to cross, and cover
  spread linearly over that leaves a parade ground with a few boxes round the
  edge. Barricades are placed on one axis or the other rather than at an
  arbitrary angle, because the collider is an AABB and a rotated four-metre
  box would have one several metres wider than the thing you can see.
  Overlapping cover is rejected outright: two overlapping AABBs make
  `resolve` fight itself and push whatever is between them out along two axes
  at once. Every mission-defined spawn point is resolved against the level
  once at spawn so a hostile can never start out wedged inside cover.
- **Missions** (`Game.h/.cpp`): spawns every wave immediately, holds the
  boss back until the waves are clear, tracks win (all hostiles Gone) and
  loss (player HP 0) conditions, and on a first win pays the mission's
  `reward` chits into the profile via `Profile::recordMissionComplete` (a
  repeat clear doesn't pay out again).
- **The trauma harness** (`Game.h/.cpp`): running out of HP does not end an
  ordinary mission. It puts you *down* — a few seconds on the floor where
  you fell, the world still running around you and nothing you can do about
  it — and then the harness stands you back up on the spot at 60% health,
  with whatever was standing over you shoved back and its swing reset. Three of them per mission; the fourth time you go down is
  the mission. Boss missions get none, because the point of a boss is that
  it can kill you. This replaces restarting a twenty-minute sector from the
  top because one warden caught you at the far end of it, and it is why
  dying somewhere specific matters: you come back *there*, into the same
  fight, not at the spawn.
- **Pickups** (`Game.h/.cpp`): a killed hostile drops resupply — ammo, or
  health every third kill — collected by walking over it. Not decoration:
  the dig site was measurably unwinnable without it, since clearing seven
  hostiles *and* a 900-HP boss doesn't fit inside the fixed starting
  reserve. The drop pattern is a fixed rotation rather than a random roll,
  so a run can't be starved by luck and a mission's total resupply is a
  known quantity when tuning it.
- **HUD** (`Hud.h/.cpp`): health bar and readout, ammo pips with real
  counts, reload sweep, a crosshair with a hit-marker flash, wave progress,
  a named boss bar, comms lines, pickup notes, and the end-of-mission
  banner — tinted by the equipped cosmetic's accent colour. Text comes from
  a hand-authored 5x7 bitmap font (`Font.h`): glyphs are bit patterns
  expanded into quads, because there's no offline way to fetch a
  font-rendering library — the same "draw it from primitives" approach as
  the procedural materials. Every glyph in a frame rides in one buffer and
  one draw call, so a screen full of text doesn't become thousands of tiny
  draws on weak integrated hardware.
- **Story** (`Content.h`, `Game.h/.cpp`): staged comms traffic, the same way
  the browser build carries its story rather than stopping for a dialogue
  screen. Each mission declares its own beats in its `.cfg` and they fire
  off real mission progress — deploy, half-cleared, waves cleared, boss
  spawn, complete, failed — with per-beat delays, one voice on the channel
  at a time, and a hold time scaled to the line's length.

## Adaptive quality

Ported from the browser build's `fps/engine.js` (`TIERS`/`trackFrame`):
`Renderer` watches a smoothed frame time and steps down through four tiers —
`high` → `medium` → `low` → `minimal` — trading away shadow-map resolution,
bloom level count, whether depth-of-field is allowed to run at all, and dust
mote count, in that order, the same as the browser build gives up the least
valuable thing left first. Falling is fast (about 1.5s of sustained slow
frames); climbing back is deliberately much harder, and gets harder each
time it has already fallen, so it can't sit oscillating between two tiers.
The current tier shows in the window title (`gfx:<name>`).

Not ported: the browser build's tiers also scale a pixel-ratio
supersampling factor, which would need an extra upscale-blit stage this
renderer doesn't have — left out rather than half-implemented. See
`EREBUS_QUALITY_TIER`/`EREBUS_QUALITY_AUTO` above to force a tier or turn
auto-adjustment off.

## Verifying it without a display

`tools/verify.sh` is the whole suite in one command: it runs the real binary
under Xvfb with a fixed timestep, reads back the state each run logs, and
asserts on it — landing on both new worlds, the Cradle's dock prompt
reachable after leaving it, the campaign's first sector playing end to end
and paying out, the route staying locked ahead of your progress, the patrol
contract still completing, and space surviving both ends of the quality
ladder. Run it before pushing anything.

The landing checks need a frame budget that stops just past the landing. Run
them longer and the mission itself ends — the scripted pilot flies but does
not shoot back — and drops you out to space again, which is
indistinguishable from never having landed.

Beyond `EREBUS_DUMP_FRAME`/`EREBUS_MAX_FRAMES` (render `n` frames
off-screen and dump the last as a PPM — see `docs/NATIVE_RENDERER.md` for
why that mattered for the renderer), the gameplay loop has its own headless
verification hooks, because a run with no physical input device still needs
a way to prove movement, combat and mission state actually work:

- `EREBUS_FORCE_FORWARD=1` — holds W the whole run (`Player::update`'s
  `forceForward` parameter), so wall collision can be verified without a
  real keyboard.
- `EREBUS_FORCE_FIRE=1` — holds the trigger, reloading only when the
  magazine is actually empty. (It used to hold the reload key too, which
  kept the weapon permanently mid-reload and let a whole run fire about six
  rounds — fine for the one-hostile fixture it was written against, and
  quietly useless for measuring whether a real wave is survivable.)
- `EREBUS_TUTORIAL_AUTO=1` — feeds a step-sequence tutorial exactly the input
  it is asking for (sprint, jump, crouch-at-speed, reload, ability). The same
  idea as `EREBUS_DEBUG_AUTOAIM`: it exists so a sequence can be proved end
  to end without a keyboard. Block D does not need it — it is laid out along
  +Z on purpose, so holding W walks the whole thing, which is what makes
  "every doorway, lintel, bench and table leaves a lane through" a check
  rather than a hope.
- `EREBUS_SKIP_TUTORIAL=1` — sends a brand-new record straight up instead of
  to the ground site. Every check in `tools/verify.sh` starts from a fresh
  save, so without this every one of them would begin in the tutorial.
- `EREBUS_CLASS=<id>` — settles the doctrine without the creation screen, the
  same idea as `EREBUS_SLOT` for the record itself: a headless run has no way
  to press a key on a screen whose whole job is to ask a question.
- `EREBUS_STATION_AT="x,y,z"` / `EREBUS_STATION_YAW=<deg>` — drop the player
  at a spot inside the Cradle on arrival, so a run can stand at the foot of a
  stair flight rather than only walking the spine in a straight line.
- `EREBUS_SCENE=<name>` — roll a named cutscene on arrival instead of `wake`.
  Writing a cutscene otherwise means playing to the trigger box that fires
  it: twenty minutes of walking to look at four seconds of camera, and a
  headless check of the *closing* shots would have to clear the whole block
  first. Every shot of Block D's lift was framed with this.
- `EREBUS_DEBUG_AUTOAIM=1` — snaps the camera onto the nearest hostile it
  can actually see, every frame. A verification aid only, **never enabled by
  default** — it exists so firing can be exercised without simulating real
  mouse input. The line-of-sight part is not a refinement: once arenas
  carried pillars and barricades, aiming at the nearest hostile regardless of
  what stood in front of it meant a whole run could be spent shooting a wall,
  and the starter patrol started reporting itself as unwinnable when the only
  thing broken was the aid. It also used to find its target by scanning the
  draw list for the nearest emissive item, which stopped meaning "a hostile"
  the moment rigs grew lit vents and kills started dropping glowing pickups.
- `EREBUS_LOG_STATE=<path.json>` — at `EREBUS_MAX_FRAMES`, writes one JSON
  line of live state: mission progress/HP/ammo/chits if in a mission,
  or chits/equipped gear/selected mission if still in the Hub
  (`"appState"` says which).
- `EREBUS_SAVE_PATH=<path>` — profile save file location (default
  `save.dat`); point it at a scratch path so a test run never touches a
  real player's progress.
- `EREBUS_SPACE_AUTOPILOT=<planet id>` — steers the ship at that body every
  frame, the flight-mode counterpart of `EREBUS_DEBUG_AUTOAIM`. With
  `EREBUS_FORCE_FORWARD=1` holding the throttle, a headless run can fly the
  full distance to a world.
- `EREBUS_FORCE_ENGAGE=1` — presses the contextual action key: E once the
  autopilot's target is in range, and the confirm at the end of a mission.
  Together with the autopilot this exercises space → land → fight → back to
  the ship end to end without a keyboard.
- `EREBUS_SKIP_SPACE=1` — go straight to the hub menu instead of open
  space, for the hub-script tests that predate flight.
- `EREBUS_FIXED_DT=0.0166` — advance the simulation by exactly this much
  per frame instead of by real elapsed time. **Use this for any gameplay
  test that asserts an outcome.** Without it a run's result depends on how
  fast the host happens to be: a loaded machine yields a larger clamped dt,
  so the same frame budget covers several times as much game time, and a
  close fight flips between won and lost between identical runs. This was
  not hypothetical — it was discovered by a regression suite that passed
  and then failed with no gameplay change in between.
- `EREBUS_SKIP_HUB=1` — boot straight into `--mission` with whatever's
  currently equipped, bypassing the Hub — every verification flow that
  predates the Hub still lands in a mission on frame 0.
- `EREBUS_HUB_SCRIPT="1,2,mission,launch"` — drives the Hub deterministically
  with no real keyboard: one comma-separated token consumed per frame while
  still in the Hub (`"1"`/`"2"`/`"3"` cycle-equip-or-buy a category,
  `"mission"` cycles the mission, `"launch"` commits) — the same idea as
  `EREBUS_FORCE_FORWARD`, but for menu input.
- `EREBUS_DEBUG_PIXEL=1` — every 60 frames, prints the screen-centre pixel
  from both the pre-tonemap linear HDR scene buffer and the final image,
  plus the health bar's pixel. Turns "the screen looks dark/black" into
  actual numbers on hardware that can't be tested directly — see
  `Renderer::debugPrintCenterPixel`'s comment for how to read the output.
- `EREBUS_QUALITY_TIER=<0-3>` — forces high/medium/low/minimal and disables
  auto-adjustment (see *Adaptive quality* below); `EREBUS_QUALITY_AUTO=0`
  disables auto-adjustment without forcing a tier. Both double as a
  diagnostic: if a simpler tier renders correctly where the default doesn't,
  that narrows down which pass is actually broken on that hardware.

```
Xvfb :99 -screen 0 1280x800x24 &
DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 \
  EREBUS_FORCE_FIRE=1 EREBUS_DEBUG_AUTOAIM=1 EREBUS_SKIP_HUB=1 \
  EREBUS_SAVE_PATH=/tmp/save.dat \
  EREBUS_LOG_STATE=/tmp/state.json EREBUS_MAX_FRAMES=400 \
  ./build/erebus_native --mission debug_single
```

`content/missions/debug_single.cfg` is a one-hostile fixture kept for
exactly this: verifying the shoot → damage → kill → mission-complete
pipeline in isolation, without a squad's combined DPS confounding the test.

## Project layout

```
CMakeLists.txt
content/
  enemies/*.cfg           enemy archetypes — see "Content" above
  missions/*.cfg          mission definitions
  weapons/*.cfg           equippable weapons
  armor/*.cfg             equippable armour
  cosmetics/*.cfg         HUD accent skins
  planets/*.cfg           destinations in open space
src/
  main.cpp                window, input, SlotSelect/Space/Hub/Mission state machine, headless hooks
  Gl.h                    GLEW/GLFW/GLM include point + glCheck()
  Shader.{h,cpp}          program compile/link, cached uniform locations
  Camera.{h,cpp}          view: look direction, FOV, the aim blend
  Draw.h                  MaterialType + DrawItem, shared by every drawable
  Mesh.{h,cpp}            box/cylinder/sphere/terrain-plane generators
  Framebuffer.{h,cpp}     2D render target (HDR + optional depth texture)
  CascadedShadowMap.{h,cpp}  3 cascades, refit to the view frustum per frame
  Bloom.{h,cpp}           5-level downsample/tent-upsample bloom
  IBL.{h,cpp}             room capture -> prefiltered cubemap
  Renderer.{h,cpp}        orchestrates one frame, shadow pass through composite
  Content.{h,cpp}         loads enemies/missions/weapons/armor/cosmetics/planets from content/
  Profile.{h,cpp}         persistent save: chits, owned/equipped gear, completed missions
  Hub.{h,cpp}             between-mission loadout/mission picker
  Space.{h,cpp}           the ship, open space, planets and docking
  Scene.h                 what Renderer needs from a world (Game and Space both supply it)
  Font.h                  hand-authored 5x7 bitmap font, as bit patterns
  Level.{h,cpp}           arena geometry + AABB colliders
  Player.{h,cpp}          physical controller: gravity, jump, collision, armour damage reduction
  Weapon.{h,cpp}          hitscan vs level + hostiles
  Hostile.{h,cpp}         rig, AI state machine, stuck-avoidance steering
  Hud.{h,cpp}             2D overlay: bars, pips, crosshair, hub swatches
  Game.{h,cpp}            owns and drives all of the above
shaders/
  pbr.vert / pbr.frag     the one material program every opaque object uses
  depth.vert / depth.frag cascade depth-only pass
  motes.vert / motes.frag camera-relative dust
  ibl_capture.*           the light-room -> cubemap capture pass
  fullscreen.vert         shared "big triangle" vertex stage for every post pass
  bright / downsample / upsample .frag   the bloom chain
  dof.frag, composite.frag
  hud.vert / hud.frag     2D HUD rectangles
  hud_text.vert / .frag   batched HUD text (one draw call per frame)
```

## Roadmap

- ~~**Phase 2** — more content: additional enemy archetypes and weapon
  types.~~ Done for weapons/armour/cosmetics (three each, all data-driven —
  see *Content*); a proper level-building path beyond one walled arena
  shape is still open.
- ~~**Phase 3** — the systems that make it a persistent game: gear/loadouts,
  currencies, a save file, a hub to return to between missions.~~ Done —
  see *Profile* and *Hub* above.
- **Phase 4** — packaging as an actual downloadable build (installer,
  versioning) rather than something built from source.

Ideas that came up but were deliberately left out of this pass, since they
weren't asked for: a per-kill bounty economy (chits currently pay out once
per mission clear, not per kill — see the comment in `Game::update`), and
armour/weapon rarity tiers or stat rolls beyond the fixed stats a `.cfg`
file declares.

### Still only in the browser build

The browser build (`src/js/fps/`) is a much larger game than this one, and
it's worth naming what has *not* been carried across rather than leaving
the gap implicit. Ported so far: the renderer, the mission loop, gear and
currency, the hub, adaptive quality, and the comms-traffic story format.
Not ported: the campaign structure and its destination/planet system
(`campaign.js`, `planets.js`), the walkable station hub (`station.js` — the
Cradle here is a menu, not a place you walk around), bounties, loot chests,
crew, the dossier/codex, gear rarity and rolls (`gear.js`), the ability
loadout the Vanguard HUD is built around (`d2hud.js` shows grenade / melee
/ super meters; this game has no abilities to meter), and networking.

Not roadmapped, and worth saying plainly rather than leaving implicit:
**co-op/netcode** is not planned for this native build in the near term —
the browser build's host-authoritative WebSocket approach doesn't carry
over for free, and networking a native FPS correctly (interpolation,
reconciliation, anti-cheat surface) is its own project.
