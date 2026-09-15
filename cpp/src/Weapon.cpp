#include "Weapon.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>

void Weapon::configure(const WeaponDef& t) {
  magSize = t.magSize;
  ammoInMag = t.magSize;
  reserveAmmo = t.reserveAmmo;
  damage = t.damage;
  headshotMultiplier = t.headshotMultiplier;
  fireInterval = t.fireInterval;
  reloadTime = t.reloadTime;
  pellets = std::max(1, t.pellets);
  spreadDegrees = t.spreadDegrees;
  pierce = t.pierce;
  cooldown = 0.0f;
  reloadT = 0.0f;
  reloading = false;
}

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

// A perturbed copy of `dir`, randomised within a `maxDegrees` cone — used
// for shotgun pellets. Builds a basis perpendicular to `dir` and offsets by
// a random angle on each axis rather than one random axis + angle, which
// would bias pellets towards the cone's rim instead of spreading evenly.
glm::vec3 spreadDir(const glm::vec3& dir, float maxDegrees) {
  if (maxDegrees <= 0.0f) return dir;
  glm::vec3 helper = std::abs(dir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  glm::vec3 right = glm::normalize(glm::cross(dir, helper));
  glm::vec3 up = glm::cross(right, dir);
  float rMax = glm::radians(maxDegrees);
  float a = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f) * rMax;
  float b = (((float)rand() / (float)RAND_MAX) * 2.0f - 1.0f) * rMax;
  return glm::normalize(dir + right * a + up * b);
}

struct HitCandidate { int index; float dist; bool head; };

// Every live hostile the ray crosses before `maxDist` (the nearest wall),
// nearest first.
std::vector<HitCandidate> castHostiles(const glm::vec3& origin, const glm::vec3& dir,
                                        std::vector<Hostile>& hostiles, float maxDist) {
  std::vector<HitCandidate> hits;
  for (size_t i = 0; i < hostiles.size(); i++) {
    Hostile& h = hostiles[i];
    if (!h.blocksShots()) continue;
    float hd = raySphere(origin, dir, h.headCentre(), h.type->radius * 0.6f * h.bossScale);
    float bd = raySphere(origin, dir, h.bodyCentre(), h.type->radius * 1.05f * h.bossScale);
    bool isHead = hd >= 0.0f && (bd < 0.0f || hd <= bd);
    float d = isHead ? hd : bd;
    if (d < 0.0f || d >= maxDist) continue;
    hits.push_back({(int)i, d, isHead});
  }
  std::sort(hits.begin(), hits.end(), [](const HitCandidate& a, const HitCandidate& b) {
    return a.dist < b.dist;
  });
  return hits;
}
}  // namespace

std::vector<ShotResult> Weapon::fire(const glm::vec3& origin, const glm::vec3& dir, const Level& level,
                                      std::vector<Hostile>& hostiles) {
  std::vector<ShotResult> results;
  if (!canFire()) return results;

  cooldown = fireInterval;
  ammoInMag--;

  float wallDist = 1e6f;
  for (auto& c : level.colliders()) wallDist = std::min(wallDist, rayAABB(origin, dir, c));

  int shotCount = std::max(1, pellets);
  for (int s = 0; s < shotCount; s++) {
    bool spread = shotCount > 1 || spreadDegrees > 0.0f;
    glm::vec3 d = spread ? spreadDir(dir, spreadDegrees) : dir;
    float wd = wallDist;
    if (spread) {
      wd = 1e6f;
      for (auto& c : level.colliders()) wd = std::min(wd, rayAABB(origin, d, c));
    }

    auto hits = castHostiles(origin, d, hostiles, wd);
    if (hits.empty()) {
      if (wd < 1e5f) {
        ShotResult r;
        r.hitSomething = true;
        results.push_back(r);
      }
      continue;
    }

    size_t take = pierce ? hits.size() : 1;
    for (size_t k = 0; k < take; k++) {
      ShotResult r;
      r.hitSomething = true;
      r.hitHostile = true;
      r.headshot = hits[k].head;
      r.hostileIndex = hits[k].index;
      r.damage = damage * (hits[k].head ? headshotMultiplier : 1.0f);
      results.push_back(r);
    }
  }
  return results;
}
