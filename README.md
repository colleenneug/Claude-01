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

Ten missions, run end to end down the length of the ark, and connected three ways.

**Geographically** — the route is one continuous ship. Each mission starts at the near
edge of its own sector, where the previous one finished: docking collar → maintenance
spine → Junction 9 → aft run → habitat ring → greenhouse → medical → reactor antechamber
→ choir array → Deck Zero.

**Mechanically** — rank, XP and the unlock chain persist on the character. Clearing a
mission unlocks the next; the campaign screen shows the whole route with what is cleared,
what is next and what is still locked.

**Narratively** — the comms beats run as one thread across all ten. CRADLE, Recovery
Division and Elias Voss talk over you throughout, and the argument they are having
resolves in the last mission.

Difficulty climbs on four axes at once — enemy count, which types appear, a health
multiplier and a damage multiplier:

| Mission | Hostiles | Health ×| Damage ×|
|---|---|---|---|
| 1 · Hard Dock | 3 | 1.00 | 1.00 |
| 3 · Junction Nine | 7 | 1.23 | 1.16 |
| 5 · The False Sky | 10 | 1.46 | 1.31 |
| 7 · Triage | 13 | 1.69 | 1.47 |
| 9 · The Array | 17 | 1.92 | 1.62 |
| 10 · The Conductor | boss | 2.04 | 1.70 |

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

There are four phases, and each one is a longer phrase played faster: three notes, then
four, then five, then six. Miss your damage window and it re-shields with the harder
phrase anyway. It summons the Choir, and it opens up with a crescendo that fills the arena
whether the shield is up or not — the puzzle is meant to be solved under fire, not
standing still.

---

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
src/js/fps/campaign.js    the ten missions, escalation curve, unlock chain
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
tools/bundle.py           inlines the above into one self-contained page
dist/erebus-cradle.html   the generated single-file build
```
