#include "Level.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

void Level::build(float arenaSize, glm::vec3 floorTint, float coverDensity, int terrainType, bool cave) {
  // Safe to call more than once on the same Level — the Hub lets a player
  // run several missions in one session, and each one rebuilds its arena
  // from scratch. Without this, a second build() would leak the previous
  // meshes' GL handles (destroy() frees them before new ones are uploaded)
  // and keep appending to walls_/crates_/colliders_ forever instead of
  // replacing them.
  destroy();
  walls_.clear();
  props_.clear();
  debris_.clear();
  colliders_.clear();

  parts_.clear();
  fromParts_ = false;
  half_ = arenaSize * 0.5f;
  floorTop_ = 0.0f;
  floorTint_ = floorTint;
  terrainType_ = terrainType;
  cave_ = cave;

  // The floor's tessellation follows the arena rather than being fixed at 96
  // segments: the terrain material blends two stone layers off a per-vertex
  // weight, so a constant segment count over a two-hundred-metre arena
  // stretches every patch into a smear.
  int segs = std::clamp((int)(arenaSize * 1.2f), 96, 220);
  floorMesh_ = Mesh::terrainPlane(arenaSize, segs, 0.06f, 0.8f);
  boxMesh_ = Mesh::box(1.0f, 1.0f, 1.0f);   // scaled per instance, for walls and cover

  if (cave_) {
    buildCaveWalls(half_, wallHeight_ * 2.5f, 3.0f);
    buildStalactites(half_, wallHeight_ * 2.0f);
  } else {
    // Perimeter walls: four boxes, thick enough that fast movement can't
    // tunnel through them in one substep at this project's frame budget.
    const float t = 1.5f;   // wall thickness
    struct WallSpec { glm::vec3 centre, half; };
    WallSpec specs[4] = {
      {{0, wallHeight_ * 0.5f, -half_}, {half_ + t, wallHeight_ * 0.5f, t}},   // north
      {{0, wallHeight_ * 0.5f,  half_}, {half_ + t, wallHeight_ * 0.5f, t}},   // south
      {{-half_, wallHeight_ * 0.5f, 0}, {t, wallHeight_ * 0.5f, half_ + t}},   // west
      {{ half_, wallHeight_ * 0.5f, 0}, {t, wallHeight_ * 0.5f, half_ + t}},   // east
    };
    for (auto& w : specs) {
      glm::mat4 m = glm::translate(glm::mat4(1.0f), w.centre);
      m = glm::scale(m, w.half * 2.0f);
      walls_.push_back(m);
      colliders_.push_back({w.centre - w.half, w.centre + w.half});
    }
  }

  // Cover, scattered over the floor and kept clear of the middle so a
  // mission's spawn rings (see spawnPoint()) don't start half-embedded in it.
  // The count follows the *area*, not the side length: doubling the arena
  // quadruples the ground to cross, and cover spread linearly over that
  // leaves a parade ground with a few boxes around the edge.
  srand(7);
  auto rnd = [](float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; };
  const float clearRadius = std::max(7.0f, arenaSize * 0.075f);
  const int attempts = std::clamp(
      (int)(arenaSize * arenaSize / 190.0f * std::max(0.0f, coverDensity)), 0, 260);

  for (int i = 0; i < attempts; i++) {
    float x = rnd(-half_ + 5.0f, half_ - 5.0f);
    float z = rnd(-half_ + 5.0f, half_ - 5.0f);
    if (std::hypot(x, z) < clearRadius) continue;

    // Roughly half blocks, a third barricades, the rest pillars. Pillars are
    // the rarest because they are the ones that remove a sightline: enough to
    // break the arena up, not so many that it becomes a forest you cannot
    // fight a ranged enemy across.
    float roll = rnd(0.0f, 1.0f);
    CoverKind kind = roll < 0.48f ? CoverKind::Block
                   : roll < 0.80f ? CoverKind::Barricade
                                  : CoverKind::Pillar;

    glm::vec3 halfExt;
    switch (kind) {
      case CoverKind::Block:
        // Chest-high and up: something to break line of sight while standing,
        // which the old 1.1-metre crates never did.
        halfExt = glm::vec3(rnd(0.7f, 1.5f), rnd(0.85f, 1.7f), rnd(0.7f, 1.5f));
        break;
      case CoverKind::Barricade:
        // Long, low, and axis-aligned one way or the other. Rotating it to an
        // arbitrary angle would leave its collider — an AABB — describing a
        // box several metres wider than the thing you can see, so the
        // orientation is a quarter turn or nothing.
        halfExt = glm::vec3(rnd(2.2f, 4.6f), rnd(0.6f, 0.95f), rnd(0.4f, 0.7f));
        if (rnd(0.0f, 1.0f) < 0.5f) std::swap(halfExt.x, halfExt.z);
        break;
      case CoverKind::Pillar:
        halfExt = glm::vec3(rnd(0.6f, 1.1f), rnd(1.8f, 3.4f), rnd(0.6f, 1.1f));
        break;
    }

    glm::vec3 centre(x, halfExt.y, z);

    // Don't stack cover on cover: overlapping AABBs make resolve() fight
    // itself, pushing whatever is between them out along two axes at once.
    bool clash = false;
    for (const Collider& c : colliders_) {
      if (centre.x + halfExt.x < c.min.x - 0.6f || centre.x - halfExt.x > c.max.x + 0.6f) continue;
      if (centre.z + halfExt.z < c.min.z - 0.6f || centre.z - halfExt.z > c.max.z + 0.6f) continue;
      clash = true;
      break;
    }
    if (clash) continue;

    Prop prop;
    prop.kind = kind;
    prop.model = glm::scale(glm::translate(glm::mat4(1.0f), centre), halfExt * 2.0f);
    props_.push_back(prop);
    colliders_.push_back({centre - halfExt, centre + halfExt});
  }

  // -- Debris scatter: non-collidable decoration that fills the empty ground
  // between cover pieces so the arena reads as a place rather than a flat
  // plane with boxes on it.

  auto debrisTint = [&]() {
    return floorTint * glm::vec3(rnd(0.7f, 1.1f), rnd(0.7f, 1.1f), rnd(0.7f, 1.1f));
  };

  // Large boulders: collidable, kept out of the spawn zone.
  int boulderCount = std::clamp((int)(arenaSize * 0.08f), 4, 8);
  for (int i = 0; i < boulderCount; i++) {
    float x = rnd(-half_ + 6.0f, half_ - 6.0f);
    float z = rnd(-half_ + 6.0f, half_ - 6.0f);
    if (std::hypot(x, z) < clearRadius) continue;

    float hw = rnd(0.5f, 1.25f);
    float hh = rnd(0.25f, 0.75f);
    float hd = rnd(0.5f, 1.25f);
    glm::vec3 he(hw, hh, hd);
    glm::vec3 c(x, hh, z);

    bool clash = false;
    for (const Collider& col : colliders_) {
      if (c.x + hw < col.min.x - 0.6f || c.x - hw > col.max.x + 0.6f) continue;
      if (c.z + hd < col.min.z - 0.6f || c.z - hd > col.max.z + 0.6f) continue;
      clash = true; break;
    }
    if (clash) continue;

    glm::mat4 m = glm::translate(glm::mat4(1.0f), c);
    m = glm::rotate(m, rnd(0.0f, 6.28f), glm::vec3(0, 1, 0));
    m = glm::rotate(m, rnd(-0.12f, 0.12f), glm::vec3(1, 0, 0));
    m = glm::rotate(m, rnd(-0.12f, 0.12f), glm::vec3(0, 0, 1));
    m = glm::scale(m, he * 2.0f);

    DrawItem it;
    it.mesh = &boxMesh_;
    it.model = m;
    it.material = MaterialType::Armour;
    it.tint = debrisTint() * 0.85f;
    it.metallic = rnd(0.0f, 0.1f);
    it.roughness = rnd(0.85f, 0.95f);
    it.wear = rnd(1.5f, 2.0f);
    it.castShadow = true;
    debris_.push_back(it);
    colliders_.push_back({c - he, c + he});
  }

  // Rubble: medium non-collidable chunks scattered between cover.
  int rubbleCount = std::clamp((int)(arenaSize * 0.3f), 15, 30);
  for (int i = 0; i < rubbleCount; i++) {
    float x = rnd(-half_ + 3.0f, half_ - 3.0f);
    float z = rnd(-half_ + 3.0f, half_ - 3.0f);

    float sw = rnd(0.15f, 0.5f);
    float sh = rnd(0.1f, 0.3f);
    float sd = rnd(0.15f, 0.5f);
    glm::vec3 pos(x, sh, z);
    glm::vec3 size(sw * 2.0f, sh * 2.0f, sd * 2.0f);

    glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
    m = glm::rotate(m, rnd(0.0f, 6.28f), glm::vec3(0, 1, 0));
    m = glm::rotate(m, rnd(-0.2f, 0.2f), glm::vec3(1, 0, 0));
    m = glm::rotate(m, rnd(-0.2f, 0.2f), glm::vec3(0, 0, 1));
    m = glm::scale(m, size);

    DrawItem it;
    it.mesh = &boxMesh_;
    it.model = m;
    it.material = MaterialType::Armour;
    it.tint = debrisTint() * 0.75f;
    it.metallic = rnd(0.0f, 0.12f);
    it.roughness = rnd(0.75f, 0.95f);
    it.wear = rnd(1.4f, 2.0f);
    it.castShadow = true;
    debris_.push_back(it);
  }

  // Small rock clusters: dense scatter of tiny fragments across the ground.
  int clusterCount = std::clamp((int)(arenaSize * 0.7f), 40, 80);
  for (int i = 0; i < clusterCount; i++) {
    float cx = rnd(-half_ + 2.0f, half_ - 2.0f);
    float cz = rnd(-half_ + 2.0f, half_ - 2.0f);
    int pieces = 2 + (int)rnd(0.0f, 2.9f);
    for (int j = 0; j < pieces; j++) {
      float ox = cx + rnd(-0.6f, 0.6f);
      float oz = cz + rnd(-0.6f, 0.6f);
      float sw = rnd(0.08f, 0.25f);
      float sh = rnd(0.05f, 0.18f);
      float sd = rnd(0.08f, 0.25f);
      glm::vec3 pos(ox, sh, oz);
      glm::vec3 size(sw * 2.0f, sh * 2.0f, sd * 2.0f);

      glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
      m = glm::rotate(m, rnd(0.0f, 6.28f), glm::vec3(0, 1, 0));
      m = glm::rotate(m, rnd(-0.25f, 0.25f), glm::vec3(1, 0, 0));
      m = glm::scale(m, size);

      DrawItem it;
      it.mesh = &boxMesh_;
      it.model = m;
      it.material = MaterialType::Armour;
      it.tint = debrisTint();
      it.metallic = rnd(0.0f, 0.08f);
      it.roughness = rnd(0.8f, 0.98f);
      it.wear = rnd(1.2f, 1.8f);
      it.castShadow = false;
      debris_.push_back(it);
    }
  }
}

void Level::destroy() {
  floorMesh_.destroy();
  boxMesh_.destroy();
}

void Level::collect(std::vector<DrawItem>& out) const {
  if (fromParts_) {
    for (const Part& p : parts_) {
      DrawItem it;
      it.mesh = &boxMesh_;
      glm::vec3 centre = (p.min + p.max) * 0.5f;
      glm::vec3 size = p.max - p.min;
      it.model = glm::scale(glm::translate(glm::mat4(1.0f), centre), size);
      it.material = p.emissive ? MaterialType::Emissive : MaterialType::Armour;
      it.tint = p.tint;
      it.metallic = p.metallic;
      it.roughness = p.roughness;
      it.wear = p.wear;
      it.castShadow = p.castShadow && !p.emissive;
      if (p.emissive) {
        it.emissive = p.tint;
        it.emissiveIntensity = p.emissiveIntensity;
      }
      out.push_back(it);
    }
    return;
  }

  DrawItem floor;
  floor.mesh = &floorMesh_;
  floor.material = MaterialType::Terrain;
  floor.tint = floorTint_;
  floor.metallic = 0.0f;
  floor.roughness = 0.92f;
  floor.terrainType = terrainType_;
  out.push_back(floor);

  DrawItem wallBase;
  wallBase.mesh = &boxMesh_;
  wallBase.material = MaterialType::Armour;
  if (cave_) {
    wallBase.tint = floorTint_ * 1.3f;
    wallBase.metallic = 0.04f;
    wallBase.roughness = 0.88f;
    wallBase.wear = 1.8f;
    wallBase.anisoStrength = 0.0f;
  } else {
    wallBase.tint = glm::vec3(0.46f, 0.49f, 0.54f);
    wallBase.metallic = 0.22f;
    wallBase.roughness = 0.46f;
    wallBase.wear = 0.7f;
    wallBase.anisoStrength = 0.35f;
  }
  for (auto& m : walls_) { DrawItem it = wallBase; it.model = m; out.push_back(it); }

  // The three kinds of cover are tinted apart. It is not decoration: you have
  // to be able to tell at a glance whether the thing ahead of you is chest
  // high or takes the sightline away, and at sixty metres the silhouette
  // alone does not say.
  for (const Prop& p : props_) {
    DrawItem it = wallBase;
    it.model = p.model;
    switch (p.kind) {
      case CoverKind::Block:
        it.tint = glm::vec3(0.60f, 0.63f, 0.68f);
        it.wear = 1.1f;
        break;
      case CoverKind::Barricade:
        it.tint = glm::vec3(0.52f, 0.47f, 0.40f);
        it.wear = 1.4f;
        it.roughness = 0.58f;
        break;
      case CoverKind::Pillar:
        it.tint = glm::vec3(0.38f, 0.41f, 0.47f);
        it.wear = 0.6f;
        it.roughness = 0.40f;
        break;
    }
    out.push_back(it);
  }

  for (const DrawItem& d : debris_) out.push_back(d);
}

bool Level::wallAt(const glm::vec3& point, glm::vec3& normalOut) const {
  for (const Collider& c : colliders_) {
    if (point.x < c.min.x || point.x > c.max.x) continue;
    if (point.y < c.min.y || point.y > c.max.y) continue;
    if (point.z < c.min.z || point.z > c.max.z) continue;

    // Inside this box: the nearest face wins, and its outward normal is the
    // axis it lies on.
    float dxMin = point.x - c.min.x, dxMax = c.max.x - point.x;
    float dzMin = point.z - c.min.z, dzMax = c.max.z - point.z;
    float best = dxMin;
    normalOut = glm::vec3(-1, 0, 0);
    if (dxMax < best) { best = dxMax; normalOut = glm::vec3(1, 0, 0); }
    if (dzMin < best) { best = dzMin; normalOut = glm::vec3(0, 0, -1); }
    if (dzMax < best) { normalOut = glm::vec3(0, 0, 1); }
    return true;
  }
  return false;
}

bool Level::lineOfSight(const glm::vec3& from, const glm::vec3& to) const {
  glm::vec3 delta = to - from;
  float dist = glm::length(delta);
  if (dist < 1e-4f) return true;
  // One sample every 40cm: finer than the thinnest cover this level builds
  // (a barricade is 0.8m through), so nothing thick enough to stop a shot
  // can slip between two samples.
  int steps = std::max(2, (int)(dist / 0.4f));
  glm::vec3 normal;
  for (int i = 1; i < steps; i++) {
    glm::vec3 p = from + delta * ((float)i / (float)steps);
    if (wallAt(p, normal)) return false;
  }
  return true;
}

bool Level::resolve(glm::vec3& pos, float radius, float height) const {
  // Whether the cylinder's footprint overlaps a box at all, ignoring height.
  auto overlapsXZ = [&](const Collider& c) {
    return pos.x > c.min.x - radius && pos.x < c.max.x + radius &&
           pos.z > c.min.z - radius && pos.z < c.max.z + radius;
  };

  // ---- 1. what holds it up. The ground plane always does; a box does if its
  // top is under the footprint and no more than a step above the feet.
  auto findSupport = [&]() {
    float best = floorTop_;
    for (const Collider& c : colliders_) {
      if (c.max.y <= best) continue;
      if (c.max.y > pos.y + kStepHeight) continue;   // too tall to step onto
      if (!overlapsXZ(c)) continue;
      best = c.max.y;
    }
    return best;
  };
  float support = findSupport();

  // ---- 2. push out of anything that actually blocks. Two passes, because
  // the first can shove the cylinder into the side of a second box; a third
  // pass buys almost nothing and this runs four times a frame per actor.
  for (int pass = 0; pass < 2; pass++) {
    for (const Collider& c : colliders_) {
      // Low enough to step onto, so it is floor rather than wall.
      if (c.max.y <= pos.y + kStepHeight) continue;
      // Entirely above the head, so it is ceiling rather than wall.
      if (c.min.y >= pos.y + height) continue;
      if (!overlapsXZ(c)) continue;

      float exMinX = c.min.x - radius, exMaxX = c.max.x + radius;
      float exMinZ = c.min.z - radius, exMaxZ = c.max.z + radius;
      float pushLeft = pos.x - exMinX, pushRight = exMaxX - pos.x;
      float pushBack = pos.z - exMinZ, pushFwd = exMaxZ - pos.z;
      float minX = std::min(pushLeft, pushRight);
      float minZ = std::min(pushBack, pushFwd);

      if (minX < minZ) pos.x += (pushLeft < pushRight) ? -minX : minX;
      else pos.z += (pushBack < pushFwd) ? -minZ : minZ;
    }
  }

  // ---- 3. settle. Being pushed sideways can move the cylinder over a
  // different box, so the support is worked out again from where it ended up
  // rather than from where it started.
  support = findSupport();
  bool grounded = pos.y <= support + 1e-3f;
  if (pos.y < support) pos.y = support;

  // The procedural arena is a walled field and its perimeter is also enforced
  // here, belt and braces, in case a fast substep skipped clean over a wall
  // collider. A level built from parts has no such perimeter — clamping one
  // would pin the player inside the bounding box of the whole structure.
  if (!fromParts_) {
    pos.x = std::clamp(pos.x, -half_ + radius + 0.05f, half_ - radius - 0.05f);
    pos.z = std::clamp(pos.z, -half_ + radius + 0.05f, half_ - radius - 0.05f);
  }
  return grounded;
}

void Level::buildFromParts(const std::vector<Part>& parts, float floorY) {
  destroy();
  walls_.clear();
  props_.clear();
  debris_.clear();
  colliders_.clear();
  parts_ = parts;
  fromParts_ = true;
  floorTop_ = floorY;
  boxMesh_ = Mesh::box(1.0f, 1.0f, 1.0f);

  // half_ still bounds the shadow cascades and the spawn ring helper, so it
  // is taken from the parts rather than left at whatever the last arena was.
  float extent = 1.0f;
  for (const Part& p : parts) {
    extent = std::max(extent, std::max(std::abs(p.min.x), std::abs(p.max.x)));
    extent = std::max(extent, std::max(std::abs(p.min.z), std::abs(p.max.z)));
    if (p.solid) colliders_.push_back({p.min, p.max});
  }
  half_ = extent;
}

// ---------------------------------------------------------------------------
// Cave geometry generators
// ---------------------------------------------------------------------------

static float caveHash(float x, float y) {
  float v = std::sin(x * 127.1f + y * 311.7f) * 43758.5453f;
  return v - std::floor(v);
}

static float caveNoise(float x, float y) {
  float ix = std::floor(x), iy = std::floor(y);
  float fx = x - ix, fy = y - iy;
  fx = fx * fx * (3.0f - 2.0f * fx);
  fy = fy * fy * (3.0f - 2.0f * fy);
  float a = caveHash(ix, iy), b = caveHash(ix + 1, iy);
  float c = caveHash(ix, iy + 1), d = caveHash(ix + 1, iy + 1);
  float ab = a + (b - a) * fx;
  float cd = c + (d - c) * fx;
  return ab + (cd - ab) * fy;
}

static float caveFbm(float x, float y) {
  float sum = 0.0f, amp = 0.5f;
  for (int i = 0; i < 4; i++) {
    sum += caveNoise(x, y) * amp;
    x *= 2.17f; y *= 2.17f;
    amp *= 0.48f;
  }
  return sum;
}

void Level::buildCaveWalls(float half, float wallH, float thickness) {
  auto rnd = [](float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; };

  int segCount = std::clamp((int)(half * 0.7f), 24, 60);
  float segLen = (half * 2.0f) / (float)segCount;

  // For each of the four walls, place segments with random inward offsets
  // and varying heights to create an irregular rock surface.
  struct WallSide { int axis; float sign; };
  WallSide sides[4] = {{2, -1.0f}, {2, 1.0f}, {0, -1.0f}, {0, 1.0f}};

  for (auto& side : sides) {
    for (int i = 0; i < segCount; i++) {
      float along = -half + segLen * ((float)i + 0.5f);

      float noiseVal = caveFbm(along * 0.08f, side.sign * 13.7f + side.axis * 7.3f);
      float inwardOffset = noiseVal * 8.0f + rnd(0.0f, 3.0f);
      float heightVar = wallH * (0.7f + noiseVal * 0.6f);
      float widthVar = segLen * rnd(0.85f, 1.4f);
      float depthVar = thickness + rnd(0.5f, 4.0f) + inwardOffset;

      glm::vec3 centre, halfExt;
      if (side.axis == 2) {
        float baseZ = side.sign * half;
        float z = baseZ - side.sign * depthVar * 0.5f;
        centre = glm::vec3(along, heightVar * 0.5f, z);
        halfExt = glm::vec3(widthVar * 0.5f, heightVar * 0.5f, depthVar * 0.5f);
      } else {
        float baseX = side.sign * half;
        float x = baseX - side.sign * depthVar * 0.5f;
        centre = glm::vec3(x, heightVar * 0.5f, along);
        halfExt = glm::vec3(depthVar * 0.5f, heightVar * 0.5f, widthVar * 0.5f);
      }

      glm::mat4 m = glm::translate(glm::mat4(1.0f), centre);
      m = glm::scale(m, halfExt * 2.0f);
      walls_.push_back(m);
      colliders_.push_back({centre - halfExt, centre + halfExt});

      // Extra jutting rocks at random intervals for more irregularity
      if (rnd(0.0f, 1.0f) < 0.35f) {
        float jw = rnd(1.0f, 3.0f);
        float jh = rnd(1.5f, heightVar * 0.6f);
        float jd = rnd(1.5f, 5.0f);
        glm::vec3 jCentre = centre;
        if (side.axis == 2) {
          jCentre.z -= side.sign * jd * 0.4f;
          jCentre.x += rnd(-2.0f, 2.0f);
        } else {
          jCentre.x -= side.sign * jd * 0.4f;
          jCentre.z += rnd(-2.0f, 2.0f);
        }
        jCentre.y = jh * 0.5f + rnd(0.0f, 2.0f);
        glm::vec3 jHalf(jw * 0.5f, jh * 0.5f, jd * 0.5f);
        glm::mat4 jm = glm::translate(glm::mat4(1.0f), jCentre);
        jm = glm::scale(jm, jHalf * 2.0f);
        walls_.push_back(jm);
        colliders_.push_back({jCentre - jHalf, jCentre + jHalf});
      }
    }
  }
}

void Level::buildCaveCeiling(float half, float baseHeight) {
  auto rnd = [](float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; };

  float tileSize = 6.0f;
  int tiles = (int)(half * 2.0f / tileSize) + 1;

  for (int ix = 0; ix < tiles; ix++) {
    for (int iz = 0; iz < tiles; iz++) {
      float x = -half + tileSize * ((float)ix + 0.5f);
      float z = -half + tileSize * ((float)iz + 0.5f);

      float distFromCentre = std::hypot(x, z) / half;

      // Skip some tiles to create natural light shafts and openings
      float gapNoise = caveFbm(x * 0.04f + 11.2f, z * 0.04f + 3.8f);
      if (gapNoise < 0.32f && distFromCentre < 0.7f) continue;

      float noise = caveFbm(x * 0.06f + 5.3f, z * 0.06f + 8.7f);

      float ceilY = baseHeight + noise * 5.0f - distFromCentre * 2.0f;
      ceilY = std::max(ceilY, 6.0f);
      float thickness = 3.0f + rnd(1.0f, 5.0f);
      float w = tileSize * rnd(0.85f, 1.15f);
      float d = tileSize * rnd(0.85f, 1.15f);

      glm::vec3 centre(x, ceilY + thickness * 0.5f, z);
      glm::vec3 halfExt(w * 0.5f, thickness * 0.5f, d * 0.5f);

      glm::mat4 m = glm::translate(glm::mat4(1.0f), centre);
      m = glm::scale(m, halfExt * 2.0f);
      walls_.push_back(m);
    }
  }
}

void Level::buildStalactites(float half, float hangHeight) {
  auto rnd = [](float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; };

  int count = std::clamp((int)(half * 0.4f), 12, 35);
  float clearRadius = std::max(7.0f, half * 0.075f);

  for (int i = 0; i < count; i++) {
    float x = rnd(-half + 6.0f, half - 6.0f);
    float z = rnd(-half + 6.0f, half - 6.0f);
    if (std::hypot(x, z) < clearRadius) continue;

    bool fromAbove = rnd(0.0f, 1.0f) < 0.45f;
    float w = rnd(0.3f, 1.2f);
    float d = rnd(0.3f, 1.2f);
    float h;
    glm::vec3 centre;

    if (fromAbove) {
      h = rnd(2.0f, hangHeight * 0.4f);
      centre = glm::vec3(x, hangHeight - h * 0.5f, z);
    } else {
      h = rnd(0.5f, 3.0f);
      centre = glm::vec3(x, h * 0.5f, z);
    }

    glm::vec3 halfExt(w * 0.5f, h * 0.5f, d * 0.5f);

    bool clash = false;
    for (const Collider& c : colliders_) {
      if (centre.x + halfExt.x < c.min.x - 0.4f || centre.x - halfExt.x > c.max.x + 0.4f) continue;
      if (centre.z + halfExt.z < c.min.z - 0.4f || centre.z - halfExt.z > c.max.z + 0.4f) continue;
      if (centre.y + halfExt.y < c.min.y || centre.y - halfExt.y > c.max.y) continue;
      clash = true;
      break;
    }
    if (clash) continue;

    DrawItem it;
    it.mesh = &boxMesh_;
    glm::mat4 m = glm::translate(glm::mat4(1.0f), centre);
    m = glm::rotate(m, rnd(0.0f, 0.15f), glm::vec3(1, 0, 0));
    m = glm::rotate(m, rnd(0.0f, 6.28f), glm::vec3(0, 1, 0));
    m = glm::rotate(m, rnd(0.0f, 0.15f), glm::vec3(0, 0, 1));
    m = glm::scale(m, halfExt * 2.0f);
    it.model = m;
    it.material = MaterialType::Armour;
    it.tint = floorTint_ * glm::vec3(rnd(0.8f, 1.2f), rnd(0.8f, 1.2f), rnd(0.8f, 1.2f));
    it.metallic = rnd(0.0f, 0.08f);
    it.roughness = rnd(0.82f, 0.98f);
    it.wear = rnd(1.0f, 2.0f);
    it.castShadow = true;
    debris_.push_back(it);

    if (!fromAbove && h > 1.5f) {
      colliders_.push_back({centre - halfExt, centre + halfExt});
    }
  }

  // Full-height columns: stalactite + stalagmite meeting in the middle
  int columnCount = std::clamp((int)(half * 0.08f), 2, 6);
  for (int i = 0; i < columnCount; i++) {
    float x = rnd(-half + 10.0f, half - 10.0f);
    float z = rnd(-half + 10.0f, half - 10.0f);
    if (std::hypot(x, z) < clearRadius * 1.5f) continue;

    float w = rnd(0.8f, 2.0f);
    float d = rnd(0.8f, 2.0f);
    float h = hangHeight;
    glm::vec3 centre(x, h * 0.5f, z);
    glm::vec3 halfExt(w * 0.5f, h * 0.5f, d * 0.5f);

    bool clash = false;
    for (const Collider& c : colliders_) {
      if (centre.x + halfExt.x < c.min.x - 1.0f || centre.x - halfExt.x > c.max.x + 1.0f) continue;
      if (centre.z + halfExt.z < c.min.z - 1.0f || centre.z - halfExt.z > c.max.z + 1.0f) continue;
      clash = true;
      break;
    }
    if (clash) continue;

    Prop prop;
    prop.kind = CoverKind::Pillar;
    prop.model = glm::scale(glm::translate(glm::mat4(1.0f), centre), halfExt * 2.0f);
    props_.push_back(prop);
    colliders_.push_back({centre - halfExt, centre + halfExt});
  }
}

glm::vec3 Level::spawnPoint(int index, int total, float radius) const {
  float a = (total > 0) ? (float)index / (float)total * 6.28318f : 0.0f;
  // a little per-index jitter so a wave doesn't read as a perfect ring
  float jitter = (float)((index * 37) % 100) / 100.0f * 0.35f;
  float r = std::min(radius, half_ - 3.0f);
  return glm::vec3(std::cos(a + jitter) * r, 0.0f, std::sin(a + jitter) * r);
}
