#pragma once
// =============================================================================
//  erebus/ecs/Components.h — the component set the shipped systems agree on.
//
//  Components are data. No methods beyond trivial accessors, no virtuals, no
//  owning pointers. Anything that needs to *happen* is a system that walks a
//  pool; see src/game/PlayerController.cpp for the shape.
//
//  Sizes are noted because they are a design constraint, not trivia: a system
//  that walks 10,000 entities touches size*10,000 bytes, and whether that fits
//  in L2 decides whether it costs 40us or 400us.
// =============================================================================

#include "erebus/core/Types.h"
#include "erebus/ecs/Entity.h"

namespace erebus::ecs {

// 48 bytes. Holds BOTH the current and previous position because the renderer
// interpolates between them by GameLoop's alpha — a fixed-timestep simulation
// displayed at a higher refresh rate judders visibly without it, and storing
// the previous state next to the current one keeps the interpolation a single
// cache line read rather than a second pool lookup.
struct alignas(16) Transform {
  Vec3 position{};
  Vec3 previousPosition{};
  Vec3 scale{1.0f, 1.0f, 1.0f};
  // Orientation as a quaternion in xyzw. Euler angles here would reintroduce
  // gimbal lock in the one place it actually bites: a player looking straight
  // up while strafing.
  f32 rotation[4] = {0.0f, 0.0f, 0.0f, 1.0f};

  // Called once per fixed step, before integration, by whichever system owns
  // movement. Forgetting it is the classic "everything interpolates from the
  // origin" bug on the first frame after a teleport.
  void beginStep() noexcept { previousPosition = position; }
};

// 32 bytes. Momentum state, separated from Transform because plenty of things
// have a transform and no dynamics (static cover, light probes, decals) and a
// system that only reads transforms should not be dragging velocity through
// cache with it.
struct alignas(16) RigidBody {
  Vec3 velocity{};
  f32  mass = 80.0f;             // kg; a person
  f32  dragCoefficient = 0.0f;   // quadratic air drag, 0 for characters
  u32  flags = 0;
  f32  _pad[2] = {};
};

// What a character controller needs that a rigid body does not express.
// 32 bytes.
struct alignas(16) CharacterState {
  f32 eyeHeight   = 1.68f;
  f32 radius      = 0.42f;
  f32 height      = 1.80f;
  f32 coyoteTimer = 0.0f;   // seconds of grace after leaving ground
  f32 jumpBuffer  = 0.0f;   // seconds a jump press stays queued
  u32 flags       = 0;      // see CharacterFlags
  f32 _pad[2]     = {};
};

enum CharacterFlags : u32 {
  kGrounded   = 1u << 0,
  kCrouching  = 1u << 1,
  kSprinting  = 1u << 2,
  kSliding    = 1u << 3,
  kWallRunning= 1u << 4,
};

// A draw submission. Indices rather than pointers: the render thread reads
// these from a snapshot taken at the sync point, and a pointer into
// simulation-owned memory is a data race waiting for a scheduler hiccup.
struct alignas(16) RenderMesh {
  u32 meshId     = 0;
  u32 materialId = 0;
  u32 lodBias    = 0;
  u32 flags      = 0;   // casts shadow, receives decals, is skinned...
};

// A dynamic light. `radius` is the culling bound used by cluster assignment
// (render/ClusteredLighting.h) — set it from the intensity and a cutoff
// threshold rather than by eye, or the binning either wastes clusters on
// lights that contribute nothing or clips ones that do.
struct alignas(16) PointLight {
  Vec3 colour{1.0f, 1.0f, 1.0f};
  f32  intensity = 1.0f;    // candela
  f32  radius    = 10.0f;   // metres; the influence sphere
  f32  _pad[3]   = {};
};

}  // namespace erebus::ecs
