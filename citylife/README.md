# City Life

An open-world city sim in the browser, in the spirit of `gta07.base44.app`, but with its own
real accounts (username + password, hashed and stored server-side) instead of a hosted platform
login.

## Running it

```
node citylife/server.js          # http://localhost:8090
PORT=9000 node citylife/server.js
```

It's a single dependency-free Node process: it serves the game's static files, reuses the
three.js build already vendored at `vendor/three.min.js` for the FPS game in this repo, and
exposes a small JSON API for accounts. Accounts are stored in `citylife/data/db.json`
(created on first signup, gitignored — it's runtime state, not source).

Open the printed URL, sign up with a username/password, pick a role, and you're in.

## What's here

- **Real accounts.** Passwords are hashed with `scrypt` + a random salt (`server.js`), sessions
  are an HttpOnly cookie backed by a server-side token map. Log out and log back in anywhere and
  your money, role and businesses are exactly where you left them.
- **Roles.** Criminal, Superhero, or Super Villain, picked once at signup. Each has a different
  passive income rate and its own "Powers" (glide, dash, bigger heist payouts).
- **Open-world city.** A grid of blocky buildings and roads (three.js, no textures fetched at
  runtime — everything is generated), with a marked Bank and a few ownable Businesses.
- **Bank heist.** Walk up to the bank and press `E` to crack the safe (a timing minigame): land
  3 hits and the server pays out a random amount, then the bank goes on a 5-minute cooldown.
- **Businesses.** Press `F` near a business lot to buy it; each one adds to your passive
  income-per-second, shown ticking up in the HUD.
- **Vehicles.** A few drivable cars scattered near spawn — `V` to get in/out, `WASD` to drive.
- **NPCs.** Walk up and press `E` to talk; the line they give you depends on your role.
- **Fast travel.** `T` opens a menu to jump straight to the bank, spawn, or a business.
- **Leaderboard.** Top players by cash, pulled live from the server's account list.
- **Build mode.** Drop and remove colored blocks in the world for fun.

### Controls

| | |
|---|---|
| `WASD` | Move / drive |
| `Space` | Jump |
| `Space` + `W` into a wall | Climb it |
| `Shift` | Sprint |
| `F` | Buy the property you're standing near |
| `E` | Talk to an NPC / rob the bank |
| `V` | Enter or exit the nearest vehicle |
| `T` | Fast travel |
| `Q` | Dash (Super Villain) |
| Drag with the mouse | Orbit the camera |
| **1st Person** button | Toggle first/third person |

This is a separate, self-contained game from the FPS project at the repo root — it doesn't
touch `src/`, `server/`, or `index.html` there.
