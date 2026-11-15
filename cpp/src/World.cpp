#include "World.h"

#include "Content.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {

// Deterministic: the same mission id builds the same world on every machine.
struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 1u) {}
  uint32_t next() {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
  }
  float f() { return (float)(next() >> 8) / 16777216.0f; }
  float range(float a, float b) { return a + (b - a) * f(); }
  int pick(int n) { return n > 0 ? (int)(next() % (uint32_t)n) : 0; }
};

uint32_t hashString(const std::string& s) {
  uint32_t h = 2166136261u;
  for (char c : s) h = (h ^ (uint8_t)c) * 16777619u;
  return h;
}

// What a biome is made of.
struct Palette {
  glm::vec3 ground, ground2, rock, rock2, glow;
  bool glowFloor = false;      // fissures of light in the ground (lava, ice)
  float metallic = 0.15f, rough = 0.85f;
};

Palette paletteFor(const std::string& biome, const glm::vec3& floor) {
  Palette p;
  p.ground = floor;
  if (biome == "ice") {
    p.ground2 = {0.70f, 0.80f, 0.88f};
    p.rock = {0.55f, 0.66f, 0.78f};
    p.rock2 = {0.82f, 0.90f, 0.96f};
    p.glow = {0.45f, 0.85f, 1.0f};
    p.glowFloor = true;
    p.metallic = 0.25f;
    p.rough = 0.35f;
  } else if (biome == "ash") {
    p.ground2 = {0.16f, 0.13f, 0.12f};
    p.rock = {0.20f, 0.17f, 0.16f};
    p.rock2 = {0.34f, 0.24f, 0.18f};
    p.glow = {1.0f, 0.42f, 0.12f};
    p.glowFloor = true;
  } else if (biome == "earth") {
    p.ground2 = {0.24f, 0.25f, 0.27f};   // old road
    p.rock = {0.46f, 0.45f, 0.43f};      // concrete
    p.rock2 = {0.40f, 0.26f, 0.20f};     // brick and rust
    p.glow = {1.0f, 0.78f, 0.45f};
  } else {                               // dust, and anything unnamed
    p.ground2 = glm::mix(floor, glm::vec3(0.55f, 0.44f, 0.32f), 0.5f);
    p.rock = {0.46f, 0.36f, 0.28f};
    p.rock2 = {0.58f, 0.48f, 0.38f};
    p.glow = {1.0f, 0.70f, 0.35f};
  }
  return p;
}

struct Builder {
  World& w;
  Palette pal;
  Rng rng;

  void box(glm::vec3 lo, glm::vec3 hi, glm::vec3 tint, bool solid = true) {
    Level::Part p;
    p.min = glm::min(lo, hi);
    p.max = glm::max(lo, hi);
    p.tint = tint;
    p.metallic = pal.metallic;
    p.roughness = pal.rough;
    p.wear = 0.9f;
    p.solid = solid;
    w.parts.push_back(p);
  }

  void glow(glm::vec3 lo, glm::vec3 hi, float intensity = 2.2f) {
    Level::Part p;
    p.min = glm::min(lo, hi);
    p.max = glm::max(lo, hi);
    p.tint = pal.glow;
    p.emissive = true;
    p.emissiveIntensity = intensity;
    p.solid = false;
    p.castShadow = false;
    w.parts.push_back(p);
  }

  glm::vec3 tintVary(glm::vec3 t) { return t * rng.range(0.85f, 1.12f); }

  // A hill you can walk up: terraces 0.4 high (one step each), shrinking.
  void mound(float cx, float cz, float rx, float rz, float h) {
    const int layers = std::max(1, (int)std::round(h / 0.4f));
    const glm::vec3 t = tintVary(glm::mix(pal.ground, pal.rock, 0.35f));
    for (int i = 0; i < layers; i++) {
      const float k = 1.0f - 0.82f * (float)i / (float)layers;
      const float top = 0.4f * (float)(i + 1);
      box({cx - rx * k, 0.0f, cz - rz * k}, {cx + rx * k, top, cz + rz * k}, glm::mix(t, pal.rock, 0.25f * (float)i / (float)layers));
    }
  }

  // A stair up the side of something, from the ground to `h`, heading in
  // direction (dx, dz) away from (x, z).
  void ramp(float x, float z, float dx, float dz, float h, float width) {
    const int steps = std::max(1, (int)std::ceil(h / 0.4f));
    const float run = 1.1f;
    const glm::vec3 t = tintVary(pal.rock2);
    for (int i = 0; i < steps; i++) {
      const float top = std::min(h, 0.4f * (float)(steps - i));
      const float d0 = run * (float)i, d1 = run * (float)(i + 1);
      const float sx = -dz, sz = dx;   // across the stair
      glm::vec3 a(x + dx * d0 - sx * width * 0.5f, 0.0f, z + dz * d0 - sz * width * 0.5f);
      glm::vec3 b(x + dx * d1 + sx * width * 0.5f, top, z + dz * d1 + sz * width * 0.5f);
      box(a, b, t);
    }
  }

  void boulder(float x, float z, float s) {
    const float h = s * rng.range(0.5f, 1.4f);
    const glm::vec3 t = tintVary(rng.f() < 0.5f ? pal.rock : pal.rock2);
    box({x - s, 0.0f, z - s * rng.range(0.6f, 1.0f)}, {x + s, h, z + s * rng.range(0.6f, 1.0f)}, t);
    if (rng.f() < 0.35f) {
      const float s2 = s * 0.55f;
      box({x - s2, h, z - s2}, {x + s2, h + s2 * 1.2f, z + s2}, t * 0.92f);
    }
  }

  bool clear(float x, float z, float r) const {
    for (const glm::vec3& lz : w.landingZones) {
      if (glm::length(glm::vec2(x - lz.x, z - lz.z)) < r + 9.0f) return false;
    }
    return std::fabs(x) < w.half - r - 4.0f && std::fabs(z) < w.half - r - 4.0f;
  }

  // ---- region styles -----------------------------------------------------

  void mesas(const WorldRegion& g) {
    const int n = 3 + rng.pick(3);
    for (int i = 0; i < n; i++) {
      const float a = rng.range(0.0f, 6.2832f), d = rng.range(0.15f, 0.75f) * g.radius;
      const float x = g.centre.x + std::cos(a) * d, z = g.centre.z + std::sin(a) * d;
      const float sx = rng.range(7.0f, 14.0f), sz = rng.range(7.0f, 14.0f), h = rng.range(5.0f, 10.0f);
      if (!clear(x, z, std::max(sx, sz) + 12.0f)) continue;
      box({x - sx, 0.0f, z - sz}, {x + sx, h, z + sz}, tintVary(pal.rock));
      box({x - sx * 0.8f, h, z - sz * 0.8f}, {x + sx * 0.8f, h + 0.5f, z + sz * 0.8f}, tintVary(pal.rock2));
      // The way up: a stair on the side facing the middle of the world.
      const glm::vec2 out = glm::normalize(glm::vec2(-x, -z) + glm::vec2(0.001f));
      const float ex = std::fabs(out.x) > std::fabs(out.y) ? (out.x > 0 ? sx : -sx) : 0.0f;
      const float ez = std::fabs(out.x) > std::fabs(out.y) ? 0.0f : (out.y > 0 ? sz : -sz);
      const float dx = ex != 0.0f ? (ex > 0 ? 1.0f : -1.0f) : 0.0f, dz = ez != 0.0f ? (ez > 0 ? 1.0f : -1.0f) : 0.0f;
      ramp(x + ex, z + ez, dx, dz, h, 3.6f);
    }
    for (int i = 0; i < 10; i++) {
      const float a = rng.range(0.0f, 6.2832f), d = rng.range(0.0f, 1.0f) * g.radius;
      const float x = g.centre.x + std::cos(a) * d, z = g.centre.z + std::sin(a) * d;
      if (clear(x, z, 3.0f)) boulder(x, z, rng.range(0.8f, 2.2f));
    }
  }

  void canyon(const WorldRegion& g) {
    const float a = rng.range(0.0f, 3.1416f);
    const glm::vec2 along(std::cos(a), std::sin(a)), across(-along.y, along.x);
    const float gap = rng.range(12.0f, 18.0f), len = g.radius * 1.7f;
    for (int side = -1; side <= 1; side += 2) {
      const int segs = 6;
      for (int k = 0; k < segs; k++) {
        const float t0 = -len * 0.5f + len * (float)k / segs, t1 = t0 + len / segs + 1.0f;
        const float off = gap * 0.5f + rng.range(0.0f, 4.0f), thick = rng.range(5.0f, 9.0f);
        const float h = rng.range(8.0f, 16.0f);
        const glm::vec2 c0 = glm::vec2(g.centre.x, g.centre.z) + along * t0 + across * (off * (float)side);
        const glm::vec2 c1 = glm::vec2(g.centre.x, g.centre.z) + along * t1 + across * ((off + thick) * (float)side);
        if (!clear((c0.x + c1.x) * 0.5f, (c0.y + c1.y) * 0.5f, 6.0f)) continue;
        box({c0.x, 0.0f, c0.y}, {c1.x, h, c1.y}, tintVary(pal.rock));
        // A ledge part way up, reached by a mound against the wall.
        if (rng.f() < 0.4f) {
          const glm::vec2 m = (c0 + c1) * 0.5f - across * (thick * 0.5f + 4.0f) * (float)side;
          mound(m.x, m.y, 4.0f, 4.0f, 2.4f);
        }
      }
    }
    for (int i = 0; i < 8; i++) {
      const float t = rng.range(-0.45f, 0.45f) * len;
      const glm::vec2 p = glm::vec2(g.centre.x, g.centre.z) + along * t + across * rng.range(-gap * 0.3f, gap * 0.3f);
      if (clear(p.x, p.y, 2.0f)) boulder(p.x, p.y, rng.range(0.7f, 1.6f));
    }
  }

  void boulders(const WorldRegion& g) {
    for (int i = 0; i < 34; i++) {
      const float a = rng.range(0.0f, 6.2832f), d = std::sqrt(rng.f()) * g.radius;
      const float x = g.centre.x + std::cos(a) * d, z = g.centre.z + std::sin(a) * d;
      if (clear(x, z, 4.0f)) boulder(x, z, rng.range(0.8f, 3.4f));
    }
    for (int i = 0; i < 3; i++) {
      const float a = rng.range(0.0f, 6.2832f), d = rng.range(0.2f, 0.8f) * g.radius;
      const float x = g.centre.x + std::cos(a) * d, z = g.centre.z + std::sin(a) * d;
      if (clear(x, z, 10.0f)) mound(x, z, rng.range(6.0f, 10.0f), rng.range(6.0f, 10.0f), rng.range(1.6f, 3.2f));
    }
  }

  void ruins(const WorldRegion& g) {
    const glm::vec3 wall = pal.rock, brick = pal.rock2;
    const int n = 5 + rng.pick(3);
    for (int i = 0; i < n; i++) {
      const float a = rng.range(0.0f, 6.2832f), d = rng.range(0.1f, 0.8f) * g.radius;
      const float x = g.centre.x + std::cos(a) * d, z = g.centre.z + std::sin(a) * d;
      const float hx = rng.range(5.0f, 9.0f), hz = rng.range(5.0f, 9.0f), T = 0.6f;
      if (!clear(x, z, std::max(hx, hz) + 2.0f)) continue;
      const glm::vec3 t = tintVary(rng.f() < 0.5f ? wall : brick);
      // Four broken walls, each in three pieces of different heights with a
      // doorway's gap somewhere.
      for (int sideI = 0; sideI < 4; sideI++) {
        const int door = rng.pick(3);
        for (int k = 0; k < 3; k++) {
          if (k == door) continue;
          const float h = rng.range(2.0f, 7.0f);
          const float u0 = -1.0f + 2.0f * (float)k / 3.0f, u1 = u0 + 2.0f / 3.0f;
          if (sideI < 2) {
            const float zz = sideI == 0 ? z - hz : z + hz;
            box({x + hx * u0, 0.0f, zz - T * 0.5f}, {x + hx * u1, h, zz + T * 0.5f}, t);
          } else {
            const float xx = sideI == 2 ? x - hx : x + hx;
            box({xx - T * 0.5f, 0.0f, z + hz * u0}, {xx + T * 0.5f, h, z + hz * u1}, t);
          }
        }
      }
      // Rubble inside, low enough to climb.
      box({x - hx * 0.4f, 0.0f, z - hz * 0.3f}, {x + hx * 0.1f, 0.4f, z + hz * 0.2f}, t * 0.8f);
      box({x - hx * 0.3f, 0.0f, z - hz * 0.2f}, {x - hx * 0.05f, 0.8f, z + hz * 0.05f}, t * 0.75f);
    }
    // Wrecks and dead lamps along an old road.
    const float ra = rng.range(0.0f, 3.1416f);
    const glm::vec2 along(std::cos(ra), std::sin(ra));
    for (int i = 0; i < 6; i++) {
      const float t = rng.range(-0.8f, 0.8f) * g.radius;
      const glm::vec2 p = glm::vec2(g.centre.x, g.centre.z) + along * t + glm::vec2(-along.y, along.x) * rng.range(-5.0f, 5.0f);
      if (!clear(p.x, p.y, 3.0f)) continue;
      if (rng.f() < 0.6f) {
        box({p.x - 2.1f, 0.0f, p.y - 1.0f}, {p.x + 2.1f, 1.3f, p.y + 1.0f}, tintVary(glm::vec3(0.30f, 0.20f, 0.16f)));
      } else {
        box({p.x - 0.15f, 0.0f, p.y - 0.15f}, {p.x + 0.15f, 5.5f, p.y + 0.15f}, glm::vec3(0.22f));
        glow({p.x - 0.35f, 5.3f, p.y - 0.35f}, {p.x + 0.35f, 5.6f, p.y + 0.35f}, 1.6f);
      }
    }
  }

  void crater(const WorldRegion& g) {
    const float r = g.radius * 0.6f;
    const int n = 14;
    for (int i = 0; i < n; i++) {
      const float a = 6.2832f * (float)i / n + rng.range(-0.1f, 0.1f);
      const float x = g.centre.x + std::cos(a) * r, z = g.centre.z + std::sin(a) * r;
      if (clear(x, z, 6.0f)) mound(x, z, rng.range(5.0f, 8.0f), rng.range(5.0f, 8.0f), rng.range(2.0f, 4.4f));
    }
    // What came down: a glowing core and its broken shell.
    glow({g.centre.x - 3.0f, 0.0f, g.centre.z - 3.0f}, {g.centre.x + 3.0f, 0.15f, g.centre.z + 3.0f}, 3.0f);
    for (int i = 0; i < 6; i++) {
      const float a = rng.range(0.0f, 6.2832f), d = rng.range(4.0f, 9.0f);
      boulder(g.centre.x + std::cos(a) * d, g.centre.z + std::sin(a) * d, rng.range(0.8f, 2.0f));
    }
  }

  void spires(const WorldRegion& g) {
    for (int i = 0; i < 16; i++) {
      const float a = rng.range(0.0f, 6.2832f), d = std::sqrt(rng.f()) * g.radius;
      const float x = g.centre.x + std::cos(a) * d, z = g.centre.z + std::sin(a) * d;
      const float s = rng.range(1.0f, 2.6f), h = rng.range(8.0f, 24.0f);
      if (!clear(x, z, s + 2.0f)) continue;
      box({x - s, 0.0f, z - s}, {x + s, h, z + s}, tintVary(rng.f() < 0.5f ? pal.rock : pal.rock2));
      box({x - s * 0.6f, h, z - s * 0.6f}, {x + s * 0.6f, h + s * 1.5f, z + s * 0.6f}, tintVary(pal.rock2));
      // Crystals, or embers, at the foot.
      if (rng.f() < 0.6f)
        glow({x + s, 0.0f, z - 0.3f}, {x + s + 0.6f, rng.range(0.6f, 1.6f), z + 0.3f}, 2.6f);
    }
  }

  void dunes(const WorldRegion& g) {
    for (int i = 0; i < 7; i++) {
      const float a = rng.range(0.0f, 6.2832f), d = rng.range(0.0f, 0.85f) * g.radius;
      const float x = g.centre.x + std::cos(a) * d, z = g.centre.z + std::sin(a) * d;
      const bool longX = rng.f() < 0.5f;
      const float rl = rng.range(12.0f, 22.0f), rs = rng.range(4.0f, 7.0f);
      if (clear(x, z, rl)) mound(x, z, longX ? rl : rs, longX ? rs : rl, rng.range(1.2f, 3.2f));
    }
  }

  // Lines of light in the ground: lava on an ash world, meltwater under ice.
  void fissures(const WorldRegion& g) {
    for (int k = 0; k < 4; k++) {
      glm::vec2 p(g.centre.x + rng.range(-0.5f, 0.5f) * g.radius, g.centre.z + rng.range(-0.5f, 0.5f) * g.radius);
      float a = rng.range(0.0f, 6.2832f);
      for (int i = 0; i < 7; i++) {
        const glm::vec2 q = p + glm::vec2(std::cos(a), std::sin(a)) * rng.range(5.0f, 9.0f);
        const glm::vec2 lo = glm::min(p, q) - glm::vec2(0.5f), hi = glm::max(p, q) + glm::vec2(0.5f);
        if (clear((lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, 1.0f)) glow({lo.x, 0.0f, lo.y}, {hi.x, 0.06f, hi.y}, 2.4f);
        p = q;
        a += rng.range(-0.8f, 0.8f);
      }
    }
    boulders(WorldRegion{g.name, g.style, g.centre, g.radius * 0.7f});
  }

  void region(const WorldRegion& g) {
    // The ground of the region, a shade apart from the plain around it.
    box({g.centre.x - g.radius, -0.02f, g.centre.z - g.radius}, {g.centre.x + g.radius, 0.0f, g.centre.z + g.radius},
        tintVary(pal.ground2), false);
    if (g.style == "mesas") mesas(g);
    else if (g.style == "canyon") canyon(g);
    else if (g.style == "boulders") boulders(g);
    else if (g.style == "ruins") ruins(g);
    else if (g.style == "crater") crater(g);
    else if (g.style == "spires") spires(g);
    else if (g.style == "dunes") dunes(g);
    else if (g.style == "fissures") fissures(g);
    else boulders(g);
  }
};

std::vector<std::pair<std::string, std::string>> defaultRegions(const std::string& biome) {
  if (biome == "ice")
    return {{"spires", "THE NEEDLES"}, {"crater", "STARFALL BASIN"}, {"canyon", "THE BLUE CUT"},
            {"fissures", "MELTWATER FLATS"}, {"mesas", "THE SHELVES"}};
  if (biome == "ash")
    return {{"fissures", "THE BURNING FLATS"}, {"mesas", "CINDER STAIR"}, {"canyon", "THE FURNACE ROAD"},
            {"spires", "SMOKESTACKS"}, {"crater", "THE BLAST SCAR"}};
  if (biome == "earth")
    return {{"ruins", "OLD TOWN"}, {"ruins", "THE CAR PARK"}, {"boulders", "THE CUTTING"},
            {"mesas", "THE OVERPASS"}, {"crater", "WHERE THE FIRST ONE LANDED"}};
  return {{"dunes", "THE DRIFTS"}, {"mesas", "THE TABLES"}, {"canyon", "DRY RIVER"}, {"boulders", "THE SCREE"},
          {"crater", "THE BOWL"}};
}

}  // namespace

void buildWorld(const MissionDef& m, World& out) {
  out = World{};
  out.half = std::max(120.0f, m.worldHalf);
  Builder b{out, paletteFor(m.biome, m.floorColour), Rng(hashString(m.id) ^ 0x51ed270bu)};
  const float H = out.half;

  // The ground, under everything.
  b.box({-H - 20.0f, -0.4f, -H - 20.0f}, {H + 20.0f, 0.0f, H + 20.0f}, m.floorColour);

  // The landing ground in the middle, and the zones you come down in.
  WorldRegion hub;
  hub.name = m.landingName.empty() ? std::string("THE LANDING") : m.landingName;
  hub.style = "landing";
  hub.radius = H * 0.2f;
  out.regions.push_back(hub);
  out.landingZones.push_back({0.0f, 0.0f, 0.0f});
  for (int k = 0; k < 4; k++) {
    const float a = 0.785f + 1.5708f * (float)k;
    out.landingZones.push_back({std::cos(a) * H * 0.62f, 0.0f, std::sin(a) * H * 0.62f});
  }
  for (const glm::vec3& lz : out.landingZones) {
    b.box({lz.x - 4.0f, 0.0f, lz.z - 4.0f}, {lz.x + 4.0f, 0.12f, lz.z + 4.0f}, glm::vec3(0.20f, 0.22f, 0.26f));
    b.glow({lz.x - 4.2f, 0.0f, lz.z - 4.2f}, {lz.x + 4.2f, 0.14f, lz.z - 3.9f}, 2.0f);
    b.glow({lz.x - 4.2f, 0.0f, lz.z + 3.9f}, {lz.x + 4.2f, 0.14f, lz.z + 4.2f}, 2.0f);
  }

  // The named regions, in a ring round the middle.
  std::vector<std::pair<std::string, std::string>> list = m.regions;
  if (list.empty()) list = defaultRegions(m.biome);
  const int n = (int)list.size();
  for (int i = 0; i < n; i++) {
    WorldRegion g;
    g.style = list[(size_t)i].first;
    g.name = list[(size_t)i].second;
    const float a = 6.2832f * (float)i / (float)n + b.rng.range(-0.15f, 0.15f);
    const float d = H * b.rng.range(0.50f, 0.66f);
    g.centre = {std::cos(a) * d, 0.0f, std::sin(a) * d};
    g.radius = H * (n > 5 ? 0.24f : 0.28f);
    out.regions.push_back(g);
    b.region(g);
  }
  // Between the regions, the odd rock, so the plain is never quite empty.
  for (int i = 0; i < 40; i++) {
    const float x = b.rng.range(-H, H), z = b.rng.range(-H, H);
    bool inRegion = false;
    for (size_t r = 1; r < out.regions.size(); r++)
      inRegion |= glm::length(glm::vec2(x - out.regions[r].centre.x, z - out.regions[r].centre.z)) < out.regions[r].radius;
    if (!inRegion && b.clear(x, z, 3.0f)) b.boulder(x, z, b.rng.range(0.6f, 2.0f));
  }

  // Cliffs all the way round: the edge of the world is a wall you can see.
  const int segs = 18;
  for (int side = 0; side < 4; side++) {
    for (int k = 0; k < segs; k++) {
      const float u0 = -H - 10.0f + (2.0f * H + 20.0f) * (float)k / segs;
      const float u1 = u0 + (2.0f * H + 20.0f) / segs + 0.5f;
      const float h = b.rng.range(18.0f, 34.0f), t = b.rng.range(8.0f, 14.0f);
      const glm::vec3 tint = b.tintVary(b.pal.rock);
      if (side == 0) b.box({u0, 0.0f, -H - t}, {u1, h, -H}, tint);
      else if (side == 1) b.box({u0, 0.0f, H}, {u1, h, H + t}, tint);
      else if (side == 2) b.box({-H - t, 0.0f, u0}, {-H, h, u1}, tint);
      else b.box({H, 0.0f, u0}, {H + t, h, u1}, tint);
    }
  }
}
