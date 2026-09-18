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

}  // namespace

bool Station::init() {
  Builder b;
  const std::vector<Hole> noHoles;

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
  terminals_ = {
    {"armoury", "ARMOURY", "Salvage, parts and the bench.", {-37.0f, DECK_B, 2.0f}, {1.0f, 0.71f, 0.33f}},
    {"flight", "FLIGHT DECK", "The ark, and the ground below it.", {0.0f, DECK_A, 34.0f}, {0.37f, 0.92f, 1.0f}},
    {"airlock", "AIRLOCK", "Back to the ship.", {0.0f, DECK_A, 44.0f}, {0.49f, 1.0f, 0.61f}},
  };
  for (const Terminal& t : terminals_) {
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

  level_.buildFromParts(b.parts, DECK_A);
  player_.radius = 0.4f;
  player_.height = 1.8f;
  return true;
}

void Station::destroy() { level_.destroy(); }

void Station::enter(Camera& camera) {
  // At the arrivals end of the tube, facing down the concourse — you come in
  // through UNITY, which is what the module is for.
  player_.position = glm::vec3(0.0f, DECK_A, -48.0f);
  player_.velocity = glm::vec3(0.0f);
  player_.grounded = true;
  player_.hp = player_.maxHp;
  camera.yaw = 90.0f;      // +Z, down the spine
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
  player_.update(window, dt, glm::radians(camera.yaw), sprint, level_, scripted);
  camera.position = player_.eyePosition();
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
}
