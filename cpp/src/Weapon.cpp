#include "Weapon.h"
#include <algorithm>
#include <cmath>

void Weapon::update(float dt) {
  cooldown = std::max(0.0f, cooldown - dt);
  if (reloading) {
    reloadT -= dt;
    if (reloadT <= 0.0f) {
      reloading = false;
      int need = magSize - ammoInMag;
      int take = std::min(need, reserveAmmo);
      ammoInMag += take;
      reserveAmmo -= take;
    }
  }
}

void Weapon::startReload() {
  if (reloading || ammoInMag == magSize || reserveAmmo <= 0) return;
  reloading = true;
  reloadT = reloadTime;
}

namespace {
// Ray vs sphere; returns the distance along the ray to the near
// intersection, or -1 if it misses.
float raySphere(const glm::vec3& origin, const glm::vec3& dir, const glm::vec3& centre, float radius) {
  glm::vec3 oc = centre - origin;
  float t = glm::dot(oc, dir);
  if (t < 0.0f) return -1.0f;
  float d2 = glm::dot(oc, oc) - t * t;
  float r2 = radius * radius;
  if (d2 > r2) return -1.0f;
  return t - std::sqrt(r2 - d2);
}

// Ray vs AABB (slab method); returns the entry distance, or a very large
// number if it misses (so "closer than the wall" comparisons just work).
float rayAABB(const glm::vec3& origin, const glm::vec3& dir, const Collider& c) {
  float tmin = 0.0f, tmax = 1e6f;
  for (int axis = 0; axis < 3; axis++) {
    float o = origin[axis], d = dir[axis];
    float lo = c.min[axis], hi = c.max[axis];
    if (std::abs(d) < 1e-8f) {
      if (o < lo || o > hi) return 1e6f;
      continue;
    }
    float inv = 1.0f / d;
    float t0 = (lo - o) * inv, t1 = (hi - o) * inv;
    if (t0 > t1) std::swap(t0, t1);
    tmin = std::max(tmin, t0);
    tmax = std::min(tmax, t1);
    if (tmin > tmax) return 1e6f;
  }
  return tmin;
}
}  // namespace

ShotResult Weapon::fire(const glm::vec3& origin, const glm::vec3& dir, const Level& level,
                         std::vector<Hostile>& hostiles) {
  ShotResult result;
  if (!canFire()) return result;

  cooldown = fireInterval;
  ammoInMag--;

  float wallDist = 1e6f;
  for (auto& c : level.colliders()) wallDist = std::min(wallDist, rayAABB(origin, dir, c));

  int bestIndex = -1;
  float bestDist = wallDist;
  bool bestHead = false;
  for (size_t i = 0; i < hostiles.size(); i++) {
    Hostile& h = hostiles[i];
    if (!h.blocksShots()) continue;
    float hd = raySphere(origin, dir, h.headCentre(), h.type->radius * 0.6f * h.bossScale);
    float bd = raySphere(origin, dir, h.bodyCentre(), h.type->radius * 1.05f * h.bossScale);
    bool isHead = hd >= 0.0f && (bd < 0.0f || hd <= bd);
    float d = isHead ? hd : bd;
    if (d < 0.0f || d >= bestDist) continue;
    bestDist = d;
    bestIndex = (int)i;
    bestHead = isHead;
  }

  if (bestIndex >= 0) {
    result.hitSomething = true;
    result.hitHostile = true;
    result.headshot = bestHead;
    result.hostileIndex = bestIndex;
    result.damage = damage * (bestHead ? headshotMultiplier : 1.0f);
  } else if (wallDist < 1e5f) {
    result.hitSomething = true;
  }
  return result;
}
