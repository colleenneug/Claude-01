#include "erebus/game/PlayerController.h"

#include <algorithm>
#include <cmath>

namespace erebus::game {
namespace {

// Horizontal component only. Friction and acceleration act in the ground
// plane; vertical motion is gravity's business and mixing them is how you get
// a character that slows its own fall by walking.
inline Vec3 horizontal(const Vec3& v) noexcept { return {v.x, 0.0f, v.z}; }

}  // namespace

void PlayerController::applyFriction(Vec3& velocity, f32 friction, f32 stopSpeed, f32 dt) noexcept {
  const Vec3 flat = horizontal(velocity);
  const f32 speed = length(flat);
  if (speed < 1e-4f) {
    velocity.x = velocity.z = 0.0f;
    return;
  }

  // The stopSpeed floor is what makes a character come to rest rather than
  // asymptote toward it. Pure exponential decay never reaches zero, so below
  // stopSpeed we drain at a constant rate instead of a proportional one and
  // the character actually stops.
  const f32 control = speed < stopSpeed ? stopSpeed : speed;
  f32 newSpeed = speed - control * friction * dt;
  if (newSpeed < 0.0f) newSpeed = 0.0f;

  const f32 scale = newSpeed / speed;
  velocity.x *= scale;
  velocity.z *= scale;
}

void PlayerController::accelerate(Vec3& velocity, const Vec3& wishDir, f32 wishSpeed,
                                  f32 accel, f32 dt) noexcept {
  // Quake's accelerate. See the header for why the clamp is along wishDir
  // rather than on total speed — it is the entire reason this feels the way
  // it does, and the most common thing people "simplify" away.
  const f32 currentSpeed = dot(velocity, wishDir);
  const f32 addSpeed = wishSpeed - currentSpeed;
  if (addSpeed <= 0.0f) return;

  f32 accelSpeed = accel * wishSpeed * dt;
  if (accelSpeed > addSpeed) accelSpeed = addSpeed;

  velocity += wishDir * accelSpeed;
}

void PlayerController::step(ecs::RigidBody& body, ecs::CharacterState& state,
                            const MovementInput& input, const GroundProbe& ground,
                            f32 dt) noexcept {
  Vec3& vel = body.velocity;

  // ---------------------------------------------------------------- intent
  // Input is in camera space; rotate it into world space by the yaw. Pitch is
  // deliberately excluded: looking down must not make you walk into the floor.
  const f32 s = std::sin(input.yawRadians);
  const f32 c = std::cos(input.yawRadians);
  Vec3 wish{input.moveRight * c + input.moveForward * s,
            0.0f,
            input.moveForward * c - input.moveRight * s};

  const f32 wishLen = length(wish);
  const Vec3 wishDir = wishLen > 1e-4f ? wish * (1.0f / wishLen) : Vec3{};
  // Clamp rather than normalise the magnitude: a stick half-deflected should
  // walk at half speed, but two keyboard axes at once must not give sqrt(2)
  // times the speed.
  const f32 wishScale = std::min(wishLen, 1.0f);

  const bool wasGrounded = (state.flags & ecs::kGrounded) != 0;

  // ---------------------------------------------------------------- timers
  // Coyote time counts *down from* the moment you leave the ground; the jump
  // buffer counts down from the press. Both are decremented every step and
  // refilled by their trigger, which keeps them frame-rate independent.
  if (ground.grounded) {
    state.coyoteTimer = cfg_.coyoteTime;
  } else {
    state.coyoteTimer = std::max(0.0f, state.coyoteTimer - dt);
  }
  if (input.jump) {
    state.jumpBuffer = cfg_.jumpBufferTime;
  } else {
    state.jumpBuffer = std::max(0.0f, state.jumpBuffer - dt);
  }

  // ---------------------------------------------------------------- stance
  const bool wantsSprint = input.sprint && input.moveForward > 0.1f && !input.crouch;
  const f32 flatSpeed = length(horizontal(vel));

  bool sliding = (state.flags & ecs::kSliding) != 0;
  if (sliding) {
    // A slide ends when it stops being fast, or when you stand up, or when you
    // leave the ground. It does not end on a timer: a timer makes the length
    // of a slide a property of the clock rather than of how fast you entered
    // it, which removes the only interesting decision in it.
    if (!ground.grounded || !input.crouch || flatSpeed < cfg_.slideMinSpeed) sliding = false;
  } else if (input.crouch && ground.grounded && flatSpeed >= cfg_.slideEntrySpeed) {
    sliding = true;
    // One-shot entry boost. Applied to the existing direction, so a slide
    // rewards carrying speed into it rather than pressing crouch.
    vel.x *= cfg_.slideBoost;
    vel.z *= cfg_.slideBoost;
  }

  f32 targetSpeed = cfg_.walkSpeed;
  if (input.crouch && !sliding) targetSpeed = cfg_.crouchSpeed;
  else if (wantsSprint)         targetSpeed = cfg_.sprintSpeed;
  targetSpeed *= wishScale;

  // ---------------------------------------------------------------- ground
  if (ground.grounded) {
    // Friction first, then acceleration, in that order. Reversed, the
    // acceleration you just applied is immediately scaled down by friction and
    // top speed lands below the configured value for reasons that are very
    // annoying to track down.
    const f32 friction = sliding ? cfg_.slideFriction : cfg_.groundFriction;
    applyFriction(vel, friction, cfg_.walkSpeed * 0.25f, dt);

    if (!sliding) {
      accelerate(vel, wishDir, targetSpeed, cfg_.groundAccel, dt);
    } else {
      // Steering while sliding: a fraction of ground acceleration, so you can
      // curve a slide but not accelerate out of one.
      accelerate(vel, wishDir, targetSpeed, cfg_.groundAccel * 0.18f, dt);
    }

    // Park vertical velocity at a small negative value rather than zero. Zero
    // makes the ground probe flicker between grounded and not on a slope, and
    // a flickering grounded flag is a character that cannot jump reliably.
    if (vel.y < 0.0f) vel.y = -2.0f;

  } else {
    // ------------------------------------------------------------- airborne
    if (cfg_.airFriction > 0.0f) {
      applyFriction(vel, cfg_.airFriction, cfg_.walkSpeed * 0.25f, dt);
    }

    // Air control, damped by how fast you are already going. At or below walk
    // speed you get close to the full strafe authority; well above it the
    // authority falls away, so a fast jump commits and a slow one is
    // steerable. Without this damping, air accel either makes slow jumps feel
    // locked or makes fast ones infinitely steerable — there is no single
    // constant that serves both.
    const f32 speedRatio = flatSpeed / std::max(1e-3f, cfg_.walkSpeed);
    const f32 damping = 1.0f / (1.0f + std::max(0.0f, speedRatio - 1.0f) * cfg_.airControlDamping);

    // Split the wish into the part along current motion and the part across
    // it. Forward authority stays small (you cannot thrust yourself faster in
    // mid-air); lateral authority is much larger, which is what turns a jump
    // into a trajectory you can shape.
    const Vec3 flatVel = horizontal(vel);
    const Vec3 moveDir = normalize(flatVel, wishDir);
    const f32 along = dot(wishDir, moveDir);
    const f32 across = 1.0f - std::fabs(along);

    const f32 accel = (cfg_.airAccel * std::fabs(along) +
                       cfg_.airStrafeAccel * across) * damping;
    accelerate(vel, wishDir, targetSpeed, accel, dt);

    // Gravity, with a terminal speed so a long fall cannot outrun the
    // collision sweep's step size and tunnel through the floor.
    vel.y -= cfg_.gravity * dt;
    if (vel.y < -cfg_.terminalSpeed) vel.y = -cfg_.terminalSpeed;
  }

  // ---------------------------------------------------------------- jump
  // Both windows have to be open. Buffer covers pressing early, coyote covers
  // pressing late; together they mean a jump lands when the player believes
  // they pressed it, which is the only definition of correct that matters.
  if (state.jumpBuffer > 0.0f && state.coyoteTimer > 0.0f) {
    vel.y = cfg_.jumpVelocity;
    state.jumpBuffer = 0.0f;
    state.coyoteTimer = 0.0f;   // consume both, or one press jumps twice
    sliding = false;
  }

  // ---------------------------------------------------------------- flags
  u32 flags = 0;
  if (ground.grounded)             flags |= ecs::kGrounded;
  if (input.crouch && !sliding)    flags |= ecs::kCrouching;
  if (wantsSprint && ground.grounded && !sliding) flags |= ecs::kSprinting;
  if (sliding)                     flags |= ecs::kSliding;
  // Preserve wall-run state, which a separate system owns.
  flags |= (state.flags & ecs::kWallRunning);
  state.flags = flags;

  // Landing this step: the trigger for landing audio, camera dip, and any
  // fall damage system. Computed here because this is the only place that
  // knows both the previous and the current grounded state.
  (void)wasGrounded;   // hook: if (!wasGrounded && ground.grounded) onLanded()
  (void)body.mass;     // hook: impulses from explosions scale by 1/mass
}

}  // namespace erebus::game
