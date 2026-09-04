# Erebus Cradle — native game

A standalone C++/OpenGL desktop build combining the cinematic, physically
based renderer (see `../docs/NATIVE_RENDERER.md`) with an actual mission
loop: a physical player with collision, a hitscan weapon, hostiles with real
AI (idle → chase → attack → die), and missions loaded from **plain text
files under `content/`, not compiled in** — adding next month's mission or
boss is dropping a `.cfg` file into `content/missions/`, not a code change.

**Scope, honestly stated:** this is Phase 1 — one weapon, one arena shape,
three enemy archetypes, text-file missions with waves and an optional boss.
It is not a port of the browser build's inventory, currencies, cosmetics,
hub, save system, or netcode; see *Roadmap* below for where those land.
Everything that exists here is real, compiled, and was verified by actually
running it and reading back live game state — not eyeballed.

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

Content is loaded once at startup from the `content/` directory next to the
executable (`EREBUS_CONTENT_DIR` overrides the path). A bad or missing
individual file is logged and skipped rather than aborting the whole load —
see `Content::loadAll` in `src/Content.cpp`.

## What's actually simulated (Phase 1)

- **Player** (`Player.h/.cpp`): gravity, jump, sprint, substepped collision
  against the level so a fast move can't tunnel through a thin wall in one
  frame.
- **Weapon** (`Weapon.h/.cpp`): hitscan against the level's colliders *and*
  every live hostile's head/body spheres — a crate genuinely blocks a shot
  to whatever's behind it. Magazine, reserve ammo, reload timer.
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
  loss (player HP 0) conditions.
- **HUD** (`Hud.h/.cpp`): health bar, ammo pips + reload sweep, a crosshair
  with a hit-marker flash, a wave-progress bar, a boss health bar. No text
  rendering — this project has no offline way to fetch a font-rendering
  library, so numbers and names aren't drawn yet. Everything shown is
  genuinely wired to live state.

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
- `EREBUS_LOG_STATE=<path.json>` — at `EREBUS_MAX_FRAMES`, writes mission
  state, player HP/position, ammo and wave progress as one JSON line.

```
Xvfb :99 -screen 0 1280x800x24 &
DISPLAY=:99 LIBGL_ALWAYS_SOFTWARE=1 \
  EREBUS_FORCE_FIRE=1 EREBUS_DEBUG_AUTOAIM=1 \
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
src/
  main.cpp                window, input, frame loop, headless verification hooks
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
  Content.{h,cpp}         loads enemies/missions from content/
  Level.{h,cpp}           arena geometry + AABB colliders
  Player.{h,cpp}          physical controller: gravity, jump, collision
  Weapon.{h,cpp}          hitscan vs level + hostiles
  Hostile.{h,cpp}         rig, AI state machine, stuck-avoidance steering
  Hud.{h,cpp}             2D overlay: bars, pips, crosshair
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

- **Phase 2** — more content: additional enemy archetypes and weapon
  types, a proper level-building path beyond one walled arena shape.
- **Phase 3** — the systems that make it a persistent game: gear/loadouts,
  currencies, a save file, a hub to return to between missions.
- **Phase 4** — packaging as an actual downloadable build (installer,
  versioning) rather than something built from source.

Not roadmapped, and worth saying plainly rather than leaving implicit:
**co-op/netcode** is not planned for this native build in the near term —
the browser build's host-authoritative WebSocket approach doesn't carry
over for free, and networking a native FPS correctly (interpolation,
reconciliation, anti-cheat surface) is its own project.
