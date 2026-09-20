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

// A deterministic unit-interval hash: the pellet spread has to be the same
// on two identical runs, which a global RNG cannot promise once anything else
// in the frame also draws from it.
float hash01(unsigned n) {
  n = (n ^ 61u) ^ (n >> 16);
  n *= 9u;
  n = n ^ (n >> 4);
  n *= 0x27d4eb2du;
  n = n ^ (n >> 15);
  return (float)(n & 0xffffffu) / (float)0x1000000u;
}
}  // namespace

void Weapon::fire(const glm::vec3& origin, const glm::vec3& dir, const Level& level,
                  std::vector<Hostile>& hostiles, std::vector<ShotResult>& out) {
  out.clear();
  if (!canFire()) return;

  cooldown = fireInterval;
  ammoInMag--;
  const bool wasPrimed = primed;
  primed = false;
  const unsigned seed = shotSeed_++;

  float wallDist = 1e6f;
  for (auto& c : level.colliders()) wallDist = std::min(wallDist, rayAABB(origin, dir, c));
  // A shot cannot reach past the weapon's own range, and a wall stops it
  // sooner. Everything below compares against this one number.
  const float reach = std::min(wallDist, range);

  // A basis to scatter pellets in. Any two vectors perpendicular to the aim
  // will do; picking the world up-axis fails only when you are aiming
  // straight up or down, which the fallback covers.
  glm::vec3 up = std::abs(dir.y) > 0.99f ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
  glm::vec3 side = glm::normalize(glm::cross(dir, up));
  up = glm::cross(side, dir);

  // Damage summed per hostile rather than one entry per pellet: eight pellets
  // into the same target is one hit as far as the kill check, the hit marker
  // and the reward are concerned.
  bool hitAnything = false;
  for (int p = 0; p < pellets; p++) {
    glm::vec3 ray = dir;
    if (spread > 0.0f) {
      // Uniform over the cone's disc: sqrt on the radius, or the pellets
      // bunch in the middle and the pattern reads as a dot with outliers.
      float angle = hash01(seed * 131u + (unsigned)p * 7u) * 6.2831853f;
      float radius = std::sqrt(hash01(seed * 977u + (unsigned)p * 31u + 17u)) * spread;
      ray = glm::normalize(dir + side * (std::cos(angle) * radius) + up * (std::sin(angle) * radius));
    }

    // Walk the hostiles in depth order along this pellet, so a piercing round
    // hits the front rank before the one behind it and a non-piercing one
    // stops at the first.
    struct Contact { int index; float dist; bool head; };
    std::vector<Contact> contacts;
    for (size_t i = 0; i < hostiles.size(); i++) {
      Hostile& h = hostiles[i];
      if (!h.blocksShots()) continue;
      float hd = raySphere(origin, ray, h.headCentre(), h.type->radius * 0.6f * h.bossScale);
      float bd = raySphere(origin, ray, h.bodyCentre(), h.type->radius * 1.05f * h.bossScale);
      bool isHead = hd >= 0.0f && (bd < 0.0f || hd <= bd);
      float d = isHead ? hd : bd;
      if (d < 0.0f || d >= reach) continue;
      contacts.push_back({(int)i, d, isHead});
    }
    std::sort(contacts.begin(), contacts.end(),
              [](const Contact& a, const Contact& b) { return a.dist < b.dist; });

    size_t take = pierce ? contacts.size() : std::min<size_t>(1, contacts.size());
    for (size_t c = 0; c < take; c++) {
      const Contact& hit = contacts[c];
      bool head = hit.head || wasPrimed;
      float dmg = damage * (head ? headshotMultiplier : 1.0f);
      hitAnything = true;

      auto existing = std::find_if(out.begin(), out.end(),
                                   [&](const ShotResult& r) { return r.hostileIndex == hit.index; });
      if (existing == out.end()) {
        ShotResult r;
        r.hitSomething = true;
        r.hitHostile = true;
        r.headshot = head;
        r.hostileIndex = hit.index;
        r.damage = dmg;
        r.point = origin + ray * hit.dist;
        out.push_back(r);
      } else {
        existing->damage += dmg;
        existing->headshot = existing->headshot || head;
      }
    }
  }

  // Nothing struck, but something stopped the shot: the caller still wants to
  // know the round went somewhere rather than into the sky.
  if (!hitAnything && wallDist < 1e5f) {
    ShotResult r;
    r.hitSomething = true;
    // Pulled back a little along the ray, so the spark sits on the face of
    // the wall rather than a hair inside it where it would be invisible.
    r.point = origin + dir * std::max(0.0f, std::min(wallDist, range) - 0.04f);
    out.push_back(r);
  }
}
