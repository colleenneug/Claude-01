#include "Level.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

void Level::build(float arenaSize) {
  half_ = arenaSize * 0.5f;
  floorTop_ = 0.0f;

  floorMesh_ = Mesh::terrainPlane(arenaSize, 96, 0.06f);
  wallMesh_ = Mesh::box(1.0f, 1.0f, 1.0f);   // scaled per-instance
  crateMesh_ = Mesh::box(1.1f, 1.1f, 1.1f);

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

  // Scattered cover, kept clear of the centre so a mission's spawn rings
  // (see spawnPoint()) don't start half-embedded in a crate.
  srand(7);
  auto rnd = [](float lo, float hi) { return lo + (hi - lo) * (float)rand() / (float)RAND_MAX; };
  int count = std::max(4, (int)(arenaSize * 0.25f));
  for (int i = 0; i < count; i++) {
    float x = rnd(-half_ + 4.0f, half_ - 4.0f);
    float z = rnd(-half_ + 4.0f, half_ - 4.0f);
    if (std::hypot(x, z) < 6.0f) continue;
    float s = rnd(0.8f, 1.6f);
    glm::vec3 centre(x, s * 0.55f, z);
    glm::vec3 halfExt(s * 0.55f, s * 0.55f, s * 0.55f);
    glm::mat4 m = glm::translate(glm::mat4(1.0f), centre);
    m = glm::rotate(m, rnd(0.0f, 6.28f), glm::vec3(0, 1, 0));
    m = glm::scale(m, glm::vec3(s));
    crates_.push_back(m);
    colliders_.push_back({centre - halfExt, centre + halfExt});
  }
}

void Level::destroy() {
  floorMesh_.destroy();
  wallMesh_.destroy();
  crateMesh_.destroy();
}

void Level::collect(std::vector<DrawItem>& out) const {
  DrawItem floor;
  floor.mesh = &floorMesh_;
  floor.material = MaterialType::Terrain;
  floor.tint = glm::vec3(0.40f, 0.33f, 0.27f);
  floor.metallic = 0.0f;
  floor.roughness = 0.92f;
  out.push_back(floor);

  DrawItem wallBase;
  wallBase.mesh = &wallMesh_;
  wallBase.material = MaterialType::Armour;
  wallBase.tint = glm::vec3(0.46f, 0.49f, 0.54f);
  wallBase.metallic = 0.85f;
  wallBase.roughness = 0.30f;
  wallBase.wear = 0.7f;
  wallBase.anisoStrength = 0.35f;
  for (auto& m : walls_) { DrawItem it = wallBase; it.model = m; out.push_back(it); }

  DrawItem crateBase = wallBase;
  crateBase.tint = glm::vec3(0.62f, 0.66f, 0.71f);
  crateBase.wear = 1.1f;
  for (auto& m : crates_) { DrawItem it = crateBase; it.model = m; out.push_back(it); }
}

bool Level::resolve(glm::vec3& pos, float radius, float height) const {
  bool grounded = pos.y <= floorTop_ + 1e-3f;
  if (pos.y < floorTop_) pos.y = floorTop_;

  for (auto& c : colliders_) {
    if (pos.y + height < c.min.y || pos.y > c.max.y) continue;   // no vertical overlap

    float exMinX = c.min.x - radius, exMaxX = c.max.x + radius;
    float exMinZ = c.min.z - radius, exMaxZ = c.max.z + radius;
    if (pos.x < exMinX || pos.x > exMaxX || pos.z < exMinZ || pos.z > exMaxZ) continue;

    float pushLeft = pos.x - exMinX, pushRight = exMaxX - pos.x;
    float pushBack = pos.z - exMinZ, pushFwd = exMaxZ - pos.z;
    float minX = std::min(pushLeft, pushRight);
    float minZ = std::min(pushBack, pushFwd);

    if (minX < minZ) pos.x += (pushLeft < pushRight) ? -minX : minX;
    else pos.z += (pushBack < pushFwd) ? -minZ : minZ;
  }

  // Keep inside the arena walls even if a fast-moving substep skipped over
  // one — belt and braces around the wall colliders above.
  pos.x = std::clamp(pos.x, -half_ + radius + 0.05f, half_ - radius - 0.05f);
  pos.z = std::clamp(pos.z, -half_ + radius + 0.05f, half_ - radius - 0.05f);
  return grounded;
}

glm::vec3 Level::spawnPoint(int index, int total, float radius) const {
  float a = (total > 0) ? (float)index / (float)total * 6.28318f : 0.0f;
  // a little per-index jitter so a wave doesn't read as a perfect ring
  float jitter = (float)((index * 37) % 100) / 100.0f * 0.35f;
  float r = std::min(radius, half_ - 3.0f);
  return glm::vec3(std::cos(a + jitter) * r, 0.0f, std::sin(a + jitter) * r);
}
