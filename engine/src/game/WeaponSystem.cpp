#include "erebus/game/WeaponSystem.h"

#include <algorithm>
#include <cmath>

namespace erebus::game {
namespace {

constexpr f32 kDegToRad = 0.01745329252f;

// Deterministic hash -> [0,1). Not a PRNG: a PRNG carries state, and state is
// what makes a replay diverge. Every call here is a pure function of the shot
// index and the pellet index, so the same shot produces the same cone on the
// client, on the server and in a replay six months later.
inline f32 hash01(u32 n) noexcept {
  n ^= n >> 16; n *= 0x7feb352du;
  n ^= n >> 15; n *= 0x846ca68bu;
  n ^= n >> 16;
  return static_cast<f32>(n & 0x00ffffffu) / static_cast<f32>(0x01000000u);
}

// A direction inside a cone of half-angle `spread` around `dir`.
//
// The sqrt on the radius is not decoration: sampling radius uniformly bunches
// pellets at the centre, because area grows as r^2. sqrt(u) gives a uniform
// areal distribution, which is what a shotgun pattern actually looks like and
// what makes the reticle radius an honest promise about where pellets land.
inline Vec3 coneSample(const Vec3& dir, const Vec3& right, const Vec3& up,
                       f32 spread, u32 seed) noexcept {
  if (spread <= 0.0f) return dir;
  const f32 angle = hash01(seed * 2u + 1u) * 6.2831853f;
  const f32 radius = std::sqrt(hash01(seed * 2u + 7919u)) * spread;
  const f32 sr = std::sin(radius);
  return normalize(dir * std::cos(radius) +
                   right * (sr * std::cos(angle)) +
                   up * (sr * std::sin(angle)), dir);
}

// One axis of the recoil spring, integrated semi-implicitly.
//
// Semi-implicit (update velocity, then use the NEW velocity to update
// position) rather than explicit Euler, because at k = 260 and dt = 1/120 the
// explicit form is right on the edge of instability and a stiffer weapon
// pushes it over. Semi-implicit is unconditionally stable for this system and
// costs nothing extra.
inline void springStep(f32& offset, f32& velocity, f32 target, f32 stiffness, f32 dt) noexcept {
  const f32 damping = 2.0f * std::sqrt(stiffness);   // zeta = 1, critical
  const f32 accel = -stiffness * (offset - target) - damping * velocity;
  velocity += accel * dt;
  offset   += velocity * dt;
}

}  // namespace

void WeaponSystem::step(const WeaponDef& def, WeaponState& state, f32 dt) noexcept {
  state.fireCooldown  = std::max(0.0f, state.fireCooldown - dt);
  state.timeSinceShot += dt;

  // ---------------------------------------------------------------- reload
  if (state.reloading) {
    state.reloadTimer -= dt;
    if (state.reloadTimer <= 0.0f) {
      const u32 want = def.magazineSize - state.ammoInMagazine;
      const u32 take = std::min(want, state.reserveAmmo);
      state.ammoInMagazine += take;
      state.reserveAmmo    -= take;
      state.reloading = false;
      state.reloadTimer = 0.0f;
      // A reload resets the burst pattern. It also resets bloom, which is the
      // reason a reload is sometimes the correct aggressive play rather than
      // only a cost.
      state.shotIndex = 0;
      state.bloom = def.bloomMin;
    }
  }

  // ---------------------------------------------------------------- bloom
  // Decay only after the settle delay. Without the delay, a weapon firing at
  // 540rpm (111ms between shots) decays meaningfully between every pair of
  // shots and sustained fire never actually blooms.
  const f32 floorBloom = def.bloomMin * (state.aiming ? def.bloomAimScale : 1.0f);
  if (state.timeSinceShot >= def.bloomSettleDelay) {
    state.bloom = std::max(floorBloom, state.bloom - def.bloomDecayPerSec * dt);
  }
  state.bloom = std::clamp(state.bloom, floorBloom,
                           def.bloomMax * (state.aiming ? def.bloomAimScale : 1.0f));

  // ---------------------------------------------------------------- recoil
  // The spring pulls toward `debt` rather than toward zero. Debt is the part
  // of the kick that is never given back, so the sight line climbs over a
  // burst and stays climbed until the player pulls it down — which is what
  // makes recoil a pattern to learn instead of a wobble to wait out.
  springStep(state.recoilPitchOffset, state.recoilPitchVelocity, state.recoilPitchDebt,
             def.recoilStiffness, dt);
  springStep(state.recoilYawOffset, state.recoilYawVelocity, state.recoilYawDebt,
             def.recoilStiffness, dt);

  // Debt itself bleeds away slowly once the trigger is released, so a player
  // who stops shooting is not left permanently aiming at the sky.
  if (state.timeSinceShot > def.bloomSettleDelay * 2.0f) {
    const f32 bleed = 2.2f * dt;
    state.recoilPitchDebt -= state.recoilPitchDebt * std::min(1.0f, bleed);
    state.recoilYawDebt   -= state.recoilYawDebt   * std::min(1.0f, bleed);
  }
}

void WeaponSystem::beginReload(const WeaponDef& def, WeaponState& state) noexcept {
  if (state.reloading) return;
  if (state.ammoInMagazine >= def.magazineSize) return;   // already full
  if (state.reserveAmmo == 0) return;
  state.reloading = true;
  state.reloadTimer = def.reloadSeconds;
}

std::vector<ShotResult> WeaponSystem::fire(const WeaponDef& def, WeaponState& state,
                                           const Vec3& eye, const Vec3& forward,
                                           const Vec3& right, const Vec3& up,
                                           u32 seed) {
  std::vector<ShotResult> results;
  if (state.reloading || state.fireCooldown > 0.0f || state.ammoInMagazine == 0) return results;

  state.fireCooldown = 60.0f / std::max(1.0f, def.roundsPerMinute);
  state.ammoInMagazine -= 1;
  state.timeSinceShot = 0.0f;

  // ---------------------------------------------------------------- pellets
  results.reserve(def.pelletsPerShot);
  for (u32 p = 0; p < def.pelletsPerShot; ++p) {
    // The cone is the CURRENT bloom, sampled before this shot's contribution
    // is added. Firing the first shot of a burst through the cone the burst
    // will eventually reach would punish the tap that the whole bloom system
    // exists to reward.
    const Vec3 dir = coneSample(forward, right, up, state.bloom, seed * 31u + p);

    ShotResult r;
    r.direction = dir;
    if (def.delivery == DeliveryKind::Hitscan && raycast_) {
      r.hit = raycast_(eye, dir, def.maxRange);
      if (r.hit.hit) {
        r.damageDealt = def.damage * (r.hit.headshot ? def.headshotMultiplier : 1.0f);
      }
    }
    // Projectiles: spawn an entity here instead, with velocity dir *
    // def.projectileSpeed, and let the fixed-step projectile system integrate
    // and sweep it. See README.md, "Projectiles".
    results.push_back(r);
  }

  // ---------------------------------------------------------------- feedback
  state.bloom = std::min(def.bloomMax * (state.aiming ? def.bloomAimScale : 1.0f),
                         state.bloom + def.bloomPerShot);

  // Recoil impulse. Pitch is always up; yaw alternates by shot index with a
  // deterministic magnitude, which is what makes a spray pattern an
  // identifiable shape rather than a random walk. Multiply `shotIndex` into
  // the pattern lookup to author per-weapon patterns as a table.
  const f32 yawSign = (state.shotIndex % 2 == 0) ? 1.0f : -1.0f;
  const f32 yawJitter = 0.65f + hash01(state.shotIndex * 2654435761u) * 0.7f;

  const f32 kickPitch = def.recoilPitch * kDegToRad;
  const f32 kickYaw   = def.recoilYaw * yawSign * yawJitter * kDegToRad;

  state.recoilPitchVelocity += kickPitch * def.recoilStiffness * 0.02f;
  state.recoilYawVelocity   += kickYaw   * def.recoilStiffness * 0.02f;
  state.recoilPitchDebt     += kickPitch * (1.0f - def.recoveryFraction);
  state.recoilYawDebt       += kickYaw   * (1.0f - def.recoveryFraction);

  state.shotIndex += 1;
  return results;
}

}  // namespace erebus::game
