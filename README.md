# EREBUS CRADLE // Signal Lost

A first-person shooter set aboard a derelict colony ark, running in a browser on
WebGL. No build step, no network required — open `index.html` and play.

Forty years ago the colony ark *Erebus Cradle* went silent eleven light-years out with
203,000 people aboard. Six days ago it started transmitting again. Not a distress code —
a lullaby. You are the single asset Recovery Division is willing to spend on finding out why.

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

## The desktop build, from the title screen

There are two Erebus Cradles: the browser game in `src/`, and the native C++/OpenGL
build in `cpp/` (its own README covers what it is). The title screen carries a
**LAUNCH GAME** plate that opens the native one.

A page can't start a process, so the plate asks the project's own server to do it —
which means the desktop build launches from the browser only when the page is being
served by `server/server.js`, on the same machine:

```
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release && cmake --build cpp/build -j
node server/server.js          # then open http://localhost:8080 and click LAUNCH GAME
```

The status line under the plate says which of the three states you are in before
you click — native build ready, not compiled yet, or no host to ask (`file://`, or
the single-file build). In the last two cases the plate hands over the exact
commands to run instead of doing nothing.

Two endpoints back it, both loopback-only, since a machine serving this on a LAN
should not hand every client on it a process:

| | |
|---|---|
| `GET /api/native` | is it built, is it running, is there a display |
| `POST /api/native/launch` | spawn `cpp/build/erebus_native` (one at a time) |

The only thing a request can choose is a `mission`, and that is matched against the
`.cfg` files actually present under `cpp/content/missions/` rather than passed
through — the request picks which mission, never what to run. The game is spawned
detached, so it outlives the server; quitting it puts the plate back to READY on its
own.

### The launcher page

`launcher.html` is the whole thing as one page: the LAUNCH GAME plate, the browser
build it opens in place, and the install steps for the desktop build beside it. Open
it from a checkout, or publish it anywhere — it resolves the game as `game.html` next
to itself and falls back to `index.html`, so both work.

Its desktop-build panel is one button, and one press runs every step there is:

1. **Find the host.** Ports 8080, 8081, 8000, 3000 and 9000 are scanned at once, along
   with the port the page was served from, so there is nothing to configure.
2. **Install it** if `cpp/build/erebus_native` isn't there — `tools/install-native.sh`,
   with the build's own output streamed into the page, because minutes of silence and a
   hang look identical.
3. **Update it** if the sources are newer than the binary. That one asks first and names
   the file that changed: update and play, or play the old build.
4. **Open it.**

No step needs a second press. The one thing a page genuinely cannot do is start the host
in the first place — no page may run a process on your machine — so that step collapses
to a single line, copied to the clipboard the moment it's needed:

```
git clone https://github.com/colleenneug/Claude-01.git erebus && cd erebus && bash tools/install-native.sh --serve
```

That installs what's missing, builds the game, starts the host and opens the launcher
from it. Leave the original tab open and it picks itself back up: the page keeps
scanning while you paste, and the moment the host answers it carries on through install
and launch on its own. `node server/server.js --open` does the same opening on its own.

"Out of date" is a plain mtime comparison — the newest file under `cpp/src`,
`cpp/shaders`, `cpp/content` or `cpp/CMakeLists.txt` against the binary's own — so a
changed mission `.cfg` counts as much as changed C++, since both are copied next to the
binary at build time. An update is an incremental rebuild, usually a second or two.

Four endpoints back all of it, and they answer cross-origin requests because the
launcher can be published somewhere other than the host it asks. What guards them is the
socket, not the origin: every state-changing path is loopback only, so a request can
only ever start something on the machine it came from, the one process it can start is
this project's own binary, and the install runs the same script a person runs by hand —
nothing from the wire reaches a command line.

| | |
|---|---|
| `GET /api/native` | built, running, stale, installing, display |
| `POST /api/native/install` | run the install script (build only, never the game) |
| `GET /api/native/install/log?since=` | the install's output as it happens |
| `POST /api/native/launch` | spawn the binary |

Installing needs the four build dependencies. Where they are missing and installing
them would need a password the page cannot supply, the script stops with the one
`sudo` line to paste rather than hanging on a prompt nobody will see.

One command does the whole native install from a fresh checkout:

```
bash tools/install-native.sh           # deps, build, play
bash tools/install-native.sh --serve   # deps, build, then serve the browser build
```

### Keyboard focus in an embedded frame

A page inside an iframe — a preview pane, the launcher, an embed — receives no key
events at all until that frame has focus, which makes `PRESS ANY KEY TO SKIP` look
broken while the mouse still works. The game asks for focus on load and takes it back
on every pointer contact, and if it still doesn't have it a moment later it says
`CLICK ANYWHERE TO GIVE THIS WINDOW THE KEYBOARD` rather than leaving a dead keyboard
unexplained. The launcher goes further and forwards any key press that lands on it
into the game frame.

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

`dist/erebus-cradle.html` is the whole game inlined into one page — every
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

## The three doctrines

Your class decides your weapon, your field ability and a passive that changes how a
firefight plays out. There is no swapping in the field.

### ✦ BULWARK — *Aegis Doctrine*
**MAUL-12 breaching shotgun** — 8 pellets × 17 damage, 75 rpm, 6-round magazine.
Devastating inside ten metres, useless past thirty.
- **Field ability — Aegis Barrier:** 60 points of overshield that soaks the next wave.
- **Passive — Bulkhead Plating:** absorbs 22% of all incoming damage. The only doctrine
  that can stand in a corridor and trade.

### ◈ ORACLE — *Signal Doctrine*
**ARC LANCE induction rifle** — 26 damage, 320 rpm, 24-round magazine, and rounds
**pierce**: a bolt passes through its target and into whatever stood behind it.
- **Field ability — Systems Breach:** EMP pulse, stunning and damaging everything within
  thirteen metres.
- **Passive — Ghost In The Wire:** deepest magazine and the flattest recoil on the roster.

### ✵ WRAITH — *Umbral Doctrine*
**WHISPER suppressed carbine** — 15 damage, 640 rpm, 32-round magazine, **×3 on a head
shot**.
- **Field ability — Phase Step:** blink forward, briefly untargetable, next round primed
  for double damage.
- **Passive — Blindside:** fragile in the open, lethal from an angle nobody covered.

---

## The campaign

Sixteen missions, run end to end down the length of the ark, and connected three ways.

**Geographically** — the route is one continuous ship. Each mission starts at the near
edge of its own sector, where the previous one finished: docking collar → maintenance
spine → Junction 9 → aft run → habitat ring → greenhouse → medical → reactor antechamber
→ choir array → Deck Zero. Several sectors are fought twice — arrival, and then the
counter-attack — which is how the ark closes behind you.

**Mechanically** — rank, XP and the unlock chain persist on the character. Clearing a
mission unlocks the next; the campaign screen shows the whole route with what is cleared,
what is next and what is still locked.

**Narratively** — the comms beats run as one thread across all sixteen. CRADLE, Recovery
Division and Elias Voss talk over you throughout, and the argument they are having
resolves in the last mission.

Difficulty climbs on four axes at once — enemy count, which types appear, a health
multiplier and a damage multiplier:

| Mission | Hostiles | Health ×| Damage ×|
|---|---|---|---|
| 1 · Hard Dock | 3 | 1.00 | 1.00 |
| 4 · Counter-Attack | 11 | 1.22 | 1.15 |
| 7 · Under the Sunset | 15 | 1.44 | 1.30 |
| 10 · Ward Six | 18 | 1.67 | 1.45 |
| 13 · The Array | 21 | 1.89 | 1.60 |
| 15 · The Threshold | 26 | 2.04 | 1.70 |
| 16 · The Conductor | boss | 2.11 | 1.75 |

## Ammunition

Ammo kits are scattered through every sector — thirty-odd across the ark, two to five per
room, placed against the level's own collider list so none of them end up inside a wall.
Walk over one for two magazines' worth of reserve. They re-form about half a minute after
being taken, so a long fight cannot dry you out, and your reserve has a ceiling: at full
capacity a kit is left where it is rather than wasted.

## Dying

Every ordinary mission issues **three trauma harness charges**, shown as pips beside your
vitals. Lose your vitals and a charge brings you back at the sector entry a couple of
seconds later, at full health, with a magazine loaded and a brief grace period — the
hostiles you already killed stay dead. Spend all three and the mission is over.

**The Conductor issues none.** Deck Zero is the one mission with no respawn: the harness
meter is not even displayed, and losing your vitals there means taking the fight again
from the top.

## The Conductor

The final mission is a boss with a shield you cannot shoot off, because the shield is the
song. A health bar runs along the bottom of the screen with a segment per phase; while
the shield holds, the bar reads as hatched and unavailable and every round you put into
it does nothing.

To break it you solve a puzzle. Four resonance nodes stand around the dais, each with its
own colour and pitch. The Conductor sings a phrase across them — **play it back by
shooting those nodes in the same order**. Get it right and the shield drops long enough
to hurt it. Hit a wrong note and it starts the bar again, and takes a swing at you for
the interruption.

No harness charge is issued for this mission — see **Dying** above. There are four
phases, and each one is a longer phrase played faster: three notes, then four, then five,
then six. Miss your damage window and it re-shields with the harder
phrase anyway. It summons the Choir, and it opens up with a crescendo that fills the arena
whether the shield is up or not — the puzzle is meant to be solved under fire, not
standing still.

---

## Gear, rarity and power

Every cleared mission pays out salvage. Items roll on a five-tier ladder — **common,
uncommon, rare, epic, exotic** — and the tier sets both a flat stat multiplier and how
many affixes the item carries. The odds tilt with how deep you are: mission 1 rolls about
55% common and 1% exotic, mission 16 rolls 18% common and 13% exotic. The Conductor never
pays out junk.

Weapons multiply your doctrine's issued gun rather than replacing it — damage, magazine,
rate of fire, reload and spread — so a WRAITH always carries a WHISPER, but an exotic one
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

**Different enemies on each world.** The desert belongs to **The Reclaimed** — scarab
drones, marauders, and dust colossi. The ice belongs to **The Stilled** — frost motes,
revenants, and hoarfrost wardens. Nothing crosses over.

**Public events.** Every minute or so an event fires somewhere in the zone. It is
announced, it drops a beam marker you can see from across the map, the panel tracks its
timer and progress, and it runs whether or not you go to it:

| Event | What it is |
|---|---|
| **Site Breach** | Something comes up out of the ground in force. Clear it. |
| **High-Value Target** | An elite is in the open with an escort. Kill it. |
| **Relay Capture** | Hold ground near the relay while it charges and they keep coming. |

Finishing one pays salvage and XP on the spot — the reward rolls against the event's
difficulty, and elites roll higher. Failing one costs nothing but the time.

**With friends.** Everything above is shared: the host owns the population and the events
and broadcasts them, so your squad sees the same event marker, fights the same hostiles,
and everyone is paid when it completes.

## Three character slots## Three character slots

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
src/js/fps/ai.js          hostiles, steering, projectiles, hit spheres
src/js/fps/hud.js         crosshair, vitals, ammo, comms, kill feed, boss bar
src/js/fps/boss.js        the Conductor: shield phases and the phrase puzzle
src/js/fps/game.js        mission loop, objectives, player condition
src/js/ui.js              menus and the handoff into a mission
src/js/main.js            entry point
src/css/launch.css        the native LAUNCH GAME plate on the title screen
src/js/launch.js          asks the local host to start the C++ desktop build
launcher.html             standalone launcher page (plate, embedded game, install steps)
tools/install-native.sh   one-command native install: dependencies, build, run
server/server.js          static host, co-op relay, native launch endpoints
cpp/                      the native C++/OpenGL build (see cpp/README.md)
tools/bundle.py           inlines the above into one self-contained page
dist/erebus-cradle.html   the generated single-file build
```
