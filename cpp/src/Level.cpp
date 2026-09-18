#include "Level.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

void Level::build(float arenaSize, glm::vec3 floorTint, float coverDensity) {
  // Safe to call more than once on the same Level — the Hub lets a player
  // run several missions in one session, and each one rebuilds its arena
  // from scratch. Without this, a second build() would leak the previous
  // meshes' GL handles (destroy() frees them before new ones are uploaded)
  // and keep appending to walls_/crates_/colliders_ forever instead of
  // replacing them.
  destroy();
  walls_.clear();
  props_.clear();
  colliders_.clear();

  parts_.clear();
  fromParts_ = false;
  half_ = arenaSize * 0.5f;
  floorTop_ = 0.0f;
  floorTint_ = floorTint;

  // The floor's tessellation follows the arena rather than being fixed at 96
  // segments: the terrain material blends two stone layers off a per-vertex
  // weight, so a constant segment count over a two-hundred-metre arena
  // stretches every patch into a smear.
  int segs = std::clamp((int)(arenaSize * 1.2f), 96, 220);
  floorMesh_ = Mesh::terrainPlane(arenaSize, segs, 0.06f);
  boxMesh_ = Mesh::box(1.0f, 1.0f, 1.0f);   // scaled per instance, for walls and cover

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
  out.push_back(floor);

  DrawItem wallBase;
  wallBase.mesh = &boxMesh_;
  wallBase.material = MaterialType::Armour;
  wallBase.tint = glm::vec3(0.46f, 0.49f, 0.54f);
  // Not polished metal: at 0.85 a wall is a mirror, and the only thing there
  // is to reflect is the light probe — a capture of a lit interior — so the
  // perimeter of a dark deck came out with orange and blue cube faces
  // painted across it. Plate and concrete, which is what it is.
  wallBase.metallic = 0.22f;
  wallBase.roughness = 0.46f;
  wallBase.wear = 0.7f;
  wallBase.anisoStrength = 0.35f;
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

glm::vec3 Level::spawnPoint(int index, int total, float radius) const {
  float a = (total > 0) ? (float)index / (float)total * 6.28318f : 0.0f;
  // a little per-index jitter so a wave doesn't read as a perfect ring
  float jitter = (float)((index * 37) % 100) / 100.0f * 0.35f;
  float r = std::min(radius, half_ - 3.0f);
  return glm::vec3(std::cos(a + jitter) * r, 0.0f, std::sin(a + jitter) * r);
}
