#pragma once
// =============================================================================
//  erebus/game/PlayerController.h — momentum-based character movement.
//
//  THE FEEL, AND WHY IT IS BUILT THIS WAY
//  "Floaty yet snappy" is not one parameter. It is a specific combination:
//
//    snappy  — ground acceleration is very high (an order of magnitude above
//              gravity) and ground friction is high, so the ground response to
//              a stick input is near-instant in both directions. You reach top
//              speed in ~100ms and you stop in ~100ms.
//    floaty  — airborne, acceleration collapses to a small fraction and
//              friction goes to zero, so horizontal velocity in the air is
//              *preserved* rather than driven. Jump arcs are long, and what
//              you entered the jump with is most of what you leave it with.
//
//  Games that feel mushy usually have the first half wrong (low ground accel).
//  Games that feel twitchy usually have the second half wrong (full air
//  control), because then the air is just the ground with a different mesh.
//
//  THE ACCELERATION MODEL
//  Quake's accelerate(), which is still the right answer thirty years later:
//
//      currentSpeed = dot(velocity, wishDir)
//      addSpeed     = wishSpeed - currentSpeed
//      if (addSpeed <= 0) return                     // already at or past it
//      accelSpeed   = min(accel * wishSpeed * dt, addSpeed)
//      velocity    += wishDir * accelSpeed
//
//  The property that matters: acceleration is applied along the *wish*
//  direction and clamped by the deficit along that axis, not by total speed.
//  So velocity perpendicular to the input is never reduced by acceleration —
//  only friction removes it. That asymmetry is the whole reason air-strafing
//  and slide-hopping are expressive rather than noise, and it is why replacing
//  this with `velocity = wishDir * speed` kills the movement dead.
//
//  DETERMINISM
//  Every function here takes an explicit dt and is a pure function of
//  (state, input, dt). No clock reads, no randomness, no globals. That is what
//  makes it safe to run inside the fixed-timestep loop, to record and replay,
//  and to run on a server for reconciliation.
// =============================================================================

#include "erebus/core/Types.h"
#include "erebus/ecs/Components.h"

namespace erebus::game {

// Tunables. Grouped in one struct so they can be hot-reloaded from a config
// file and A/B'd without touching code — movement feel is found by iteration,
// not derived, and a recompile per iteration is how you stop iterating.
struct MovementConfig {
  // --- ground
  f32 walkSpeed      = 4.6f;    // m/s
  f32 sprintSpeed    = 9.4f;
  f32 crouchSpeed    = 2.3f;
  f32 groundAccel    = 62.0f;   // 1/s; reaches walkSpeed in ~1/13 s
  f32 groundFriction = 9.0f;    // 1/s

  // --- air. The two numbers that make the jump read as a jump.
  f32 airAccel       = 6.5f;    // ~10% of ground: influence, not control
  f32 airFriction    = 0.0f;    // none. Air does not slow you down here.
  // Separate, larger cap on acceleration perpendicular to current velocity.
  // This is what permits air-strafing to bend a trajectory without letting a
  // player accelerate forwards indefinitely in mid-air.
  f32 airStrafeAccel = 28.0f;
  f32 airControlDamping = 0.72f; // scales air accel as speed exceeds walkSpeed

  // --- gravity and jumping
  f32 gravity        = 22.0f;   // m/s^2. ~2.2g: shorter hang than real gravity
                                // at the same jump height, which reads as
                                // responsive rather than as low-g.
  f32 jumpVelocity   = 7.2f;    // m/s; apex ~1.18m at the gravity above
  f32 terminalSpeed  = 55.0f;

  // Grace windows, in seconds. Both exist because a player's perception of
  // "I pressed jump at the edge" is generous and the simulation's is not.
  f32 coyoteTime     = 0.12f;   // jump still allowed after leaving ground
  f32 jumpBufferTime = 0.14f;   // jump pressed before landing still fires

  // --- slide
  f32 slideEntrySpeed = 5.6f;   // crouch above this and it is a slide
  f32 slideFriction   = 2.4f;   // much lower than ground: you keep the speed
  f32 slideMinSpeed   = 3.0f;   // below this the slide ends
  f32 slideBoost      = 1.15f;  // one-shot multiplier on entry
};

// What the controller consumes. Normalised, device-agnostic: the platform
// layer converts keyboard/gamepad into this, and nothing below here knows
// which it was.
struct MovementInput {
  f32  moveForward = 0.0f;   // [-1, 1]
  f32  moveRight   = 0.0f;   // [-1, 1]
  f32  yawRadians  = 0.0f;   // where the camera is looking
  bool jump        = false;  // edge, not level: set on the press
  bool sprint      = false;
  bool crouch      = false;
};

// Ground query result. The controller does not own collision — it asks. Swap
// in a capsule sweep against your broadphase; the reference implementation
// used by the unit tests is a flat plane at y = 0.
struct GroundProbe {
  bool  grounded     = false;
  Vec3  normal{0.0f, 1.0f, 0.0f};
  f32   distance     = 0.0f;   // to the surface below
};

class PlayerController {
 public:
  explicit PlayerController(MovementConfig cfg = {}) noexcept : cfg_(cfg) {}

  [[nodiscard]] const MovementConfig& config() const noexcept { return cfg_; }
  void setConfig(const MovementConfig& cfg) noexcept { cfg_ = cfg; }

  // One fixed step. Mutates velocity and the character's flag set; does not
  // move the transform — integration and collision resolution belong to the
  // physics step that runs after this, so that movement and collision can be
  // tested independently.
  void step(ecs::RigidBody& body, ecs::CharacterState& state,
            const MovementInput& input, const GroundProbe& ground, f32 dt) noexcept;

  // Exposed for tests and for anyone building a different controller on the
  // same primitives.
  static void applyFriction(Vec3& velocity, f32 friction, f32 stopSpeed, f32 dt) noexcept;
  static void accelerate(Vec3& velocity, const Vec3& wishDir, f32 wishSpeed,
                         f32 accel, f32 dt) noexcept;

 private:
  MovementConfig cfg_;
};

}  // namespace erebus::game
