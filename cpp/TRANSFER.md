# Moving Erebus Cradle onto Aedan's Playground

For the session building **Aedan's Playground** (C++20 / Vulkan; plan in
`aedans-playground/ROADMAP.md` and `HANDOFF.md` on branch
`claude/relaxed-cray-5kc2fl`). This is the game side of Milestone 5 ("Host
Erebus Cradle"): where the game is, what it has become since the roadmap was
written, and the seams to port along.

- Game branch: `claude/scifi-game-classes-c2ljc6`, folder `cpp/`.
- Size: about 22,700 lines of C++17, 18 GLSL 4.10 shaders, and 264 content
  files (`content/`).
- Status: builds on Linux (GCC) and Windows (MSVC + vcpkg), and passes
  `tools/verify.sh` (150 headless checks) as of this document.

**The rule from the roadmap still holds: `cpp/` keeps working until the
Playground plays the whole campaign.** Port; do not delete.

---

## 1. Already done on the game side, to make the move a port

The roadmap named two leaks as "the real work of the port". Both are fixed.

| Leak (roadmap §1) | Now |
|---|---|
| `Gl.h` included by nearly every header | Gameplay includes `Math.h` (GLM only). `Gl.h` is included only by the renderer side (`Renderer`, `Mesh.cpp`, `Hud.cpp`, `Shader`, `Framebuffer`, `CascadedShadowMap`, `IBL`, `Bloom`), `PlatformInput.cpp` and `main.cpp`. No gameplay `.cpp` sees OpenGL, even through its includes. |
| Ten gameplay files poll GLFW keys | Gameplay reads an `Input` snapshot (`Input.h`): an engine-neutral `Key` enum, keys held, text typed, time. `PlatformInput.cpp` is the only file with GLFW key codes; `main.cpp` samples it once a frame. |

Also changed for the move:

- **Particles are data.** The dust motes and the starfield used to be GL
  buffers owned by `Game` and `Space`. Now `SceneSource::motePoints()` returns
  plain floats (x, y, z, phase, size per point) and the renderer owns the GPU
  copy.
- **`Mesh` and `Hud` hide their GPU side.** `Mesh.h` keeps its handles as plain
  integers, and `Hud.h` keeps its shaders behind a private struct.

---

## 2. The seams

Port these in order. Everything not listed moves across unchanged.

### 2.1 Drawing (replace the renderer, keep the draw list)

- `Scene.h`: `SceneSource`, what a world gives the renderer.
  - `collect(time, out)` fills `std::vector<DrawItem>`.
  - The sun, sky, fog, IBL strength, view distance, whether to draw shadows,
    and mote settings are all virtual getters with defaults.
  - Three worlds implement it: `Game` (missions, patrols, open worlds),
    `Station` (hideout and Cradle) and `Space`.
  - `extras` is a list appended after `collect()`; online players are drawn
    through it.
- `Draw.h`: `DrawItem`, holding a `const Mesh*`, a model matrix, a
  `MaterialType` (Armour, Terrain, Emissive, Rock, Planet), tints, metallic,
  roughness, wear, emissive colour and strength, and a shadow flag. **There
  are no textures**: every surface is shaded procedurally from world position
  and normal (`shaders/pbr.frag`).
- `Mesh.h/.cpp`:
  - box, cylinder, sphere and terrain-plane generators, plus `upload`/`draw`;
  - the vertex is position, normal and a blend weight;
  - shared unit meshes come from `HostileGeometry::unitBox/Sphere/Cylinder/Taper()`.
- `Renderer.cpp` is the reference look for Milestone 4, in this frame order:
  1. cascaded shadow maps;
  2. HDR scene;
  3. motes;
  4. bloom;
  5. depth of field;
  6. composite (sky, fog, grade).
- Quality tiers (`EREBUS_QUALITY_TIER`) switch cascades, bloom and DoF.
- `Hud.h` is the 2D layer: `begin`, `rect`, `text`, `textCentered`,
  `wrapped`, `worldToScreen` and `end`, with a 5x7 bitmap font (`Font.h`).
  The new engine needs the same small API; every screen (`Hud::draw*` and
  `NetView`) is built only from those calls.

### 2.2 Input (fill one struct)

- Fill `Input` (`Input.h`) from the Playground's input subsystem once a frame.
  That replaces `PlatformInput.cpp`; nothing else changes.
- The roadmap's action layer can sit in front of it later. Gameplay currently
  asks for keys (`in.held(Key::W)`), so the action table can start as a
  key-to-action map.
- `main.cpp` still reads the mouse and a few app-level keys directly (kit
  screen `G`, weapon slots `1`, `2` and `X`, the leave dialog, cutscene skip).
  Those belong to the app layer and move with `main.cpp`.

### 2.3 Collision (behind a query interface)

- `Level.h` holds the world as axis-aligned boxes (`Collider{min,max}`). There
  are three queries:
  - `resolve(pos, radius, height)`: step up to 0.45 m, push out of walls;
  - `lineOfSight(a, b)`;
  - `wallAt(point, normal)`.
- `Level::buildFromParts` takes `Level::Part` boxes. Expeditions
  (`Sites.cpp`) and open worlds (`World.cpp`) are generated as parts.
- Open worlds have up to about 600 colliders. That is the roadmap's trigger
  for a uniform grid (§4.8).

### 2.4 Moves unchanged

| Area | Files | Note |
|---|---|---|
| Content | `Content.h/.cpp`, `content/` | The `.cfg` format. New missions, weapons and planets need no rebuild. |
| Save files | `Profile.h/.cpp` | Each save slot (`save1-3.dat`), and `account.dat`, the account shared by every slot. |
| Audio | `Audio.h/.cpp`, `third_party/miniaudio.h` | All sound is generated in code; there are no sound files. |
| Gameplay | `Game`, `Player`, `Weapon`, `Hostile`, `Station`, `Crew`, `Space`, `Hub`, `Loadout`, `Post`, `Bounty`, `Cutscene`, `Sites`, `World` | |
| Networking | `Net*`, `NetProtocol.h` | Stays in `game/`, as the roadmap decided. |

---

## 3. New since the roadmap read `cpp/`

The roadmap describes "campaign, hub, open space, co-op over its own UDP
layer". It has grown. Everything below is in `cpp/README.md` in detail.

### Guns

- 125 weapons, every one with a unique look: a palette per maker, plus
  fittings such as muzzle, optic, underbarrel, magazine and stock.
- Heavy kinds: sniper, LMG, rocket launcher, grenade launcher.
- Real projectiles with gravity, splash damage and a proximity fuse.

### Planets are open worlds (`World.h/.cpp`, `mode = world`)

- Each planet is generated from a seed of its mission id, so every player's
  game builds the same one. `--worlds` checks this.
- Each world has a landing ground, five named regions (mesas, canyons,
  ruins, craters, spires, dunes, lava fissures) and cliffs round the edge.
- Cybers spawn at random near players and despawn when nobody is near.
- There is no end. A downed player gets back up at the nearest landing zone.

### Online (`Net*`)

- **Server and copies.**
  - The game *is* the server (`--server`).
  - Online is automatic: the game finds a server on the local network or runs
    one in a background thread, and another copy takes over if the host
    leaves.
  - Every place (hideout, Cradle, each planet, each mission, orbit) is split
    into copies of 12. A squad mate pulls you into their copy.
- **Gamer tags.** `NAME#1234`, server-assigned and unique, owned by the
  account rather than a slot.
- **Social.** Friends with presence, joining a friend where they are, and
  the social screen on `O`.
- **Shared enemies everywhere** except PvP or a mission with `shared = false`.
  - One game per copy hosts the Cybers. The others mirror them from
    snapshots and send their hits to the host.
  - In missions with objectives the host leads the objectives.
- **Modes.**
  - PvP: the Pit and the Sump. Whoever is leading is marked for everyone.
  - The Tuning (Gambit-style): two teams, notes banked at a tap, "screamers"
    sent across to the other team.
- **More than one server.** A main server handles social traffic and
  placement. Instance servers (`--join`) run the copies.

### Engine work this implies

- **Render world:** one more `SceneSource`-style producer (online players),
  the drop-in beam, and the open worlds' long view distance (900 m).
- **Threads:** the game runs a server thread when hosting. Shutdown is
  ordered (`HostGuard` in `main.cpp`).

---

## 4. How to verify a port

- `tools/verify.sh [build-dir]` runs the real binary under Xvfb and llvmpipe
  with a fixed timestep, reads back state the game writes
  (`EREBUS_LOG_STATE`, `EREBUS_NET_LOG`) and checks it. It covers 150 checks:
  the campaign, guns, worlds, online and servers. The Playground build should
  pass the same suite.
- Headless hooks, all environment variables (full list in the README):

  | Hook | What it does |
  |---|---|
  | `EREBUS_SKIP_HUB` with `--mission <id>` | Start straight in a mission |
  | `EREBUS_FIXED_DT` | Fixed timestep |
  | `EREBUS_MAX_FRAMES` | Stop after N frames |
  | `EREBUS_DUMP_FRAME` | Save a screenshot (PPM) |
  | `EREBUS_FORCE_FIRE`, `EREBUS_DEBUG_AUTOAIM` | A bot that fights, hunts and banks |
  | `EREBUS_NET_*` | Online automation |

- `tools/net_test.py` and `tools/net_shard_test.py` test the server with no
  window and no GPU. They do not change when the renderer does.
- `--looks`, `--routes` and `--worlds` print content checks without a window.
- Reference images for Milestone 4: run a mission with `EREBUS_DUMP_FRAME`
  in both builds at the same frame and compare.

---

## 5. Suggested order for Milestone 5

This follows the roadmap's "port by seam".

1. **Draw list.** A `game/` module whose `SceneSource` worlds feed the
   Playground's render world. Start with the HUD (the 2D API above) and one
   mission: `tutorial_earth` (HOME).
2. **Input.** Fill `Input` from the Playground's input layer.
3. **Collision.** Put `Level` behind the physics query interface.
4. **Hideout and hub.** `Station`, `Hub`, `Loadout`, `Post`.
5. **Space and open worlds.** `Space`, `World`.
6. **Online.** Two clients in one copy, then shared enemies in a patrol.
   `verify.sh`'s online section is the exit test.

The game side keeps moving meanwhile: new content and gameplay land in `cpp/`
behind the same seams, so they come across with the port.
