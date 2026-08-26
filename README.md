# EREBUS CRADLE // Signal Lost

A cursor-driven, science-fiction narrative RPG that runs in a browser. No build step,
no dependencies, no network required — open `index.html` and play.

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
directly — there are no ES-module CORS problems and nothing to install.

### Single-file build

`dist/erebus-cradle.html` is the whole game inlined into one page — every
stylesheet and script, no external requests except the Google Fonts pair. Use it
when the game has to travel as a single file (hosting it somewhere, emailing it,
opening it off a USB stick). Regenerate it after changing anything under `src/`:

```
python3 tools/bundle.py
```

---

## The interface

The native mouse pointer is hidden across the whole app and replaced by a targeting
reticle that reports what it is over:

| Cursor state | Meaning |
|---|---|
| Cyan ring, small | Idle — nothing under the pointer |
| Amber, expanded, labelled | Over something interactive; the label names the action |
| Red | Destructive or dangerous (erasing a dossier, entering a fight) |
| Dimmed, no glow | Locked — the option exists but not for your doctrine |
| Filling arc | **Hold to commit.** Erasing a character requires a sustained press, not a click |

Supporting the reticle: a parallax starfield that warps on transitions, CRT scanlines and
sweep, a perspective grid floor, panel corner brackets, glitch-split titles, a typewriter
that renders every passage (click the text to skip it), and a fully procedural sound bank
— every bleep, hit and fanfare is synthesised in the Web Audio API at runtime, so there
are no audio assets in the repository.

---

## The three doctrines

Your class decides your four combat abilities, a passive that changes the rules of a
fight, and a field skill that opens routes through the ark nobody else can take. Options
gated to other doctrines stay **visible but locked**, so each playthrough shows you what
the other two would have done.

### ✦ BULWARK — *Aegis Doctrine*
Armour grown from the hull of a dead ship. 124 vitals, high MIGHT.
- **KINETIC SLAM** — heavy strike, 30% chance to STAGGER (target loses its next action)
- **AEGIS WALL** — big shield plus FORTIFY, halving whatever breaks through
- **RIOT PULSE** — hits every hostile and SUNDERS armour for 3 turns
- **LAST STAND** ★ — heal 25%, +30 shield, +50% damage for 2 turns
- *Passive — Bulkhead Plating:* all incoming damage reduced by 3
- *Field skill — FORCE:* opens doors by disagreeing with them

### ◈ ORACLE — *Signal Doctrine*
Wetware spliced to a dead god's switchboard. 94 vitals, high SYNC, deepest CORE pool.
- **OVERLOAD SPIKE** — pierces shields entirely
- **NANITE WEAVE** — heal and scrub one hostile status effect
- **SYSTEMS BREACH** — GLITCH a target (its damage halved) and siphon 4 CORE
- **RECURSIVE CASCADE** ★ — shield-piercing damage to everything, then OVERCLOCK
- *Passive — Ghost In The Wire:* +2 CORE regenerated every turn
- *Field skill — SYSTEMS:* rewrites locks, manifests and, once, an argument

### ✵ WRAITH — *Umbral Doctrine*
Nine-tenths of a person and all of a knife. 100 vitals, high GUILE.
- **VIBRO LUNGE** — fast strike, 25% critical
- **PHASE STEP** — EVASION (dodge the next attack) plus PRIMED (next strike auto-crits)
- **NEURO TOXIN** — stacking poison that ignores armour and shields
- **MARK & ERASE** ★ — doubled against wounded targets, outright lethal below 20%
- *Passive — Blindside:* your first strike in any engagement is a guaranteed critical
- *Field skill — STEALTH:* takes the route nobody is watching

---

## Three character slots

The crew registry holds exactly three bays, persisted to `localStorage`. Each dossier
carries its own name, doctrine, rank, vitals, field kit, story position and world flags,
and can be deployed, saved and erased independently. Storage failures (private windows,
blocked site data) degrade to a session-only game rather than crashing.

Erasing a bay is deliberately awkward: press and **hold** the ERASE control until the
cursor's arc completes.

Finishing the story closes that operative's file — they keep their rank and their record
of which endings they reached, and can be sent back aboard for another run.

---

## Combat

Turn-based, one action per turn, resource-managed:

- **CORE** is your energy. Abilities spend it; FOCUS ends your turn and restores 4.
- **Intent** — every hostile telegraphs its next action above its head before it takes it.
  The ark does not lie about what it is going to do.
- **Statuses** — STAGGER, SUNDER, GLITCH, TOXIN, EVASION, PRIMED, FORTIFY, RESOLVE,
  OVERCLOCK, EXPOSED, BLEED, SILENCED.
- **Field kit** — medgel, pulse charges and shield cells, usable from the HUD mid-fight.
- Multiple hostiles put the cursor into targeting mode: pick who takes the hit.
- Abilities are also bound to keys `1`–`4`, and `Space` focuses.
- Falling in a fight triggers your trauma harness rather than a game over — you come back
  up at reduced vitals and try again.

---

## Story

Four chapters, ~30 nodes, branching by doctrine and by choice, ending three ways:
**SEVER**, **DIVERT**, or **BURN**. Along the way: a botanist who has been barricaded in a
greenhouse for forty years, a shipmind that solved loneliness the way an engineer solves
loneliness, and a composition with 203,000 parts and no rests in it anywhere.

---

## Layout

```
index.html              markup and screen scaffolding
src/css/base.css        palette, cursor rig, ambience, shared chrome
src/css/screens.css     boot, title, registry, enlistment, codex
src/css/hud.css         in-mission HUD and narrative stage
src/css/combat.css      combat layer, enemies, abilities, feedback
src/js/util.js          helpers, toasts, modal
src/js/audio.js         procedural Web Audio sound bank
src/js/cursor.js        the reticle, hover states, hold-to-commit
src/js/fx.js            starfield, shake, floating numbers, typewriter
src/js/classes.js       the three doctrines, abilities, levelling
src/js/enemies.js       bestiary and intent rolls
src/js/storage.js       the three save slots
src/js/story.js         the narrative graph and codex
src/js/combat.js        turn engine
src/js/ui.js            screens, slots, enlistment, story driver
src/js/main.js          entry point
tools/bundle.py         inlines the above into one self-contained page
dist/erebus-cradle.html the generated single-file build
```
