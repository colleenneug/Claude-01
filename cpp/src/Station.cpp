#include "Station.h"
#include <algorithm>
#include <cmath>

namespace {

// Deck heights and the top of the concourse volume, from the browser build.
constexpr float DECK_A = 0.0f, DECK_B = 7.0f, DECK_C = 14.0f;
constexpr float CEIL = 22.0f;
// One stair step. Under Level::kStepHeight, so walking into a flight climbs
// it and no ramp special case is needed.
constexpr float RISE = 0.29f;

// The materials, as tints rather than textures — every surface in this
// project shades procedurally from world position and normal.
const glm::vec3 HULL(0.30f, 0.33f, 0.38f);
const glm::vec3 DECKM(0.24f, 0.26f, 0.30f);
const glm::vec3 GRATE(0.19f, 0.21f, 0.25f);
const glm::vec3 DARK(0.09f, 0.10f, 0.13f);
const glm::vec3 PIPE(0.38f, 0.40f, 0.45f);
const glm::vec3 PANE(0.05f, 0.07f, 0.11f);
const glm::vec3 PAINT(0.42f, 0.36f, 0.26f);
const glm::vec3 WARM(0.87f, 0.94f, 1.0f);
const glm::vec3 CYAN(0.37f, 0.92f, 1.0f);

struct Hole { float x0, x1, z0, z1; };

// A builder, so the layout below reads as rooms rather than as pushes into a
// vector. Everything is given as the extents it occupies, which is how the
// browser build writes it too: a station is a set of rooms, and a room is
// easier to write as the space it fills than as a centre plus a size.
struct Builder {
  std::vector<Level::Part> parts;

  void box(float x0, float x1, float y0, float y1, float z0, float z1,
           glm::vec3 tint, bool solid = true, float metallic = 0.5f, float rough = 0.5f) {
    Level::Part p;
    p.min = {std::min(x0, x1), std::min(y0, y1), std::min(z0, z1)};
    p.max = {std::max(x0, x1), std::max(y0, y1), std::max(z0, z1)};
    p.tint = tint;
    p.metallic = metallic;
    p.roughness = rough;
    p.wear = 0.7f;
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

  void plate(float x0, float x1, float z0, float z1, float y, glm::vec3 tint) {
    box(x0, x1, y - 0.4f, y, z0, z1, tint, true, 0.35f, 0.62f);
  }

  // A deck plate with rectangular holes cut in it: the atrium, and a well
  // over each stair flight. Cutting the wells matters more than it sounds —
  // a flight running under an unbroken deck stops you dead when your head
  // reaches the slab, about two thirds of the way up.
  //
  // Rather than special-casing shapes, cut the plate on every hole edge and
  // emit the cells no hole covers.
  void plateWith(float x0, float x1, float z0, float z1, float y,
                 const std::vector<Hole>& holes, glm::vec3 tint) {
    std::vector<float> xs{x0, x1}, zs{z0, z1};
    for (const Hole& h : holes) {
      for (float v : {h.x0, h.x1}) if (v > x0 && v < x1) xs.push_back(v);
      for (float v : {h.z0, h.z1}) if (v > z0 && v < z1) zs.push_back(v);
    }
    std::sort(xs.begin(), xs.end());
    std::sort(zs.begin(), zs.end());
    for (size_t i = 0; i + 1 < xs.size(); i++) {
      for (size_t j = 0; j + 1 < zs.size(); j++) {
        float ax = xs[i], bx = xs[i + 1], az = zs[j], bz = zs[j + 1];
        if (bx - ax < 0.01f || bz - az < 0.01f) continue;
        float cx = (ax + bx) * 0.5f, cz = (az + bz) * 0.5f;
        bool covered = false;
        for (const Hole& h : holes) {
          if (cx > h.x0 && cx < h.x1 && cz > h.z0 && cz < h.z1) { covered = true; break; }
        }
        if (covered) continue;
        plate(ax, bx, az, bz, y, tint);
      }
    }
  }

  // A railing along one edge — solid, because the whole point of a gallery is
  // that you can lean on the edge and not fall off it.
  void rail(float x0, float x1, float z0, float z1, float y) {
    box(x0, x1, y, y + 1.05f, z0, z1, PIPE, true, 0.8f, 0.3f);
  }

  // A flight of stairs as a run of steps. Each rise is under the player's
  // step height, so walking into it climbs it.
  void stairs(float x0, float x1, float zFrom, float zTo, float yFrom, float yTo) {
    int n = (int)std::lround((yTo - yFrom) / RISE);
    if (n <= 0) return;
    float dz = (zTo - zFrom) / (float)n;
    for (int i = 0; i < n; i++) {
      float y = yFrom + RISE * (float)(i + 1);
      float za = zFrom + dz * (float)i, zb = zFrom + dz * (float)(i + 1);
      box(x0, x1, y - RISE - 0.25f, y, std::min(za, zb), std::max(za, zb), DECKM, true, 0.35f, 0.62f);
    }
  }
};

// The Cradle: three decks around an open concourse, as described in
// Station.h. Everything in here was the body of init() before there were two
// places to stand.
void buildCradle(Builder& b, std::vector<Station::Terminal>& terminals) {
  const std::vector<Hole> noHoles;
  (void)noHoles;

  // ---------- deck A: concourse floor, arrivals, airlock, quartermaster bay
  b.plate(-20, 20, -26, 26, DECK_A, DECKM);
  b.plate(-11, 11, -54, -26, DECK_A, GRATE);      // UNITY, arrivals
  b.plate(-9, 9, 26, 46, DECK_A, GRATE);          // QUEST, the airlock
  b.plate(20, 42, -10, 14, DECK_A, GRATE);        // ZVEZDA side bay

  // ---------- deck B: a gallery ring around the open middle
  const std::vector<Hole> wellB = {
    {-13, 13, -19, 19},        // the atrium
    {-23, -15, -16, 8},        // the well over the port flight
    {15, 23, -8, 16},          // ...and the starboard one
  };
  b.plateWith(-28, 28, -34, 34, DECK_B, wellB, DECKM);
  b.plate(-46, -28, -8, 12, DECK_B, GRATE);       // COLUMBUS arm
  b.plate(28, 46, -8, 12, DECK_B, GRATE);         // KIBO arm

  // ---------- deck C: a smaller ring, and the cupola
  const std::vector<Hole> wellC = {
    {-13, 13, -19, 19},
    {-22, -15, 4, 28},
    {15, 22, -28, -4},
  };
  b.plateWith(-24, 24, -30, 30, DECK_C, wellC, DECKM);
  // The cupola hangs off the port side rather than off the bow, because a
  // cupola over another module is a window onto that module's roof. Out here
  // there is nothing below it at any deck, which is the entire specification
  // for this room.
  b.plate(-30, -24, -20, -14, DECK_C, GRATE);     // the neck you walk in along
  // ...and then the floor becomes the window: the cupola looks *down*.
  b.box(-43.4f, -30, DECK_C - 0.3f, DECK_C, -25.4f, -12.6f, PANE, true, 0.9f, 0.08f);
  // Mullions, so it reads as a built window and not a hole. Matte and dark:
  // a frame lit from behind by a lamp and in front by an environment map
  // turns into two white bars across the view.
  for (float x : {-39.9f, -36.7f, -33.5f})
    b.box(x - 0.12f, x + 0.12f, DECK_C - 0.32f, DECK_C + 0.06f, -25.4f, -12.6f, DARK, false, 0.15f, 0.9f);
  for (float z : {-22.2f, -19.0f, -15.8f})
    b.box(-43.4f, -30, DECK_C - 0.32f, DECK_C + 0.06f, z - 0.12f, z + 0.12f, DARK, false, 0.15f, 0.9f);

  // ---------- the hull around it all
  const float WALL = 1.2f;
  b.box(-30 - WALL, -30, DECK_A - 1, CEIL, -36, 36, HULL, true, 0.55f, 0.45f);
  b.box(30, 30 + WALL, DECK_A - 1, CEIL, -36, 36, HULL, true, 0.55f, 0.45f);
  b.box(-30, -10, DECK_A - 1, CEIL, 36, 36 + WALL, HULL, true, 0.55f, 0.45f);
  b.box(10, 30, DECK_A - 1, CEIL, 36, 36 + WALL, HULL, true, 0.55f, 0.45f);
  b.box(-10, 10, 7.0f, CEIL, 36, 36 + WALL, HULL, true, 0.55f, 0.45f);
  // The bow wall is in two pieces with the arrivals tube's mouth between
  // them. Built as one slab it seals the tube off, and you walk in from
  // UNITY and stop dead in the dark twelve metres short of the concourse.
  b.box(-30, -12, DECK_A - 1, CEIL, -36 - WALL, -36, HULL, true, 0.55f, 0.45f);
  b.box(12, 30, DECK_A - 1, CEIL, -36 - WALL, -36, HULL, true, 0.55f, 0.45f);
  b.box(-12, 12, 7.6f, CEIL, -36 - WALL, -36, HULL, true, 0.55f, 0.45f);
  b.box(-30, 30, CEIL, CEIL + 0.6f, -36, 36, DARK, false, 0.3f, 0.7f);   // roof, not a collider

  // arrivals tube
  b.box(-12, -11, DECK_A - 1, 7, -54, -26, HULL, true, 0.55f, 0.45f);
  b.box(11, 12, DECK_A - 1, 7, -54, -26, HULL, true, 0.55f, 0.45f);
  b.box(-12, 12, DECK_A - 1, 7, -55, -54, HULL, true, 0.55f, 0.45f);
  b.box(-12, 12, 7, 7.6f, -54, -26, DARK, false, 0.3f, 0.7f);

  // airlock tube
  b.box(-10, -9, DECK_A - 1, 6.5f, 26, 46, HULL, true, 0.55f, 0.45f);
  b.box(9, 10, DECK_A - 1, 6.5f, 26, 46, HULL, true, 0.55f, 0.45f);
  b.box(-10, 10, DECK_A - 1, 6.5f, 46, 47, HULL, true, 0.55f, 0.45f);
  b.box(-10, 10, 6.5f, 7, 26, 46, DARK, false, 0.3f, 0.7f);

  // Side bays and lab arms get walls too, so you cannot walk off into space.
  struct Bay { float x0, x1, z0, z1, y, h; };
  for (const Bay& bay : std::vector<Bay>{
         {20, 42, -10, 14, DECK_A, 6.5f},
         {-46, -28, -8, 12, DECK_B, 6.0f},
         {28, 46, -8, 12, DECK_B, 6.0f}}) {
    b.box(bay.x0, bay.x1, bay.y - 1, bay.y + bay.h, bay.z0 - 1, bay.z0, HULL, true, 0.55f, 0.45f);
    b.box(bay.x0, bay.x1, bay.y - 1, bay.y + bay.h, bay.z1, bay.z1 + 1, HULL, true, 0.55f, 0.45f);
    float far = bay.x0 > 0 ? bay.x1 : bay.x0;
    b.box(far - (bay.x0 > 0 ? 0.0f : 1.0f), far + (bay.x0 > 0 ? 1.0f : 0.0f),
          bay.y - 1, bay.y + bay.h, bay.z0, bay.z1, HULL, true, 0.55f, 0.45f);
    b.box(bay.x0, bay.x1, bay.y + bay.h, bay.y + bay.h + 0.5f, bay.z0, bay.z1, DARK, false, 0.3f, 0.7f);
  }

  // the cupola: a walled neck out from the ring, then the bay itself
  b.box(-30, -24, DECK_C - 1, DECK_C + 4.5f, -21, -20, HULL, true, 0.55f, 0.45f);
  b.box(-30, -24, DECK_C - 1, DECK_C + 4.5f, -14, -13, HULL, true, 0.55f, 0.45f);
  b.box(-45, -43.4f, DECK_C - 1, DECK_C + 5, -26.4f, -11.6f, HULL, true, 0.55f, 0.45f);
  b.box(-45, -29, DECK_C - 1, DECK_C + 5, -26.4f, -25.4f, HULL, true, 0.55f, 0.45f);
  b.box(-45, -29, DECK_C - 1, DECK_C + 5, -12.6f, -11.6f, HULL, true, 0.55f, 0.45f);
  b.box(-30, -29, DECK_C - 1, DECK_C + 5, -26.4f, -21, HULL, true, 0.55f, 0.45f);
  b.box(-30, -29, DECK_C - 1, DECK_C + 5, -13, -11.6f, HULL, true, 0.55f, 0.45f);
  // a rail at the lip where the grating stops and the glass starts
  b.box(-30.2f, -29.8f, DECK_C, DECK_C + 1.0f, -25.4f, -12.6f, DARK, true, 0.15f, 0.9f);

  // ---------- railings around every drop
  for (float y : {DECK_B, DECK_C}) {
    b.rail(-13, 13, -19.2f, -19, y);
    b.rail(-13, 13, 19, 19.2f, y);
    b.rail(-13.2f, -13, -19, 19, y);
    b.rail(13, 13.2f, -19, 19, y);
  }
  // the stair wells, on the two long sides — the ends are where you walk in
  b.rail(-23.2f, -23, -16, 8, DECK_B);
  b.rail(-15, -14.8f, -16, 8, DECK_B);
  b.rail(14.8f, 15, -8, 16, DECK_B);
  b.rail(23, 23.2f, -8, 16, DECK_B);
  b.rail(-22.2f, -22, 4, 28, DECK_C);
  b.rail(-15, -14.8f, 4, 28, DECK_C);
  b.rail(14.8f, 15, -28, -4, DECK_C);
  b.rail(22, 22.2f, -28, -4, DECK_C);
  // deck C outer edge, where it overhangs deck B
  b.rail(-24, 24, 29.8f, 30, DECK_C);
  b.rail(-24, 24, -30, -29.8f, DECK_C);
  b.rail(-24, -23.8f, -30, 30, DECK_C);
  b.rail(23.8f, 24, -30, 30, DECK_C);

  // ---------- stairs. Two flights up each side of the concourse, offset
  // front to back so the climb reads as a route through the room rather than
  // a ladder. Landings, so the top of a flight is a floor and not a lip.
  b.stairs(-22, -16, -14, 8, DECK_A, DECK_B);
  b.stairs(16, 22, 14, -8, DECK_A, DECK_B);
  b.stairs(-21, -16, 26, 4, DECK_B, DECK_C);
  b.stairs(16, 21, -26, -4, DECK_B, DECK_C);
  b.plate(-22, -16, 8, 12, DECK_B, DECKM);
  b.plate(16, 22, -12, -8, DECK_B, DECKM);
  b.plate(-21, -16, 0, 4, DECK_C, DECKM);
  b.plate(16, 21, -4, 0, DECK_C, DECKM);

  // ---------- the view: windows in the concourse walls
  for (float z : {-24.0f, -8.0f, 8.0f, 24.0f}) {
    for (int side = -1; side <= 1; side += 2) {
      float x = (float)side * 30.0f;
      b.box(x - 0.1f, x + 0.1f, DECK_B + 0.3f, DECK_B + 4.5f, z - 3.5f, z + 3.5f,
            PANE, false, 0.9f, 0.08f);
    }
  }

  // ---------- terminals. The things you actually came here to do, each a lit
  // kiosk you stand at, placed apart on purpose.
  // Only the airlock is an unattended kiosk now. The armoury and the flight
  // deck are people — Voss and Kaur stand at them (content/crew) — and a
  // terminal next to a person offering the same thing is two prompts for one
  // job.
  terminals = {
    {"airlock", "AIRLOCK", "Back to the ship.", {0.0f, DECK_A, 44.0f}, {0.49f, 1.0f, 0.61f}},
  };
  for (const Station::Terminal& t : terminals) {
    b.box(t.pos.x - 1.1f, t.pos.x + 1.1f, t.pos.y, t.pos.y + 1.0f, t.pos.z - 0.7f, t.pos.z + 0.7f,
          DARK, true, 0.4f, 0.55f);
    b.box(t.pos.x - 0.09f, t.pos.x + 0.09f, t.pos.y + 1.0f, t.pos.y + 1.7f, t.pos.z - 0.09f,
          t.pos.z + 0.09f, PIPE, false, 0.8f, 0.3f);
    b.lit(t.pos.x - 0.95f, t.pos.x + 0.95f, t.pos.y + 1.4f, t.pos.y + 2.0f,
          t.pos.z - 0.06f, t.pos.z + 0.06f, t.colour, 2.0f);
  }

  // ---------- dressing: crates and pipe runs, so the volumes read lived in
  const float spots[][3] = {
    {-6, DECK_A, -34}, {5, DECK_A, -38}, {-8, DECK_A, 30}, {26, DECK_A, 4},
    {30, DECK_A, -4},  {-34, DECK_B, 8}, {-40, DECK_B, -2}, {34, DECK_B, 8},
    {16, DECK_A, 18},  {-17, DECK_A, -20}, {20, DECK_C, 20}, {-19, DECK_C, -22},
  };
  int n = 0;
  for (const auto& s : spots) {
    // Deterministic heights rather than a random roll: the station has to
    // look the same every time you dock at it.
    float h = 0.7f + (float)((n++ * 37) % 8) * 0.1f;
    b.box(s[0] - 0.7f, s[0] + 0.7f, s[1], s[1] + h, s[2] - 0.7f, s[2] + 0.7f, PAINT, true, 0.3f, 0.7f);
  }

  // ---------- lighting the volume.
  // A room this size cannot be lit by point lights — this renderer has one
  // directional sun and a probe. So the station lights itself the way a real
  // one does, with strips: emissive geometry costs no light and reads as the
  // source, while the probe and the fill do the actual illuminating.
  for (int side = -1; side <= 1; side += 2) {
    float x = (float)side * 29.0f;
    b.lit(x - 0.6f, x + 0.6f, CEIL - 1.34f, CEIL - 1.2f, -34, 34, WARM, 3.0f);
    b.lit(x - 0.5f, x + 0.5f, DECK_B + 5.26f, DECK_B + 5.4f, -32, 32, WARM, 1.9f);
    b.lit(x - 0.5f, x + 0.5f, DECK_A + 5.46f, DECK_A + 5.6f, -24, 24, WARM, 1.9f);
  }
  // Deck-edge nosing, so every drop reads before you reach it.
  for (float y : {DECK_B, DECK_C}) {
    b.lit(-13, 13, y + 0.02f, y + 0.09f, -19.3f, -18.9f, CYAN, 2.2f);
    b.lit(-13, 13, y + 0.02f, y + 0.09f, 18.9f, 19.3f, CYAN, 2.2f);
    b.lit(-13.3f, -12.9f, y + 0.02f, y + 0.09f, -19, 19, CYAN, 2.2f);
    b.lit(12.9f, 13.3f, y + 0.02f, y + 0.09f, -19, 19, CYAN, 2.2f);
  }
  // The tube runs and the lab arms.
  b.lit(-0.6f, 0.6f, 6.63f, 6.77f, -53, -27, WARM, 2.6f);
  b.lit(-0.6f, 0.6f, 6.13f, 6.27f, 27, 45, WARM, 2.6f);
  b.lit(-45, -29, DECK_B + 5.26f, DECK_B + 5.4f, -0.5f, 0.5f, WARM, 2.4f);
  b.lit(29, 45, DECK_B + 5.26f, DECK_B + 5.4f, -0.5f, 0.5f, WARM, 2.4f);
  b.lit(21, 41, DECK_A + 5.76f, DECK_A + 5.9f, -0.5f, 0.5f, WARM, 2.4f);

}

// ============================================================
// KOUROU GROUND STATION — where the Strider programme actually happens.
//
// Not a smaller Cradle. The Cradle is a sealed can with strip light and no
// horizon; this is a shed on a launch site with one wall mostly open to the
// pan, and the difference is the point: the first campaign is spent here, and
// arriving in orbit later should feel like somewhere else.
//
// One hall with a mezzanine over its sides, laid out along +Z, which is the
// way you walk in from the pad:
//
//   z -32 .. -24   PAD DOOR     the way in, and the light source
//   z -24 ..   4   MUSTER FLOOR benches, the dispatch board, the programme desk
//   z   4 ..  20   COUNTERS     armoury on the port side, flight line starboard
//   z  20 ..  28   RANGE WALL   a window onto the qualification range
// ============================================================
void buildKourou(Builder& b, std::vector<Station::Terminal>& terminals) {
  const float FLOOR = 0.0f;
  const float MEZZ = 5.2f;           // the gallery over the side bays
  const float ROOF = 11.0f;
  const float HALFW = 26.0f;         // the hall is 52m across
  const float T = 0.5f;

  const glm::vec3 CONCRETE(0.40f, 0.39f, 0.36f);
  const glm::vec3 SLAB(0.215f, 0.215f, 0.205f);
  const glm::vec3 PANEL(0.43f, 0.44f, 0.46f);
  const glm::vec3 TRUSS(0.30f, 0.31f, 0.34f);
  const glm::vec3 BENCH(0.36f, 0.30f, 0.24f);
  const glm::vec3 SUNLIT(1.00f, 0.97f, 0.90f);

  // ---------------- the floor, and the apron outside the door
  b.plate(-HALFW, HALFW, -32, 28, FLOOR, SLAB);
  // Concrete running out through the door, so the doorway reads as an opening
  // onto somewhere rather than as a lit rectangle painted on a wall.
  b.box(-14, 14, FLOOR - 0.4f, FLOOR, -88, -32, CONCRETE, true, 0.08f, 0.88f);
  b.box(-400, 400, FLOOR - 0.9f, FLOOR - 0.4f, -400, 400,
        glm::vec3(0.35f, 0.33f, 0.28f), false, 0.05f, 0.92f);
  b.parts.back().castShadow = false;

  // ---------------- walls. The pad end is a doorway; everything else is shed.
  b.box(-HALFW - T, -HALFW, FLOOR - 0.4f, ROOF, -32 - T, 28 + T, PANEL, true, 0.35f, 0.62f);
  b.box(HALFW, HALFW + T, FLOOR - 0.4f, ROOF, -32 - T, 28 + T, PANEL, true, 0.35f, 0.62f);
  b.box(-HALFW - T, HALFW + T, FLOOR - 0.4f, ROOF, 28, 28 + T, PANEL, true, 0.35f, 0.62f);
  // The pad end, either side of an eight-metre opening, and a lintel over it.
  b.box(-HALFW - T, -8, FLOOR - 0.4f, ROOF, -32 - T, -32, PANEL, true, 0.35f, 0.62f);
  b.box(8, HALFW + T, FLOOR - 0.4f, ROOF, -32 - T, -32, PANEL, true, 0.35f, 0.62f);
  b.box(-8, 8, 6.5f, ROOF, -32 - T, -32, PANEL, true, 0.35f, 0.62f);
  b.box(-HALFW - T, HALFW + T, ROOF, ROOF + T, -32 - T, 28 + T, TRUSS, false, 0.4f, 0.6f);

  // Roof trusses, visible from the floor. A flat ceiling at this span reads
  // as a lid; the trusses are what give the volume a scale to read against.
  for (int i = 0; i < 11; i++) {
    float z = -30.0f + (float)i * 5.6f;
    b.box(-HALFW, HALFW, ROOF - 0.9f, ROOF - 0.55f, z - 0.22f, z + 0.22f, TRUSS, false, 0.5f, 0.5f);
  }

  // ---------------- the mezzanine, over the side bays only
  // Left as two galleries rather than a ring: the middle of the hall is where
  // the muster floor is, and a deck over it would put the whole room in
  // shadow from the one light that reaches in through the door.
  b.plate(-HALFW, -13, -8, 20, MEZZ, SLAB);
  b.plate(13, HALFW, -8, 20, MEZZ, SLAB);
  for (float x : {-13.0f, 13.0f}) {
    // Rail along the open edge.
    for (int i = 0; i < 15; i++) {
      float z = -8.0f + (float)i * 2.0f;
      b.box(x - 0.06f, x + 0.06f, MEZZ, MEZZ + 1.05f, z - 0.06f, z + 0.06f, TRUSS, false, 0.6f, 0.4f);
    }
    b.box(x - 0.08f, x + 0.08f, MEZZ + 1.0f, MEZZ + 1.1f, -8, 20, TRUSS, false, 0.6f, 0.4f);
  }

  // Stairs up to each gallery, against the side walls and facing inward.
  for (int side = -1; side <= 1; side += 2) {
    float x = (float)side * 20.0f;
    for (int i = 0; i < 18; i++) {
      float y = FLOOR + (float)i * RISE;
      float z = 21.5f - (float)i * 0.62f;
      b.box(x - 2.2f, x + 2.2f, y - 0.4f, y, z - 0.31f, z + 0.31f, TRUSS, true, 0.45f, 0.55f);
    }
  }

  // ---------------- muster floor: benches in rows, facing the board
  for (int r = 0; r < 4; r++) {
    float z = -20.0f + (float)r * 4.5f;
    for (int c = -1; c <= 1; c += 2) {
      float x = (float)c * 6.5f;
      b.box(x - 4.0f, x + 4.0f, FLOOR + 0.30f, FLOOR + 0.42f, z - 0.35f, z + 0.35f, BENCH, true, 0.1f, 0.8f);
      b.box(x - 4.0f, x + 4.0f, FLOOR, FLOOR + 0.30f, z - 0.10f, z + 0.10f, TRUSS, true, 0.5f, 0.5f);
    }
  }

  // ---------------- the range window, in the far wall
  b.box(-10, 10, 1.6f, 4.2f, 27.9f, 28.1f, glm::vec3(0.06f, 0.08f, 0.12f), false, 0.9f, 0.08f);
  for (float x : {-6.0f, -2.0f, 2.0f, 6.0f})
    b.box(x - 0.1f, x + 0.1f, 1.6f, 4.2f, 27.8f, 28.2f, TRUSS, false, 0.2f, 0.85f);

  // ---------------- light
  // Overhead strips down the hall, and a much brighter band on the floor and
  // the walls near the door where the sun actually reaches. The renderer has
  // one directional light, so "sunlight coming through the door" has to be
  // painted: an emissive patch on the concrete inside the opening.
  for (int i = 0; i < 9; i++) {
    float z = -28.0f + (float)i * 6.5f;
    b.lit(-1.6f, 1.6f, ROOF - 1.35f, ROOF - 1.15f, z - 2.4f, z + 2.4f, SUNLIT, 2.2f);
    b.lit(-18.0f, -15.0f, ROOF - 1.35f, ROOF - 1.15f, z - 2.4f, z + 2.4f, SUNLIT, 1.5f);
    b.lit(15.0f, 18.0f, ROOF - 1.35f, ROOF - 1.15f, z - 2.4f, z + 2.4f, SUNLIT, 1.5f);
  }
  b.lit(-7.6f, 7.6f, FLOOR + 0.01f, FLOOR + 0.03f, -32.0f, -25.0f,
        glm::vec3(1.00f, 0.94f, 0.80f), 0.22f);

  // Hazard stripe across the pad door, and the two exit markers.
  for (int i = 0; i < 8; i++) {
    float x = -7.5f + (float)i * 2.0f;
    b.lit(x, x + 1.0f, FLOOR + 0.02f, FLOOR + 0.04f, -25.4f, -24.6f,
          glm::vec3(1.0f, 0.72f, 0.20f), 1.1f);
  }
  b.lit(-1.6f, 1.6f, 6.6f, 6.9f, -32.05f, -31.95f, glm::vec3(0.45f, 1.0f, 0.60f), 2.0f);

  // ---------------- the one unattended kiosk. Everything else here is a
  // person; a terminal standing next to somebody who offers the same thing is
  // two prompts for one job.
  terminals = {
    {"pad", "THE PAD", "Out to the flight line.", {-9.5f, FLOOR, -27.0f}, {0.49f, 1.0f, 0.61f}},
  };
  for (const Station::Terminal& t : terminals) {
    b.box(t.pos.x - 1.1f, t.pos.x + 1.1f, t.pos.y, t.pos.y + 1.0f, t.pos.z - 0.7f, t.pos.z + 0.7f,
          TRUSS, true, 0.4f, 0.55f);
    b.lit(t.pos.x - 0.95f, t.pos.x + 0.95f, t.pos.y + 1.0f, t.pos.y + 1.12f,
          t.pos.z - 0.06f, t.pos.z + 0.06f, t.colour, 2.0f);
  }
}

// The crew's furniture is the hub's, not the crew's: a counter you can walk
// through is not a counter, and only what goes into the level's part list
// gets a collider. The people themselves are drawn by Crew.
void buildPosts(Builder& b, const Content& content, const std::string& station) {
  for (const std::string& id : content.crewIds(station)) {
    const CrewDef* def = content.crew(id);
    if (!def || !def->desk) continue;
    // Rotating an axis-aligned box would leave its collider describing
    // something wider than what you can see, so a post is placed on whichever
    // axis it faces and sized accordingly. Facings are quarter turns.
    float yaw = def->facingDegrees;
    bool alongZ = std::abs(std::fmod(std::abs(yaw), 180.0f)) < 45.0f;   // faces +/-Z
    float fx = -std::sin(glm::radians(yaw));   // the local -Z direction, in world
    float fz = -std::cos(glm::radians(yaw));
    glm::vec3 front = def->position + glm::vec3(fx, 0.0f, fz) * 1.3f;
    glm::vec3 half = alongZ ? glm::vec3(1.3f, 0.0f, 0.45f) : glm::vec3(0.45f, 0.0f, 1.3f);

    b.box(front.x - half.x, front.x + half.x, def->position.y, def->position.y + 1.05f,
          front.z - half.z, front.z + half.z, glm::vec3(0.19f, 0.21f, 0.25f), true, 0.75f, 0.45f);
    // A lit edge along the counter's front, not a lit tabletop: a whole
    // glowing surface at this size stops reading as a light and starts
    // reading as a slab of colour, and silhouettes whoever is behind it.
    glm::vec3 edge = front + glm::vec3(fx, 0.0f, fz) * (alongZ ? 0.40f : 0.40f);
    glm::vec3 eh = alongZ ? glm::vec3(half.x * 0.92f, 0.0f, 0.05f)
                          : glm::vec3(0.05f, 0.0f, half.z * 0.92f);
    b.lit(edge.x - eh.x, edge.x + eh.x, def->position.y + 1.02f, def->position.y + 1.08f,
          edge.z - eh.z, edge.z + eh.z, def->colour, 1.4f);

    if (def->board) {
      glm::vec3 back = def->position - glm::vec3(fx, 0.0f, fz) * 1.5f;
      glm::vec3 bh = alongZ ? glm::vec3(2.3f, 0.0f, 0.125f) : glm::vec3(0.125f, 0.0f, 2.3f);
      b.box(back.x - bh.x, back.x + bh.x, def->position.y + 0.3f, def->position.y + 3.3f,
            back.z - bh.z, back.z + bh.z, glm::vec3(0.11f, 0.13f, 0.16f), true, 0.6f, 0.6f);
      for (int r = 0; r < 2; r++) {
        for (int c = 0; c < 3; c++) {
          float off = ((float)c - 1.0f) * 1.42f;
          float y = def->position.y + 2.45f - (float)r * 1.3f;
          // Proud of the slab's face, not sharing its centre — sharing it
          // buries every slate inside the board.
          glm::vec3 at = back + glm::vec3(fx, 0.0f, fz) * 0.18f +
                         (alongZ ? glm::vec3(off, 0.0f, 0.0f) : glm::vec3(0.0f, 0.0f, off));
          glm::vec3 sh = alongZ ? glm::vec3(0.62f, 0.0f, 0.04f) : glm::vec3(0.04f, 0.0f, 0.62f);
          // Dim: six lit slates at full strength turn the whole post into a
          // wall of flat colour with a silhouette in front of it.
          b.lit(at.x - sh.x, at.x + sh.x, y - 0.57f, y + 0.57f, at.z - sh.z, at.z + sh.z,
                def->colour * 0.55f, 0.55f);
        }
      }
    }
  }

}

}  // namespace

bool Station::init(const Content& content, const std::string& layout) {
  layout_ = layout.empty() ? "cradle" : layout;
  Builder b;
  float floorY = DECK_A;

  if (layout_ == "kourou") {
    title_ = "KOUROU GROUND STATION";
    subtitle_ = "STRIDER PROGRAMME - MUSTER FLOOR";
    buildKourou(b, terminals_);
    // Daylight through a pad door, not strip light in a sealed can. The fill
    // is warm and much stronger than the Cradle's, because the far wall of
    // this room is a doorway onto a concrete pan in full sun and the bounce
    // off it is most of what lights the inside.
    spawn_ = glm::vec3(0.0f, 0.0f, -29.5f);
    spawnYaw_ = 90.0f;
    sunDir_ = glm::normalize(glm::vec3(-0.35f, -0.62f, 0.70f));
    sunColour_ = glm::vec3(1.00f, 0.96f, 0.88f);
    sunIntensity_ = 4.4f;
    fogDensity_ = 0.0022f;
    fogColour_ = glm::vec3(0.52f, 0.56f, 0.62f);
    clearColour_ = glm::vec3(0.36f, 0.45f, 0.58f);
    viewDistance_ = 600.0f;
    skyZenith_ = glm::vec3(0.16f, 0.30f, 0.62f);
    skyHorizon_ = glm::vec3(0.72f, 0.80f, 0.92f);
    skyIntensity_ = 1.0f;
    iblIntensity_ = 1.0f;
    ambientFill_ = glm::vec3(0.150f, 0.152f, 0.158f);
    floorY = 0.0f;
  } else {
    title_ = "THE CRADLE";
    subtitle_ = "";
    buildCradle(b, terminals_);
    spawn_ = glm::vec3(0.0f, 0.0f, -48.0f);
    spawnYaw_ = 90.0f;
  }

  buildPosts(b, content, layout_);

  level_.buildFromParts(b.parts, floorY);
  crew_.init(content, layout_);
  player_.radius = 0.4f;
  player_.height = 1.8f;
  return true;
}

void Station::destroy() { level_.destroy(); }

void Station::enter(Camera& camera) {
  // Wherever this layout's way in is: the Cradle's arrivals tube, or Kourou's
  // pad door. Both face down the length of the place, because the first thing
  // you should see on arriving somewhere is the whole of it.
  player_.position = spawn_;
  player_.velocity = glm::vec3(0.0f);
  player_.grounded = true;
  player_.hp = player_.maxHp;
  camera.yaw = spawnYaw_;
  camera.pitch = -2.0f;
  camera.position = player_.eyePosition();
}

void Station::placeAt(Camera& camera, glm::vec3 at, float yawDegrees) {
  player_.position = at;
  player_.velocity = glm::vec3(0.0f);
  player_.grounded = true;
  camera.yaw = yawDegrees;
  camera.pitch = 0.0f;
  camera.position = player_.eyePosition();
}

void Station::update(GLFWwindow* window, Camera& camera, float dt,
                     const ScriptedInput& scripted) {
  bool sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS;
  if (!inputFrozen_) player_.update(window, dt, glm::radians(camera.yaw), sprint, level_, scripted);
  camera.position = player_.eyePosition();
  crew_.update(dt);
}

const Crew::Person* Station::nearestPerson() const {
  return crew_.nearestPost(player_.position, reach_);
}

const Station::Terminal* Station::nearestTerminal() const {
  const Terminal* best = nullptr;
  float bestDist = reach_;
  for (const Terminal& t : terminals_) {
    // Distance to the kiosk's base, ignoring how tall you are relative to it.
    glm::vec2 d(player_.position.x - t.pos.x, player_.position.z - t.pos.z);
    float dist = glm::length(d);
    // ...but not across decks: the armoury is on B and the flight deck on A,
    // and standing above one is not standing at it.
    if (std::abs(player_.position.y - t.pos.y) > 2.0f) continue;
    if (dist < bestDist) { bestDist = dist; best = &t; }
  }
  return best;
}

void Station::collect(float time, std::vector<DrawItem>& out) const {
  (void)time;
  level_.collect(out);
  crew_.collect(out);
}
