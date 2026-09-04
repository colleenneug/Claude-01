#pragma once
#include "Gl.h"
#include "Hostile.h"
#include "Level.h"
#include <vector>

struct ShotResult {
  bool hitSomething = false;
  bool hitHostile = false;
  bool headshot = false;
  int hostileIndex = -1;
  float damage = 0.0f;
};

// A single hitscan weapon: fixed damage, a magazine, a reload, and a fire
// rate. Firing raycasts against the level's walls (so a shot can't pass
// through a crate) and against every live hostile's head/body spheres,
// taking whichever is closer.
class Weapon {
public:
  int magSize = 24;
  int ammoInMag = 24;
  int reserveAmmo = 96;
  float damage = 22.0f;
  float headshotMultiplier = 2.0f;
  float fireInterval = 0.11f;   // seconds between shots, i.e. ~9 rounds/sec
  float reloadTime = 1.6f;

  float cooldown = 0.0f;
  float reloadT = 0.0f;
  bool reloading = false;

  void update(float dt);
  void startReload();

  // origin/dir define the ray (camera eye, forward). Returns what it hit;
  // does not itself apply damage to the hostile — the caller (Game) does,
  // so it can also handle kill rewards and hit-marker feedback in one place.
  ShotResult fire(const glm::vec3& origin, const glm::vec3& dir,
                   const Level& level, std::vector<Hostile>& hostiles);

  bool canFire() const { return !reloading && cooldown <= 0.0f && ammoInMag > 0; }
};
