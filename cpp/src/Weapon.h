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

  // Ballistics, from the WeaponDef. See its comment in Content.h — these are
  // what make a breaching shotgun and an induction rifle different weapons
  // rather than two numbers.
  int pellets = 1;
  float spread = 0.0f;
  bool pierce = false;
  float range = 200.0f;

  float cooldown = 0.0f;
  float reloadT = 0.0f;
  bool reloading = false;
  // The Wraith's phase step leaves the next round primed: it lands as a
  // headshot wherever it hits. Set by Game::useAbility, spent on the next
  // trigger pull whether or not it connected.
  bool primed = false;

  void update(float dt);
  void startReload();

  // origin/dir define the ray (camera eye, forward). Fills `out` with one
  // entry per hostile struck — more than one when the weapon throws pellets
  // or pierces, with a pellet spread's worth of damage already summed per
  // hostile, plus a single no-hostile entry if the shot only found a wall.
  // Applying the damage is the caller's job (Game), so kill rewards and
  // hit-marker feedback stay in one place.
  //
  // `out` is cleared first and is expected to be a buffer the caller reuses,
  // because this runs on every trigger pull of every automatic weapon.
  void fire(const glm::vec3& origin, const glm::vec3& dir,
            const Level& level, std::vector<Hostile>& hostiles,
            std::vector<ShotResult>& out);

  bool canFire() const { return !reloading && cooldown <= 0.0f && ammoInMag > 0; }

private:
  // Counts trigger pulls, so the pellet spread is a deterministic function of
  // which shot and which pellet rather than a global RNG — two identical runs
  // have to produce identical results for the headless suite to mean anything.
  unsigned shotSeed_ = 0;
};
