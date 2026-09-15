# Erebus Cradle — native game

A standalone C++/OpenGL desktop build combining the cinematic, physically
based renderer (see `../docs/NATIVE_RENDERER.md`) with an actual mission
loop: a physical player with collision, a roster of hitscan weapons,
hostiles with real AI (idle → chase → attack → die), and missions loaded
from **plain text files under `content/`, not compiled in** — adding next
month's mission, enemy or weapon is dropping a `.cfg` file into
`content/missions/`, `content/enemies/` or `content/weapons/`, not a code
change.

It also has a persistent profile — chits (currency), owned and equipped
gear, completed missions — picked in a keyboard-driven **Hub** between
missions and saved to disk, so progress carries across runs.

**Scope, honestly stated:** six weapons (three starter-tier shop items plus
three combat archetypes with pellet spread or piercing — see *Content*
below), three armour pieces, three cosmetics, six enemy archetypes across
two factions, four missions. All of it is real, data-driven content under
`content/`, not hardcoded — a monthly drop of new gear, a new enemy or a new
mission is text files, not a code change. What's still not here: a proper
level-building path beyond one walled arena shape, co-op/netcode (see the
note at the bottom of *Roadmap*), and packaging as an actual installable
build. Everything that exists here is real, compiled, and was verified by
actually running it and reading back live game state — not eyeballed.

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

The game opens in the **Hub**, not straight into a mission:

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

Nothing about adding a mission, an enemy, or a piece of gear touches C++.
Five file types, all plain text (`key = value` lines, `#` comments, blank
lines ignored):

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

**`content/weapons/<id>.cfg`** — one weapon per file, and both a combat
archetype and a shop item at once: `cost` (chits; 0 = starter gear, owned
from a fresh profile) is what the Hub charges to buy and equip it, and
`pellets`/`spread_degrees`/`pierce` are what it actually does in a mission.
`pellets > 1` fires that many hitscan rays per trigger pull, each randomised
within `spread_degrees` (a shotgun — one pull, one shell, several pellets,
which is also why a shotgun's magazine only drops by one per pull, not
eight); `pierce` fires a single ray that damages every hostile it crosses
before the wall instead of stopping at the nearest one (an induction bolt
punching through). Both default off, so a plain weapon entry is a
single-target hitscan with no special behaviour beyond its stats:

```
name = MAUL-12
damage = 17
pellets = 8
spread_degrees = 4.0
headshot_multiplier = 1.4
mag_size = 6
reserve_ammo = 30
fire_interval = 0.8       # seconds between trigger pulls
reload_time = 2.2
cost = 150                 # chits
```

**`content/armor/<id>.cfg`**, **`content/cosmetics/<id>.cfg`** — the other
two equippable gear slots, same `key = value` format:

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

**`content/missions/<id>.cfg`** — an arena size, an optional `weapon = <id>`
that pins a specific weapon for this mission regardless of what's equipped
(omit it and the profile's equipped weapon applies, same as any other
mission), an optional `reward = <chits>` paid once on first completion
(defaults to 40), any number of `wave` lines, and an optional `boss` line
(spawned once every regular wave is cleared, with a health multiplier on
top of the boss's own `enemies/*.cfg` stats):

```
name = The Dig Site: Colossus
arena = 90
weapon = whisper

wave scarab 4 30      # enemy id, count, spawn ring radius (metres)
wave marauder 3 22
boss colossus 2.2
```

Content is loaded once at startup from the `content/` directory next to the
executable (`EREBUS_CONTENT_DIR` overrides the path). A bad or missing
individual file is logged and skipped rather than aborting the whole load —
see `Content::loadAll` in `src/Content.cpp`.

## What's actually simulated

- **Profile** (`Profile.h/.cpp`): chits, owned/equipped weapon, armour and
  cosmetic, completed-mission list. Saved as a plain `key = value` file
  (`ProfileStore::save`/`load`, default path `save.dat`, `EREBUS_SAVE_PATH`
  overrides it); a first run with no save file gets a fresh profile with
  starter gear already granted, never a "no save" error state.
- **Hub** (`Hub.h/.cpp`): the between-mission loadout/mission picker — see
  *Controls* above. Cycling an unowned item buys it if it's affordable;
  Hud's `drawHub` renders it back as swatches (green = equipped, blue =
  owned, dim grey/red = affordable/not), the same bar-and-colour language
  the mission HUD speaks, since this project has no text rendering.
- **Player** (`Player.h/.cpp`): gravity, jump, sprint, substepped collision
  against the level so a fast move can't tunnel through a thin wall in one
  frame. Equipped armour raises `maxHp` and shaves a fraction off every hit
  taken (`Player::damageReduction`), applied in `Game::update`.
- **Weapon** (`Weapon.h/.cpp`): hitscan against the level's colliders *and*
  every live hostile's head/body spheres — a crate genuinely blocks a shot
  to whatever's behind it. Magazine, reserve ammo, reload timer, and two
  content-driven variants on the base single-target case: pellet spread
  (a shotgun) and piercing (an induction rifle bolt that damages every
  hostile in line before the wall) — all of it set from the equipped
  `WeaponDef` at mission start (the profile's choice, or a mission's pinned
  `weapon = <id>`; see *Content* above).
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
- **HUD** (`Hud.h/.cpp`): health bar, ammo pips + reload sweep, a crosshair
  with a hit-marker flash, a wave-progress bar, a boss health bar — tinted
  by the equipped cosmetic's accent colour. No text rendering — this
  project has no offline way to fetch a font-rendering library, so numbers
  and names aren't drawn yet, in the Hub or in a mission. Everything shown
  is genuinely wired to live state.

## Verifying it without a display

Beyond `EREBUS_DUMP_FRAME`/`EREBUS_MAX_FRAMES` (render `n` frames
off-screen and dump the last as a PPM — see `docs/NATIVE_RENDERER.md` for
why that mattered for the renderer), the gameplay loop has its own headless
verification hooks, because a run with no physical input device still needs
a way to prove movement, combat and mission state actually work:

- `EREBUS_FORCE_FORWARD=1` — holds W the whole run (`Player::update`'s
  `forceForward` parameter), so wall collision can be verified without a
  real keyboard.
- `EREBUS_FORCE_FIRE=1` — holds the trigger (and auto-reloads when empty).
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
- `EREBUS_SKIP_HUB=1` — boot straight into `--mission` with whatever's
  currently equipped, bypassing the Hub — every verification flow that
  predates the Hub still lands in a mission on frame 0.
- `EREBUS_HUB_SCRIPT="1,2,mission,launch"` — drives the Hub deterministically
  with no real keyboard: one comma-separated token consumed per frame while
  still in the Hub (`"1"`/`"2"`/`"3"` cycle-equip-or-buy a category,
  `"mission"` cycles the mission, `"launch"` commits) — the same idea as
  `EREBUS_FORCE_FORWARD`, but for menu input.

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
src/
  main.cpp                window, input, Hub/Mission state machine, headless verification hooks
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
  Content.{h,cpp}         loads enemies/missions/weapons/armor/cosmetics from content/
  Profile.{h,cpp}         persistent save: chits, owned/equipped gear, completed missions
  Hub.{h,cpp}             between-mission loadout/mission picker
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
```

## Roadmap

- ~~**Phase 2** — more content: additional enemy archetypes and weapon
  types.~~ Done: a data-driven weapon system with pellet spread and piercing
  variants, three more weapons on top of the shop's original three, and
  three more enemy archetypes (a second faction, on the ice-world roster
  the browser build's orbital destinations already use — see *Content*). A
  proper level-building path beyond one walled arena shape is still open.
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

Not roadmapped, and worth saying plainly rather than leaving implicit:
**co-op/netcode** is not planned for this native build in the near term —
the browser build's host-authoritative WebSocket approach doesn't carry
over for free, and networking a native FPS correctly (interpolation,
reconciliation, anti-cheat surface) is its own project.
