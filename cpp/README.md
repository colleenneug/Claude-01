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

**Scope, honestly stated:** three weapons, three armour pieces, three
cosmetics, three enemy archetypes, four missions, three destinations. All of it is real,
data-driven content under `content/`, not hardcoded — a monthly drop of new
gear or a new mission is text files, not a code change (see *Content*
below). What's still not here: co-op/netcode (see the note at the bottom of
*Roadmap*) and packaging as an actual installable build. Everything that
exists here is real, compiled, and was verified by actually running it and
reading back live game state — not eyeballed.

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

Then you're in **open space**, in your ship:

| Input | Action |
|---|---|
| Mouse | Steer (the ship goes where it's pointed) |
| W / S | Thrust forward / reverse |
| A / D | Strafe |
| Space / Left Ctrl | Rise / drop |
| Left Shift | Boost |
| E | Land on the world you're near, or dock at the Cradle |

Docking at the Cradle opens the **Hub** (Q undocks back to the ship):

| Input | Action |
|---|---|
| 1 / 2 / 3 | Cycle weapon / armour / cosmetic — equips it if owned, buys-then-equips it if not and it's affordable, otherwise just moves the selection so you can see what you don't own yet |
| Tab | Cycle the selected mission |
| Enter or Space | Launch the selected mission |

In a mission:

| Input | Action |
|---|---|
| Mouse | Look |
| WASD | Move (physical: gravity, collision against the level) |
| Left Shift | Sprint |
| Space | Jump |
| Left click | Fire (hitscan) |
| R | Reload |
| Right mouse, held | Aim — narrows the FOV and brings depth of field in on the background |
| Escape | Release the mouse; left click re-captures it |
| Enter or Space, once the mission has ended | Return to the Hub (saves your profile) |

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
xp = 40
```

**`content/missions/<id>.cfg`** — an arena size, any number of `wave` lines,
and an optional `boss` line (spawned once every regular wave is cleared,
with a health multiplier on top of the boss's own `enemies/*.cfg` stats):

```
name = The Dig Site: Colossus
arena = 90

wave scarab 4 30      # enemy id, count, spawn ring radius (metres)
wave marauder 3 22
boss colossus 2.2
```

**`content/planets/<id>.cfg`** — somewhere to fly to:

```
name = Erebus III - Dust Shelf
position = 2600, 240, -1800   # where it sits in open space
radius = 520
colour = 0.72, 0.58, 0.40
mission = patrol_dust_shelf   # what landing here drops you into
# station = true              # the Cradle instead: docking opens the hub
```

**`content/weapons/<id>.cfg`**, **`content/armor/<id>.cfg`**,
**`content/cosmetics/<id>.cfg`** — equippable gear, all the same format:

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
  already granted, never a "no save" error state. Three **save slots**
  (`save1.dat` … `save3.dat`) are picked on the startup screen, which shows
  each record's chits, missions cleared and equipped weapon, or EMPTY.
  `ProfileStore::exists` backs that distinction, since `load()` deliberately
  can't tell you — it hands back a playable profile either way.
  `EREBUS_SAVE_PATH` points at one explicit file and skips slot selection
  (what every headless test uses); `EREBUS_SLOT=<1-3>` picks a slot.
- **Space** (`Space.h/.cpp`): the ship, and the open space you fly it
  through. Worlds come from `content/planets/*.cfg` — a position, a radius,
  a colour, and the mission you land into — so adding a destination is a
  text file like everything else. Flight is deliberately arcade rather than
  Newtonian: velocity is damped toward the thrust direction, so releasing
  the key coasts to a stop and the ship goes where it's pointed. True
  frictionless flight means every nudge is permanent, which is miserable to
  actually fly. Bodies are solid — you stop at the surface and slide along
  it rather than passing through the middle of a planet.
- **Hub** ("THE CRADLE" — `Hub.h/.cpp`): the between-mission loadout and
  destination picker — see *Controls* above. Cycling an unowned item buys
  it if it's affordable; `Hud::drawHub` lists every weapon, armour piece,
  shader and destination by name with its price and status (equipped /
  owned / affordable / out of reach), the selected row carrying a caret.
- **Player** (`Player.h/.cpp`): gravity, jump, sprint, substepped collision
  against the level so a fast move can't tunnel through a thin wall in one
  frame. Equipped armour raises `maxHp` and shaves a fraction off every hit
  taken (`Player::damageReduction`), applied in `Game::update`.
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
- **Level** (`Level.h/.cpp`): a walled arena with scattered crate cover,
  built fresh per mission from its `arena` size; every mission-defined
  spawn point is resolved against the level once at spawn so a hostile can
  never start out wedged inside a crate.
- **Missions** (`Game.h/.cpp`): spawns every wave immediately, holds the
  boss back until the waves are clear, tracks win (all hostiles Gone) and
  loss (player HP 0) conditions, and on a first win pays the mission's
  `reward` chits into the profile via `Profile::recordMissionComplete` (a
  repeat clear doesn't pay out again).
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
- `EREBUS_DEBUG_AUTOAIM=1` — snaps the camera onto the nearest hostile
  every frame. A verification aid only, **never enabled by default** —
  it exists so firing can be exercised without simulating real mouse input.
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
