# Baseball Masters 26

A full browser baseball game in plain JavaScript — meter pitching, PCI hitting,
card packs, and a season mode, in the spirit of *MLB The Show*. No build step,
no dependencies. Open `index.html` (or serve the folder) and play.

```
cd baseball-masters
python3 -m http.server 8000   # then visit http://localhost:8000
```

`file://` works too — everything is classic `<script>` tags.

## Features

- **8 fictional teams**, deterministically generated rosters (lineup, bench,
  4-man rotation, 4-man bullpen), each player rated 0–99 across contact,
  power, vision, discipline, speed, fielding, arm (hitters) or velocity,
  control, movement, stamina (pitchers), with an 8-pitch arsenal pool
  (four-seam, sinker, cutter, slider, curveball, changeup, splitter,
  knuckleball).
- **Real physics**: a drag + Magnus-lift ball-flight simulator calibrated
  against Statcast-style exit-velocity/launch-angle/distance references, and
  a geometric fielder-coverage model tuned to real-world ground-ball and
  fly-ball out rates. League-average simulated output lands close to modern
  MLB: ~.27 BA, ~9 runs/game, ~2.2 HR/game, ~21% K rate.
- **Meter pitching**: pick a pitch from the arsenal, aim inside (or around)
  the zone, then stop a sweeping accuracy meter — miss the sweet spot and
  the pitch drifts.
- **PCI hitting**: move a reticle onto the incoming ball (mouse, touch, or
  arrow keys) and time a Contact / Normal / Power / Bunt swing — location
  and timing both matter, independently.
- **5 difficulty tiers** (Rookie → Legend) that scale pitch flight time,
  the CPU's control/contact skill, and your own PCI/timing windows.
- **4 stadiums** with distinct fence distances by spray angle.
- **Card packs** (Standard / Premium / Diamond) bought with Stubs, pulling
  Common → Diamond tier player cards into a persistent collection
  (`localStorage`); build **My Team** from whatever you've pulled.
- **Home Run Derby**: 10 BP fastballs, chase the longest ball and the most
  home runs, bank Stubs.
- **Franchise mode**: pick a club, play a 14-game round-robin (live or
  simmed), then a one-game Pennant Series against the season's best
  challenger.
- Full box scores (line score, batting, pitching) after every game.

## Controls

**Pitching** — click a pitch button (or press `1`–`8`), aim with the mouse/
touch or arrow keys, click **Throw** / `Space` to start the meter, click /
`Space` again to lock it.

**Hitting** — move the reticle onto the ball, then swing: `Space` normal,
`Z` contact, `X` power, `C` bunt.

## Code layout

| File | Responsibility |
| --- | --- |
| `js/data.js` | Teams, rosters, pitch arsenal, stadiums, card tiers |
| `js/physics.js` | Ball flight (drag + Magnus lift), fielder pursuit math |
| `js/game.js` | Rules engine: counts, swings, batted-ball resolution, baserunning, box stats — pure logic, no DOM |
| `js/packs.js` | Save data, Stubs currency, pack odds, My Team assembly |
| `js/render.js` | Canvas drawing: overhead field, batter's-eye pitch tunnel |
| `js/input.js` | The live at-bat state machine (pitch → flight → swing → play) |
| `js/ui.js` | Menu, exhibition setup, collection, franchise screens |
| `js/main.js` | Bootstrap, HUD wiring, keyboard/mouse input, Derby mode |

`js/game.js` and `js/physics.js` have no DOM dependency and run standalone
under Node for testing/simulation (see `Game.prototype.simGame`).
