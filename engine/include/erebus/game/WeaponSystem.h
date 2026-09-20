#pragma once
// =============================================================================
//  erebus/game/WeaponSystem.h — hitscan, projectiles, recoil and reticle bloom.
//
//  THREE THINGS THAT ARE OFTEN CONFLATED, AND MUST NOT BE
//
//  1. RECOIL is a deterministic transform applied to the *camera*. It is a
//     pattern, and the player learns it and counters it. It must be the same
//     every time for a given shot index or there is nothing to learn.
//
//  2. BLOOM (spread, cone of fire) is a random cone applied to the *shot*. The
//     player cannot counter it; they can only stop firing and let it settle.
//     It is what makes sustained fire worse than tapping.
//
//  3. SWAY/BREATHING is a low-frequency drift, cosmetic at hip fire and
//     meaningful when scoped.
//
//  Games that feel unfair usually have too much (2) and too little (1) — the
//  shots go somewhere the reticle did not predict and no amount of skill
//  changes it. Games that feel weightless have too little of both.
//
//  RECOIL AS A SECOND-ORDER SYSTEM
//  A shot applies an impulse to a critically damped spring pulling the camera
//  offset back to zero. Critically damped (zeta = 1) is the right default: it
//  returns fastest without overshoot, and overshoot in a recoil recovery reads
//  as the gun fighting the player.
//
//      x'' = -k*x - c*x'      with c = 2*sqrt(k) for zeta = 1
//
//  Integrated semi-implicitly, which is stable at the stiffnesses this needs
//  (k in the hundreds) where explicit Euler is not.
//
//  RECOVERY IS SPLIT FROM THE KICK. The camera takes the full kick and gives
//  back only a fraction of it, because a recoil that returns exactly to where
//  it started means the gun does not climb, and a gun that does not climb has
//  no recoil pattern to learn. `recoveryFraction` below is that dial.
//
//  PROJECTILES vs HITSCAN
//  Hitscan resolves in the frame it is fired. Projectiles are entities
//  integrated in the fixed step with gravity and drag, and they need
//  continuous collision — a 90 m/s rocket moves 75cm per 120Hz step, which is
//  more than thin cover is thick. Sweep, never point-test.
// =============================================================================

#include "erebus/core/Types.h"
#include "erebus/ecs/Entity.h"
#include "erebus/ecs/World.h"

#include <functional>
#include <vector>

namespace erebus::game {

// ---------------------------------------------------------------- definitions
enum class FireMode : u8 { Semi, Burst, Auto };
enum class DeliveryKind : u8 { Hitscan, Projectile };

struct WeaponDef {
  // --- ballistics
  DeliveryKind delivery = DeliveryKind::Hitscan;
  f32 damage            = 22.0f;   // per pellet
  f32 headshotMultiplier = 2.0f;
  u32 pelletsPerShot    = 1;       // >1 is a shotgun; damage is per pellet
  f32 maxRange          = 200.0f;  // metres; hitscan misses beyond this
  f32 projectileSpeed   = 90.0f;   // m/s, DeliveryKind::Projectile only
  f32 projectileGravity = 9.8f;

  // --- cadence
  FireMode mode         = FireMode::Auto;
  f32 roundsPerMinute   = 540.0f;
  u32 magazineSize      = 30;
  f32 reloadSeconds     = 1.9f;

  // --- reticle bloom (see header note 2)
  f32 bloomMin          = 0.0020f;  // radians; the cone when perfectly settled
  f32 bloomMax          = 0.0450f;  // the cone under sustained fire
  f32 bloomPerShot      = 0.0060f;  // added per trigger pull
  f32 bloomDecayPerSec  = 0.0700f;  // subtracted per second, once settling
  f32 bloomSettleDelay  = 0.110f;   // seconds after the last shot before decay
  f32 bloomAimScale     = 0.25f;    // multiplier while aiming down sights

  // --- recoil (see header note 1)
  f32 recoilPitch       = 0.85f;    // degrees of kick per shot, up
  f32 recoilYaw         = 0.30f;    // degrees, alternating left/right
  f32 recoilStiffness   = 260.0f;   // spring k
  f32 recoveryFraction  = 0.72f;    // how much of the kick is given back
  f32 cameraShake       = 0.20f;    // positional shake amplitude, metres
};

// ---------------------------------------------------------------- runtime state
struct WeaponState {
  u32 ammoInMagazine = 0;
  u32 reserveAmmo    = 0;
  u32 shotIndex      = 0;     // index within the current burst; drives the pattern

  f32 fireCooldown   = 0.0f;  // seconds until the next shot is permitted
  f32 reloadTimer    = 0.0f;
  f32 timeSinceShot  = 999.0f;

  f32 bloom          = 0.0f;  // radians, current cone half-angle

  // Recoil spring, in degrees. `offset` is added to the camera; `velocity` is
  // its rate. `debt` is the part of the kick that will never be returned,
  // which is what makes the gun climb.
  f32 recoilPitchOffset = 0.0f, recoilPitchVelocity = 0.0f;
  f32 recoilYawOffset   = 0.0f, recoilYawVelocity   = 0.0f;
  f32 recoilPitchDebt   = 0.0f, recoilYawDebt       = 0.0f;

  bool reloading = false;
  bool aiming    = false;
};

// ---------------------------------------------------------------- results
struct RayHit {
  bool hit = false;
  Vec3 point{};
  Vec3 normal{};
  f32  distance = 0.0f;
  ecs::Entity entity{};
  bool headshot = false;
};

// The world query the weapon system needs. Injected rather than depended on so
// the weapon logic can be unit-tested against a stub that returns scripted
// hits — gunplay tuning that requires a level loaded is gunplay tuning that
// does not happen.
using RaycastFn = std::function<RayHit(const Vec3& origin, const Vec3& direction, f32 maxDistance)>;

struct ShotResult {
  Vec3 direction{};      // after bloom was applied
  RayHit hit{};
  f32 damageDealt = 0.0f;
};

// ---------------------------------------------------------------- the system
class WeaponSystem {
 public:
  explicit WeaponSystem(RaycastFn raycast) noexcept : raycast_(std::move(raycast)) {}

  // Per fixed step, whether or not the trigger is held: this is what decays
  // bloom and integrates the recoil spring, and both must keep running while
  // the player is not shooting or nothing ever settles.
  void step(const WeaponDef& def, WeaponState& state, f32 dt) noexcept;

  // Attempt to fire. Returns the pellets that were resolved — empty if the
  // weapon was on cooldown, reloading or dry. `seed` drives the bloom cone
  // and must come from a deterministic per-shot counter (not rand()), so a
  // replay or a server reconciliation reproduces the same pellets.
  std::vector<ShotResult> fire(const WeaponDef& def, WeaponState& state,
                               const Vec3& eye, const Vec3& forward,
                               const Vec3& right, const Vec3& up,
                               u32 seed);

  void beginReload(const WeaponDef& def, WeaponState& state) noexcept;

  // The camera offset to add this frame, in degrees. Read by the camera system
  // after step(); the weapon never writes the camera directly, because two
  // systems writing one transform is how you get a camera that fights itself.
  static void cameraRecoil(const WeaponState& s, f32& outPitchDeg, f32& outYawDeg) noexcept {
    outPitchDeg = s.recoilPitchOffset;
    outYawDeg   = s.recoilYawOffset;
  }

  // The reticle's on-screen radius in pixels, for a given vertical FOV and
  // viewport height. The reticle must be derived from the same bloom value the
  // shots use — a reticle that is a fixed sprite while the cone grows is the
  // single most common cause of "my shots do not go where I aim".
  [[nodiscard]] static f32 reticleRadiusPixels(const WeaponState& s, f32 verticalFovRadians,
                                               f32 viewportHeightPixels) noexcept {
    const f32 halfFov = verticalFovRadians * 0.5f;
    return (std::tan(s.bloom) / std::tan(halfFov)) * (viewportHeightPixels * 0.5f);
  }

 private:
  RaycastFn raycast_;
};

}  // namespace erebus::game
