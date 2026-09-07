# DUSKBORNE

A browser-based, PvE first-person shooter — a **complete, separate game**
from *Erebus Cradle* (the repo's other browser build, in `../src` /
`../index.html`) and unrelated to the native C++ build in `../cpp`. All
three live in this repository; none of them build or run the others.

No build step, no assets, no accounts: open `index.html` and play.

```
open index.html          # macOS (xdg-open on Linux, double-click elsewhere)
# or serve it:
python3 -m http.server 8000    # then visit http://localhost:8000/duskborne/
```

The only dependency is the three.js UMD build already vendored at
`../vendor/three.min.js` (shared with Erebus Cradle) — nothing else is
fetched, not even a web font, so the game works from `file://` with no
network at all.

## What this is

An original setting built to the shape of a specific brief: a realistic
PvE shooter where the player aligns with a Darkness-equivalent cosmic
force and fights back against Light-wielding enemies. Nothing in it is
Bungie's Destiny 2 — there is no Guardian, no Witness, no Fallen, Hive,
Taken, Stasis, or Strand anywhere in this codebase. The setting is its
own: **the Hollow**, **the Unseen**, three peoples (Vek'thal, Cindrit,
the Warped), three classes (Bastion, Occultist, Wraithblade) each bound
to one of three Hollow disciplines (Rime, Weave, Umbral), and an enemy
faction of Light-wielding **Lumen Wardens** guarding **the Sunken
Throne**.

## Scope, honestly stated

Built against the brief's own flow list, and scoped to match its stated
status on each:

| Flow | Status here |
|---|---|
| Introductory tutorial | **Live.** A real mission — three Ember Wardens in a small chamber — teaching movement, the weapon, and the class ability. |
| The Sunken Throne questline | **Live.** Waves of Voltaic and Null Wardens culminating in a boss, the Radiant Vanguard, with a real two-stage phase change (adds summoned, enrage) at 66%/33% HP. |
| Bosses & gear progression | **Live.** Clearing either mission rolls gear on a four-tier rarity ladder (common → exotic) and raises a Power score, shown in the debrief and the hub. |
| Subclass unlock quests | **Partially live**, matching the brief exactly: your class's native Hollow discipline is playable from character creation; the other two are shown in the hub as locked — quests to unlock them aren't built yet. |
| The Hollow Anchorage (hub) | **Menu, not a walkable space.** Character summary, mission list, discipline status, and a "the Fleet" tab that's honest about what's still Planned rather than pretending to be a 3D social space it isn't. |
| Free roam, season pass, fireteam link | **Planned.** Shown as locked rows in the hub, not built. |

## Controls

| | |
|---|---|
| `W` `A` `S` `D` | Move |
| Mouse | Look |
| `Shift` | Sprint |
| `Ctrl` / `C` | Crouch |
| `Space` | Jump |
| Left click | Fire |
| `R` | Reload |
| `E` or `Q` | Hollow ability |
| `Esc` | Release the mouse (pauses) |

Click the ENGAGE prompt at the start of a mission to capture the
pointer — pointer lock has to follow a real click, so that gate exists
for the same reason Erebus Cradle's does.

## The three callings

Species and class are chosen independently at character creation, and
each combination is legitimate — the passive stacks with the class
kit rather than gating it.

- **Vek'thal** (Salvage Instinct, +10% ability regen) · **Cindrit**
  (Swarm Resilience, +10% vitals) · **the Warped** (Raw Communion, +10%
  ability damage)
- **Bastion** — GRAVEBREAKER heavy carbine, **Rime Ward** (overshield +
  a freezing AoE pulse), native discipline Rime, 20% damage resistance.
- **Occultist** — HOLLOW NEEDLER induction carbine, **Umbral Grasp** (a
  damage cone that leaves every target it hits draining), native
  discipline Umbral.
- **Wraithblade** — THORNEDGE suppressed SMG (3× headshot), **Weave
  Dash** (a twelve-metre blink that damages everything it crosses and
  leaves you briefly untouchable), native discipline Weave.

## Layout

```
index.html            markup and screen scaffolding
css/style.css          the luxury-brand UI: near-black, one gold accent, thin rules
js/util.js             clamp/lerp/toast/localStorage wrapper (degrades to session-only)
js/audio.js            procedural Web Audio sound bank — no audio assets
js/classes.js          species, classes, disciplines, weapon/ability stats
js/story.js            briefing and debrief copy
js/level.js            arena geometry, canvas-baked procedural materials, colliders
js/player.js           pointer-lock FPS controller: move/jump/sprint/crouch/collision
js/weapons.js          hitscan weapon + the ability cooldown timer
js/ai.js                Lumen Wardens: state machine, stuck-avoidance steering, boss phases
js/hud.js               DOM HUD: vitals, ability, ammo, crosshair, boss bar, kill feed
js/game.js              the mission loop: spawns, win/loss, ability effects, gear rolls
js/ui.js                screen flow, character creation, hub, save/load
js/main.js              renderer, render loop, input, pointer lock, pause
```

## Relationship to the rest of this repository

- **`../index.html` / `../src`** — *Erebus Cradle*, a different game
  entirely (its own setting, sixteen-mission campaign, co-op). Nothing
  is shared between the two beyond the vendored three.js build.
- **`../cpp`** — the native C++/OpenGL build, kept exactly as it was.
  Requires a compile step; this game exists so there's something
  playable with zero setup while that build waits for you to have a
  toolchain handy.
