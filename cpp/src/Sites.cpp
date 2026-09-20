#include "Site.h"
#include <algorithm>
#include <cmath>

// ============================================================
// KOUROU, BLOCK D — the crew quarters, and the pad outside them.
//
// Where a new record starts. You wake up in the bunks with nothing, because
// whatever you were going to be issued is in the armoury on the other side
// of the building, and something got into the block while you were asleep.
//
// The route is a route, not a checkpoint list: bunks, washroom corridor,
// armoury, muster hall, blast door, and then out onto the pad. Objectives
// change as you cross the building and nothing stops to congratulate you.
//
// Laid out along +Z, which is the direction you wake up facing:
//
//   z -34 .. -20   BUNKROOM      you, the bunks, a footlocker with a sidearm
//   z -20 ..  -6   CORRIDOR      washroom doors either side
//   z  -6 ..  10   ARMOURY       racks, and your issued weapon in one of them
//   z  10 ..  30   MUSTER HALL   tables, the briefing screen, the blast door
//   z  30 ..  80   THE PAD       outside: open ground and the range
// ============================================================

namespace {

// Materials, as tints — every surface in this project shades procedurally
// from world position and normal, so a "material" here is a colour and two
// numbers rather than a texture set.
const glm::vec3 WALL(0.27f, 0.29f, 0.33f);
const glm::vec3 FLOOR_IN(0.19f, 0.20f, 0.23f);
const glm::vec3 CEILING(0.13f, 0.14f, 0.17f);
const glm::vec3 BUNK(0.33f, 0.30f, 0.27f);
const glm::vec3 LOCKER(0.24f, 0.27f, 0.31f);
const glm::vec3 TABLE(0.30f, 0.26f, 0.22f);
const glm::vec3 RACK(0.22f, 0.24f, 0.28f);
const glm::vec3 CONCRETE(0.34f, 0.33f, 0.30f);
const glm::vec3 STRIP(0.88f, 0.93f, 1.00f);
const glm::vec3 ALARM(1.00f, 0.42f, 0.30f);
const glm::vec3 EXIT_SIGN(0.45f, 1.00f, 0.60f);
// Painted panel, not bare metal. A hull at metallic 0.7 with no strong probe
// on it goes black the moment it turns away from the sun, and every shot of
// this ship looks at its shaded side: the sun is off the nose and the ramp
// is at the tail.
const glm::vec3 HULL(0.44f, 0.46f, 0.50f);
const glm::vec3 HULL_DARK(0.25f, 0.26f, 0.29f);
const glm::vec3 ENGINE_GLOW(0.42f, 0.74f, 1.00f);
const glm::vec3 STROBE(1.00f, 0.32f, 0.26f);
const glm::vec3 HOLD_LIGHT(1.00f, 0.86f, 0.62f);

struct Builder {
  std::vector<Level::Part> parts;

  void box(float x0, float x1, float y0, float y1, float z0, float z1,
           glm::vec3 tint, bool solid = true, float metallic = 0.25f, float rough = 0.7f) {
    Level::Part p;
    p.min = {std::min(x0, x1), std::min(y0, y1), std::min(z0, z1)};
    p.max = {std::max(x0, x1), std::max(y0, y1), std::max(z0, z1)};
    p.tint = tint;
    p.metallic = metallic;
    p.roughness = rough;
    p.wear = 0.8f;
    p.solid = solid;
    parts.push_back(p);
  }

  void lit(float x0, float x1, float y0, float y1, float z0, float z1,
           glm::vec3 tint, float intensity) {
    Level::Part p;
    p.min = {std::min(x0, x1), std::min(y0, y1), std::min(z0, z1)};
    p.max = {std::max(x0, x1), std::max(y0, y1), std::max(z0, z1)};
    p.tint = tint;
    p.emissive = true;
    p.emissiveIntensity = intensity;
    p.solid = false;
    p.castShadow = false;
    parts.push_back(p);
  }

  // A room: floor, ceiling, and four walls with a gap left in whichever ends
  // the route runs through. Doorways are cut rather than modelled, for the
  // same reason the station's deck plates are cut around their wells — a wall
  // with a hole in it is two walls, and pretending otherwise means walking
  // into something you can see through.
  void room(float x0, float x1, float z0, float z1, float ceiling,
            bool openNorth, bool openSouth, float doorHalf = 1.6f) {
    const float T = 0.4f;
    box(x0, x1, -0.4f, 0.0f, z0, z1, FLOOR_IN, true, 0.2f, 0.8f);
    box(x0, x1, ceiling, ceiling + T, z0, z1, CEILING, false, 0.2f, 0.85f);
    box(x0 - T, x0, -0.4f, ceiling, z0 - T, z1 + T, WALL);
    box(x1, x1 + T, -0.4f, ceiling, z0 - T, z1 + T, WALL);

    auto endWall = [&](float z, bool open) {
      if (!open) {
        box(x0 - T, x1 + T, -0.4f, ceiling, z, z + T, WALL);
        return;
      }
      box(x0 - T, -doorHalf, -0.4f, ceiling, z, z + T, WALL);
      box(doorHalf, x1 + T, -0.4f, ceiling, z, z + T, WALL);
      // The lintel over the doorway, so a door reads as a door and not as a
      // slot the wall happens to stop at. High, deliberately: a player
      // standing on something knee-high next to a door is 2.25 metres tall,
      // and a realistic 2.1-metre lintel makes them too tall to walk through
      // their own armoury.
      box(-doorHalf, doorHalf, 2.6f, ceiling, z, z + T, WALL);
    };
    endWall(z0 - T, openNorth);
    endWall(z1, openSouth);
  }

  // A ceiling strip down the middle of a run.
  void strips(float z0, float z1, float ceiling, float spacing, glm::vec3 tint, float intensity) {
    for (float z = z0 + spacing * 0.5f; z < z1; z += spacing) {
      lit(-0.9f, 0.9f, ceiling - 0.14f, ceiling - 0.02f, z - 0.7f, z + 0.7f, tint, intensity);
    }
  }
};

void buildKourouBlockD(Site& s) {
  Builder b;
  const float CEIL = 3.4f;

  // ---------------- bunkroom ----------------
  b.room(-7, 7, -34, -20, CEIL, false, true);
  // Two rows of bunks against the side walls. Each is a frame, a lower bed
  // and an upper one — a single slab reads as a shelf.
  for (int side = -1; side <= 1; side += 2) {
    float x = (float)side * 5.0f;
    for (int i = 0; i < 4; i++) {
      float z = -32.0f + (float)i * 3.0f;
      b.box(x - 1.6f, x + 1.6f, 0.0f, 0.55f, z, z + 2.0f, BUNK, true, 0.15f, 0.8f);
      b.box(x - 1.6f, x + 1.6f, 1.45f, 1.75f, z, z + 2.0f, BUNK, true, 0.15f, 0.8f);
      b.box(x + (float)side * 1.45f, x + (float)side * 1.6f, 0.0f, 2.1f, z, z + 0.2f,
            LOCKER, true, 0.5f, 0.5f);
      b.box(x + (float)side * 1.45f, x + (float)side * 1.6f, 0.0f, 2.1f, z + 1.8f, z + 2.0f,
            LOCKER, true, 0.5f, 0.5f);
    }
  }
  // The footlocker, standing open, across the room from where you wake up —
  // far enough that getting to it is the first thing you do rather than
  // something that happens to you while the cutscene is still running.
  // Knee height on purpose: anything over Level::kStepHeight is a wall, and
  // a wall across the only lane out of the bunkroom is not a footlocker.
  b.box(-1.2f, 0.4f, 0.0f, 0.42f, -22.6f, -21.4f, LOCKER, true, 0.55f, 0.45f);
  // A lit rim round the open lid, not a lit lid: a whole glowing surface at
  // this size stops reading as a light and starts reading as furniture made
  // of light.
  b.lit(-1.2f, 0.4f, 0.42f, 0.46f, -22.62f, -22.5f, EXIT_SIGN, 0.7f);
  b.lit(-1.2f, 0.4f, 0.42f, 0.46f, -21.5f, -21.38f, EXIT_SIGN, 0.7f);

  // The alarm: the block's strips are dead and the emergency line is on.
  b.strips(-34, -20, CEIL, 4.5f, ALARM, 1.1f);

  // ---------------- corridor, washrooms either side ----------------
  b.room(-2.6f, 2.6f, -20, -6, CEIL, true, true);
  for (int side = -1; side <= 1; side += 2) {
    float x0 = side < 0 ? -9.0f : 2.6f, x1 = side < 0 ? -2.6f : 9.0f;
    b.box(x0, x1, -0.4f, 0.0f, -18.0f, -12.0f, FLOOR_IN, true, 0.2f, 0.8f);
    b.box(x0, x1, CEIL, CEIL + 0.4f, -18.0f, -12.0f, CEILING, false, 0.2f, 0.85f);
    float far = side < 0 ? x0 : x1;
    b.box(far - (side < 0 ? 0.4f : 0.0f), far + (side < 0 ? 0.0f : 0.4f),
          -0.4f, CEIL, -18.4f, -11.6f, WALL);
    b.box(x0, x1, -0.4f, CEIL, -18.4f, -18.0f, WALL);
    b.box(x0, x1, -0.4f, CEIL, -12.0f, -11.6f, WALL);
    // basins, so a washroom is a washroom
    for (int i = 0; i < 3; i++) {
      float z = -17.0f + (float)i * 1.8f;
      b.box(far - (side < 0 ? 0.0f : 0.9f), far + (side < 0 ? 0.9f : 0.0f),
            0.85f, 1.05f, z, z + 1.1f, LOCKER, true, 0.6f, 0.35f);
    }
  }
  b.strips(-20, -6, CEIL, 3.5f, ALARM, 1.1f);

  // ---------------- armoury ----------------
  b.room(-8, 8, -6, 10, CEIL, true, true, 1.8f);
  // Racks down both walls, and a bench across the back.
  for (int side = -1; side <= 1; side += 2) {
    float x = (float)side * 6.4f;
    for (int i = 0; i < 5; i++) {
      float z = -4.5f + (float)i * 2.8f;
      b.box(x - 0.9f, x + 0.9f, 0.0f, 2.3f, z, z + 0.35f, RACK, true, 0.65f, 0.4f);
      b.box(x - 0.9f, x + 0.9f, 1.05f, 1.15f, z + 0.35f, z + 1.6f, RACK, true, 0.65f, 0.4f);
    }
  }
  b.box(-5.0f, -2.0f, 0.0f, 0.95f, 7.6f, 9.0f, TABLE, true, 0.3f, 0.6f);
  b.box(2.0f, 5.0f, 0.0f, 0.95f, 7.6f, 9.0f, TABLE, true, 0.3f, 0.6f);
  // ...and the middle span is knee height, so the weapon on it is
  // something you step over rather than something you walk around.
  b.box(-2.0f, 2.0f, 0.0f, 0.42f, 7.6f, 9.0f, TABLE, true, 0.3f, 0.6f);
  // The bench is lit: it is where the one weapon still signed out is lying.
  b.lit(-1.6f, 1.6f, 0.42f, 0.46f, 7.7f, 8.9f, EXIT_SIGN, 1.5f);
  b.strips(-6, 10, CEIL, 4.0f, STRIP, 2.0f);

  // ---------------- muster hall ----------------
  b.room(-12, 12, 10, 30, 4.4f, true, true, 2.2f);
  // Three rows of tables with an aisle down the middle. A muster hall has an
  // aisle; a room spanned wall to wall by benches is a room you can only
  // cross by climbing over the furniture.
  for (int r = 0; r < 3; r++) {
    float z = 13.5f + (float)r * 5.0f;
    b.box(-6.0f, -1.6f, 0.0f, 0.9f, z, z + 1.3f, TABLE, true, 0.25f, 0.65f);
    b.box(1.6f, 6.0f, 0.0f, 0.9f, z, z + 1.3f, TABLE, true, 0.25f, 0.65f);
    for (float x : {-5.0f, -2.6f, 2.6f, 5.0f}) {
      b.box(x - 0.6f, x + 0.6f, 0.0f, 0.42f, z - 1.1f, z - 0.3f, TABLE, true, 0.25f, 0.65f);
    }
  }
  // The briefing screen on the side wall, and the exit sign over the door.
  b.lit(-11.6f, -11.5f, 1.6f, 3.4f, 16.0f, 23.0f, glm::vec3(0.35f, 0.62f, 0.9f), 0.9f);
  b.lit(-2.0f, 2.0f, 3.5f, 3.9f, 29.8f, 30.0f, EXIT_SIGN, 2.2f);
  b.strips(10, 30, 4.4f, 5.0f, STRIP, 2.2f);

  // ---------------- the pad ----------------
  // Outside. No ceiling, which is the whole point of it: the first sky in
  // the game, after four rooms of strip light.
  b.box(-46, 46, -0.4f, 0.0f, 30, 82, CONCRETE, true, 0.1f, 0.85f);
  b.box(-46, 46, -0.4f, 6.0f, 82, 82.6f, CONCRETE);
  b.box(-46.6f, -46, -0.4f, 6.0f, 30, 82.6f, CONCRETE);
  b.box(46, 46.6f, -0.4f, 6.0f, 30, 82.6f, CONCRETE);
  // The face of the block you just walked out of, either side of the door.
  b.box(-46, -2.2f, -0.4f, 7.0f, 29.6f, 30.0f, WALL);
  b.box(2.2f, 46, -0.4f, 7.0f, 29.6f, 30.0f, WALL);
  b.box(-2.2f, 2.2f, 4.4f, 7.0f, 29.6f, 30.0f, WALL);
  // Blast barriers on the pad: cover, and something to break a hundred
  // metres of flat concrete up.
  for (int i = 0; i < 6; i++) {
    float x = -30.0f + (float)i * 12.0f;
    float z = 44.0f + (float)((i * 7) % 5) * 5.0f;
    b.box(x - 3.2f, x + 3.2f, 0.0f, 1.5f, z, z + 0.8f, CONCRETE, true, 0.1f, 0.85f);
  }
  // Service pylons. Kept west of x = 16: the transport's pan starts at 22
  // and the row used to put a five-metre concrete post straight through its
  // port nacelle.
  for (int i = 0; i < 4; i++) {
    float x = -32.0f + (float)i * 16.0f;
    b.box(x - 1.0f, x + 1.0f, 0.0f, 5.0f, 70.0f, 72.0f, CONCRETE, true, 0.1f, 0.85f);
  }

  // Kourou, beyond the pan. The pad's perimeter wall used to be the edge of
  // the world: from anything above head height you could see the concrete
  // stop and blue sky start underneath it, and the closing shot — which
  // climbs — made a launch site look like a platform hanging in mid-air.
  // A ground plane out to four hundred metres and a scatter of buildings on
  // it cost two dozen boxes and fix both.
  b.box(-420, 420, -0.9f, -0.4f, -420, 440, glm::vec3(0.35f, 0.33f, 0.28f), false, 0.05f, 0.92f);
  // It is flat, it is under everything, and it is in every cascade: casting
  // from it buys nothing and only risks acne on itself.
  b.parts.back().castShadow = false;
  {
    // Hangars, assembly buildings and towers, out where you will never walk
    // to them. Deterministic placement: a launch site is laid out, not
    // scattered, and a seeded rand() here would differ between runs.
    const float far_[][5] = {
      // x, z, half-width, half-depth, height
      {-150.0f, 210.0f, 34.0f, 22.0f, 26.0f},
      { -78.0f, 300.0f, 20.0f, 20.0f, 14.0f},
      {  96.0f, 180.0f, 26.0f, 30.0f, 34.0f},
      { 190.0f, 260.0f, 40.0f, 24.0f, 18.0f},
      {-230.0f, 120.0f, 24.0f, 34.0f, 12.0f},
      { 150.0f,  20.0f, 18.0f, 18.0f, 22.0f},
      {-140.0f, -60.0f, 30.0f, 20.0f, 16.0f},
      {  60.0f, -90.0f, 22.0f, 26.0f, 28.0f},
    };
    for (const auto& g : far_) {
      b.box(g[0] - g[2], g[0] + g[2], -0.9f, g[4], g[1] - g[3], g[1] + g[3],
            glm::vec3(0.40f, 0.39f, 0.36f), false, 0.1f, 0.85f);
      // A service tower on the corner of each, and a hazard light on top of
      // it: at four hundred metres that red pinprick is the only thing that
      // says the rest of the site is still crewed.
      float tx = g[0] + g[2] * 0.7f, tz = g[1] - g[3] * 0.7f;
      b.box(tx - 2.2f, tx + 2.2f, -0.9f, g[4] + 16.0f, tz - 2.2f, tz + 2.2f,
            glm::vec3(0.33f, 0.33f, 0.31f), false, 0.2f, 0.8f);
      b.lit(tx - 1.2f, tx + 1.2f, g[4] + 16.0f, g[4] + 17.2f, tz - 1.2f, tz + 1.2f,
            glm::vec3(1.0f, 0.30f, 0.22f), 2.6f);
    }
  }

  // The transport, parked on the far pan with its ramp down and its hold
  // lit, from the moment you walk outside. It is not yours and you cannot
  // fly it — you do not have a ship yet, you have a pistol you found in a
  // footlocker — but it is the thing Division keeps saying is holding for
  // you, and it is visible across the whole fight so that the lift at the
  // end is something you have been walking toward rather than a surprise.
  //
  // Parked nose-out (+Z) off to the right of the pan, clear of the pylons,
  // so the walk out of the blast door puts it in frame without it standing
  // between you and anything you have to shoot.
  {
    const float CX = 30.0f;
    // Legs first: the hull floats 1.7m off the concrete and something has
    // to be holding it there.
    for (int sx = -1; sx <= 1; sx += 2) {
      for (int sz = -1; sz <= 1; sz += 2) {
        float x = CX + (float)sx * 2.6f;
        float z = 70.0f + (float)sz * 5.4f;
        b.box(x - 0.34f, x + 0.34f, 0.0f, 1.9f, z - 0.34f, z + 0.34f, HULL_DARK, true, 0.34f, 0.55f);
        b.box(x - 0.85f, x + 0.85f, 0.0f, 0.2f, z - 0.85f, z + 0.85f, HULL_DARK, true, 0.30f, 0.62f);
      }
    }
    // Fuselage, nose, dorsal spine, tail fin.
    b.box(CX - 3.0f, CX + 3.0f, 1.7f, 4.0f, 62.0f, 78.0f, HULL, true, 0.30f, 0.52f);
    b.box(CX - 2.2f, CX + 2.2f, 2.0f, 3.7f, 78.0f, 81.4f, HULL, true, 0.30f, 0.50f);
    b.box(CX - 2.4f, CX + 2.4f, 4.0f, 4.7f, 65.0f, 75.0f, HULL_DARK, true, 0.34f, 0.55f);
    b.box(CX - 0.4f, CX + 0.4f, 4.7f, 7.0f, 62.4f, 66.0f, HULL, true, 0.30f, 0.52f);
    // Nacelles on their pylons, and the bells facing back down the pad.
    for (int sx = -1; sx <= 1; sx += 2) {
      float in = CX + (float)sx * 3.0f;
      float out = CX + (float)sx * 7.8f;
      b.box(std::min(in, out), std::max(in, out), 2.5f, 3.2f, 68.0f, 74.0f, HULL_DARK, true, 0.34f, 0.55f);
      float n0 = CX + (float)sx * 5.0f, n1 = out;
      float a = std::min(n0, n1), c = std::max(n0, n1);
      b.box(a, c, 2.1f, 3.9f, 65.2f, 77.0f, HULL, true, 0.32f, 0.48f);
      // The bell: a dark housing with the glow set back inside it. An
      // emissive panel across the nacelle's whole end face does not read as
      // an engine, it reads as a lit screen bolted to the back of the wing,
      // which is exactly what the first pass looked like.
      b.box(a, c, 2.1f, 3.9f, 64.6f, 65.2f, HULL_DARK, true, 0.30f, 0.60f);
      b.lit(a + 0.75f, c - 0.75f, 2.65f, 3.35f, 64.74f, 64.86f, ENGINE_GLOW, 1.15f);
      // Wingtip strobe, and formation lights down the nacelle's outboard
      // side so the shaded half of the ship is not one black slab.
      b.lit(a + 0.4f, a + 0.9f, 3.9f, 4.1f, 70.0f, 70.9f, STROBE, 2.4f);
      float outer = (sx < 0) ? a : c;
      for (int k = 0; k < 3; k++) {
        float z = 67.5f + (float)k * 4.0f;
        b.lit(outer - 0.05f, outer + 0.05f, 2.9f, 3.1f, z, z + 1.2f,
              glm::vec3(0.70f, 0.84f, 1.00f), 0.8f);
      }
    }
    // Cabin windows down both flanks.
    for (int sx = -1; sx <= 1; sx += 2) {
      float x = CX + (float)sx * 3.0f;
      b.lit(x - 0.06f, x + 0.06f, 3.05f, 3.45f, 73.5f, 79.5f, glm::vec3(0.62f, 0.80f, 1.00f), 1.1f);
    }
    // The ramp, down on the concrete, and the hold lit behind it. This is
    // the shot at the end of the mission: you walk up that.
    b.box(CX - 2.2f, CX + 2.2f, 0.0f, 0.3f, 58.4f, 62.2f, HULL_DARK, true, 0.30f, 0.58f);
    // The hold behind it, and one strip lighting it. An emissive panel the
    // size of the whole opening does not read as light spilling out of a
    // hold — it reads as a white billboard nailed to the back of the ship,
    // which is exactly what it looked like the first time.
    // The opening itself glows faintly — a hold with the lights on, seen
    // from outside — with the strip that is doing the lighting bright above
    // it. A flat black rectangle here is a hole in the ship, not a door.
    b.lit(CX - 2.0f, CX + 2.0f, 0.3f, 2.5f, 61.9f, 62.1f, glm::vec3(0.52f, 0.43f, 0.33f), 0.32f);
    b.lit(CX - 1.7f, CX + 1.7f, 2.18f, 2.34f, 61.95f, 62.08f, HOLD_LIGHT, 1.8f);
    // Ramp lights either side of it, and a tail beacon. The sun is off the
    // nose, so every shot that looks at the ramp is looking at the ship's
    // shadow side: without these the beat where somebody reaches down for
    // you happens inside a black rectangle.
    for (int sx = -1; sx <= 1; sx += 2) {
      float x = CX + (float)sx * 1.9f;
      b.lit(x - 0.12f, x + 0.12f, 0.30f, 0.40f, 58.6f, 62.0f, HOLD_LIGHT, 1.4f);
    }
    b.lit(CX - 0.35f, CX + 0.35f, 6.6f, 6.9f, 62.5f, 63.0f, STROBE, 2.2f);
  }

  s.parts = std::move(b.parts);
  s.floorY = 0.0f;

  // You wake up in the second bunk on the left, facing the length of the
  // room — which is the way out.
  s.playerSpawn = glm::vec3(-1.2f, 0.0f, -26.0f);
  s.playerYaw = 90.0f;    // +Z, down the block

  s.triggers = {
    {"locker",   {-2.4f, -0.4f, -23.4f}, {1.8f, 2.2f, -20.4f}},
    {"corridor", {-2.6f, -0.4f, -19.5f}, {2.6f, 2.6f, -16.5f}},
    {"armoury",  {-8.0f, -0.4f, -5.5f},  {8.0f, 2.6f, -2.5f}},
    {"muster",   {-12.0f, -0.4f, 10.5f}, {12.0f, 3.6f, 13.5f}},
    {"outside",  {-8.0f, -0.4f, 30.5f},  {8.0f, 4.0f, 34.0f}},
  };

  // A sidearm in the footlocker, and your issued weapon in the lit rack.
  s.drops = {
    {"sidearm", {-0.4f, 0.60f, -22.0f}, "SIDEARM RECOVERED"},
    {"@issued", {0.0f, 0.60f, 8.3f}, "WEAPON RECOVERED"},
  };

  // Hostiles. Nothing is awake in the bunkroom — you get a weapon before
  // anything gets a shot at you — and each room's own wakes when you enter
  // it, so the building does not empty itself into the corridor behind you.
  s.hostiles = {
    {"drone", {-1.6f, 0.0f, -14.0f}, "locker"},
    {"drone", {1.8f, 0.0f, -9.0f}, "locker"},
    {"drone", {-5.5f, 0.0f, 1.0f}, "corridor"},
    {"thrall", {5.0f, 0.0f, 4.5f}, "corridor"},
    {"drone", {-8.0f, 0.0f, 19.0f}, "armoury"},
    {"thrall", {7.5f, 0.0f, 22.0f}, "armoury"},
    {"thrall", {0.0f, 0.0f, 26.0f}, "armoury"},
    {"drone", {-14.0f, 0.0f, 48.0f}, "muster"},
    {"drone", {16.0f, 0.0f, 52.0f}, "muster"},
    {"thrall", {-4.0f, 0.0f, 58.0f}, "muster"},
    {"thrall", {9.0f, 0.0f, 62.0f}, "muster"},
    {"warden", {0.0f, 0.0f, 70.0f}, "outside"},
  };

  s.objectives = {
    {"GET UP", "W A S D OR THE ARROW KEYS TO MOVE", "locker", false},
    {"GET OUT OF THE BLOCK", "SHIFT SPRINTS. CTRL OR C CROUCHES - AT SPEED THAT IS A SLIDE.", "corridor", false},
    {"REACH THE ARMOURY", "LEFT MOUSE FIRES. R RELOADS.", "armoury", false},
    {"CROSS THE MUSTER HALL", "Q OR E IS YOUR FIELD ABILITY.", "muster", false},
    {"GET TO THE PAD", "RIGHT MOUSE AIMS.", "outside", false},
    {"CLEAR THE PAD", "", "", true},
  };
}

}  // namespace

bool buildSite(const std::string& name, Site& out) {
  out = Site{};
  if (name == "kourou_block_d") {
    buildKourouBlockD(out);
    return true;
  }
  return false;
}
