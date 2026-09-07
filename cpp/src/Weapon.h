#pragma once
#include "Gl.h"
#include "Content.h"
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

// A hitscan weapon: fixed damage, a magazine, a reload, and a fire rate.
// Firing raycasts against the level's walls (so a shot can't pass through a
// crate) and against every live hostile's head/body spheres. Two content-
// driven variants on the base single-target case: `pellets > 1` fires that
// many rays per trigger pull, each randomised within `spreadDegrees` (a
// shotgun — one pull, one shell, several pellets); `pierce` makes a single
// ray damage every hostile it crosses before the wall instead of stopping
// at the nearest one (an induction rifle bolt punching through).
class Weapon {
public:
  int magSize = 24;
  int ammoInMag = 24;
  int reserveAmmo = 96;
  float damage = 22.0f;
  float headshotMultiplier = 2.0f;
  float fireInterval = 0.11f;   // seconds between shots, i.e. ~9 rounds/sec
  float reloadTime = 1.6f;
  int pellets = 1;
  float spreadDegrees = 0.0f;
  bool pierce = false;

  float cooldown = 0.0f;
  float reloadT = 0.0f;
  bool reloading = false;

  // Applies a WeaponType's stats and refills the magazine — called once
  // when a mission's chosen weapon is resolved (see Game::init).
  void configure(const WeaponType& t);

  void update(float dt);
  void startReload();

  // origin/dir define the ray (camera eye, forward). Returns everything it
  // hit this trigger pull — one entry for a plain shot, up to `pellets` for
  // a shotgun, or one per hostile in line for a piercing shot. Does not
  // itself apply damage to a hostile — the caller (Game) does, so it can
  // also handle kill rewards and hit-marker feedback in one place.
  std::vector<ShotResult> fire(const glm::vec3& origin, const glm::vec3& dir,
                                const Level& level, std::vector<Hostile>& hostiles);

  bool canFire() const { return !reloading && cooldown <= 0.0f && ammoInMag > 0; }
};
