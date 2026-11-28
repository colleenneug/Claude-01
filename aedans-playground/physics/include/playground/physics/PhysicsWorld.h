#pragma once
// =============================================================================
//  playground/physics/PhysicsWorld.h — rigid bodies, characters and ray
//  queries, by Jolt Physics, behind an interface of our own.
//
//  No Jolt header appears here (the implementation is behind a pointer):
//  everything that includes this sees glm vectors and our own small types,
//  and only physics/src is compiled against Jolt.
//
//  UNITS: metres, kilograms, seconds; +y is up; gravity -9.81 m/s² by default.
//
//  BODIES are rigid shapes (box, sphere, capsule, or a triangle mesh) that
//  are one of:
//    Static     never move (floors, walls, level geometry);
//    Kinematic  moved by the game (moveKinematic: lifts, doors), pushing
//               dynamic bodies out of the way but never pushed back;
//    Dynamic    moved by forces, gravity and collisions (crates, debris).
//  Each body may name the ECS entity it belongs to, which raycasts report.
//
//  CHARACTERS (JPH::CharacterVirtual) are not bodies: a capsule moved by
//  the game with a desired velocity each step, which walks up slopes up to
//  maxSlopeDegrees, steps up stairs up to stepHeight, slides along walls,
//  sticks to the floor going down slopes and steps, pushes dynamic bodies
//  (with maxStrength) and falls under gravity when unsupported. The
//  smooth, predictable movement a player expects, which a dynamic body
//  rolling around cannot give.
//
//  LAYERS: every body is in a Layer; a LayerMask selects layers for queries.
//    Static  collides with Moving and Debris (not with itself: nothing to do)
//    Moving  collides with everything except Debris
//    Debris  collides with Static only (small pieces that should not block)
//  Characters move as Moving objects (so Debris does not block them).
//
//  STEPPING: step(dt) advances the world by dt (the game's fixed 1/120 s).
//  With a JobSystem, the step's work is spread over its workers (through a
//  bridge to Jolt's job interface); without, it runs on the caller's thread.
//  Results are the same either way, and on every machine (Jolt is built
//  cross-platform deterministic): the same bodies created in the same order
//  with the same inputs give the same numbers, bit for bit.
//
//  ONE THREAD calls a PhysicsWorld's functions (queries included).
//
//      auto physics = physics::PhysicsWorld::create({}, &jobs);
//      auto floor = physics->addBody({.shape = physics::ShapeDesc::box({10, 0.5f, 10}),
//                                     .motion = physics::MotionType::Static, .position = {0, -0.5f, 0}});
//      auto crate = physics->addBody({.shape = physics::ShapeDesc::box(glm::vec3(0.5f)),
//                                     .position = {0, 5, 0}}, crateEntity);
//      physics->step(1.0f / 120.0f);
//      if (auto hit = physics->raycast(eye, forward, 100.0f)) shoot(hit->entity);
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"
#include "playground/ecs/Entity.h"

#include <memory>
#include <optional>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace playground {
class JobSystem;
}

namespace playground::physics {

enum class MotionType : u8 { Static, Kinematic, Dynamic };

enum class Layer : u8 { Static = 0, Moving = 1, Debris = 2 };
inline constexpr u32 kLayerCount = 3;

using LayerMask = u32;
inline constexpr LayerMask kAllLayers = ~LayerMask{0};
[[nodiscard]] constexpr LayerMask maskOf(Layer layer) noexcept { return LayerMask{1} << static_cast<u32>(layer); }

enum class ShapeType : u8 { Box, Sphere, Capsule, Mesh };

// A collision shape, centred on the body's position.
struct ShapeDesc {
  ShapeType type = ShapeType::Box;
  glm::vec3 halfExtents{0.5f};  // Box
  f32 radius = 0.5f;            // Sphere, Capsule
  f32 halfHeight = 0.5f;        // Capsule: half the straight part, along y (total height 2 * (halfHeight + radius))
  // Mesh: triangles (three indices each) over these vertices. Static or
  // kinematic bodies only (a mesh has no volume, so no mass).
  std::vector<glm::vec3> vertices;
  std::vector<u32> indices;

  [[nodiscard]] static ShapeDesc box(glm::vec3 halfExtents) { return {.type = ShapeType::Box, .halfExtents = halfExtents}; }
  [[nodiscard]] static ShapeDesc sphere(f32 radius) { return {.type = ShapeType::Sphere, .radius = radius}; }
  [[nodiscard]] static ShapeDesc capsule(f32 halfHeight, f32 radius) {
    return {.type = ShapeType::Capsule, .radius = radius, .halfHeight = halfHeight};
  }
  [[nodiscard]] static ShapeDesc mesh(std::vector<glm::vec3> vertices, std::vector<u32> indices) {
    return {.type = ShapeType::Mesh, .vertices = std::move(vertices), .indices = std::move(indices)};
  }
};

struct BodyDesc {
  ShapeDesc shape;
  MotionType motion = MotionType::Dynamic;
  std::optional<Layer> layer;  // default: Static for static bodies, Moving otherwise
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  f32 mass = 0.0f;         // kg; 0: from the shape's volume at the density of water (1000 kg/m³)
  f32 friction = 0.5f;     // 0 ice .. 1 rubber
  f32 restitution = 0.0f;  // bounciness: 0 none .. 1 bounces back as fast as it came
  f32 linearDamping = 0.05f;
  f32 angularDamping = 0.05f;
  f32 gravityFactor = 1.0f;
  glm::vec3 linearVelocity{0.0f};
  glm::vec3 angularVelocity{0.0f};
};

// A body in a PhysicsWorld. Stale ids (of removed bodies) match nothing.
struct BodyId {
  u32 value = 0xFFFFFFFFu;
  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0xFFFFFFFFu; }
  friend constexpr bool operator==(BodyId, BodyId) = default;
};

struct BodyState {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  glm::vec3 linearVelocity{0.0f};
  glm::vec3 angularVelocity{0.0f};
  bool active = false;  // false: asleep (at rest), or static
};

struct CharacterDesc {
  f32 radius = 0.3f;
  f32 height = 1.8f;  // total, feet to top of head; the position is the FEET
  glm::vec3 position{0.0f};
  f32 maxSlopeDegrees = 45.0f;  // steeper ground is a wall
  f32 stepHeight = 0.35f;       // steps up to this high are climbed
  f32 stickDownDistance = 0.5f; // walking off a step or down a slope keeps it on the ground
  f32 mass = 70.0f;             // kg, for pushing dynamic bodies
  f32 maxStrength = 100.0f;     // N, the most it pushes with
};

struct CharacterId {
  u32 value = 0;  // 0: none
  [[nodiscard]] constexpr bool valid() const noexcept { return value != 0; }
  friend constexpr bool operator==(CharacterId, CharacterId) = default;
};

enum class GroundState : u8 {
  OnGround,       // standing on walkable ground
  OnSteepGround,  // touching ground too steep to stand on (it slides)
  NotSupported,   // touching something, but nothing below
  InAir,          // touching nothing
};

// What the game asks a character to do this step.
struct CharacterMove {
  glm::vec3 velocity{0.0f};  // wanted horizontal velocity, m/s (y is ignored)
  f32 jumpSpeed = 0.0f;      // > 0: jump with this upward speed, if on the ground
};

struct CharacterState {
  glm::vec3 position{0.0f};  // feet
  glm::vec3 velocity{0.0f};
  GroundState ground = GroundState::InAir;
  glm::vec3 groundNormal{0.0f, 1.0f, 0.0f};
  ecs::Entity groundEntity;  // what it stands on, if that body has an entity
};

struct RaycastHit {
  ecs::Entity entity;  // the body's entity (invalid if it was added without one)
  BodyId body;
  glm::vec3 point{0.0f};
  glm::vec3 normal{0.0f, 1.0f, 0.0f};  // the surface's, facing the ray
  f32 distance = 0.0f;
};

struct PhysicsWorldDesc {
  u32 maxBodies = 4096;
  u32 maxBodyPairs = 8192;
  u32 maxContactConstraints = 4096;
  glm::vec3 gravity{0.0f, -9.81f, 0.0f};
  usize tempMemoryBytes = 8u * 1024u * 1024u;  // per-step scratch memory
};

struct PhysicsStats {
  u64 steps = 0;
  usize bodies = 0;
  usize activeBodies = 0;
  usize characters = 0;
};

class PhysicsWorld {
 public:
  // `jobs`: spread each step over these workers (may be null: one thread).
  // The JobSystem must outlive the PhysicsWorld.
  [[nodiscard]] static Result<std::unique_ptr<PhysicsWorld>> create(const PhysicsWorldDesc& desc = {},
                                                                    JobSystem* jobs = nullptr);
  ~PhysicsWorld();
  PhysicsWorld(const PhysicsWorld&) = delete;
  PhysicsWorld& operator=(const PhysicsWorld&) = delete;

  // Errc::InvalidArgument for a shape that cannot be built (a zero size, a
  // mesh with no triangles or on a dynamic body); Errc::InvalidState when
  // maxBodies is reached.
  [[nodiscard]] Result<BodyId> addBody(const BodyDesc& desc, ecs::Entity entity = {});
  void removeBody(BodyId body);
  [[nodiscard]] bool contains(BodyId body) const;
  [[nodiscard]] std::optional<BodyState> bodyState(BodyId body) const;
  [[nodiscard]] ecs::Entity entityOf(BodyId body) const;

  // Puts a body somewhere at once (a teleport: no velocity from the jump).
  void setTransform(BodyId body, glm::vec3 position, glm::quat rotation);
  // Kinematic bodies: reach this pose at the end of the next step of `dt`,
  // moving (and pushing) on the way.
  void moveKinematic(BodyId body, glm::vec3 position, glm::quat rotation, f32 dt);
  void setVelocity(BodyId body, glm::vec3 linear, glm::vec3 angular = glm::vec3(0.0f));
  // An instant push (N·s) at a world point; wakes the body.
  void addImpulse(BodyId body, glm::vec3 impulse, glm::vec3 atPoint);

  [[nodiscard]] Result<CharacterId> addCharacter(const CharacterDesc& desc, ecs::Entity entity = {});
  void removeCharacter(CharacterId character);
  // One step of movement for a character (call once per step, before step()).
  CharacterState moveCharacter(CharacterId character, const CharacterMove& move, f32 dt);
  [[nodiscard]] std::optional<CharacterState> characterState(CharacterId character) const;
  void setCharacterPosition(CharacterId character, glm::vec3 feet);

  // Advances every body by `dt` seconds.
  void step(f32 dt);

  // The first body hit by a ray from `origin` along `direction` (need not
  // be unit length) within `maxDistance`, among the layers in `mask`.
  // Characters are not bodies and are not hit. A ray starting inside a
  // body hits that body at distance 0.
  [[nodiscard]] std::optional<RaycastHit> raycast(glm::vec3 origin, glm::vec3 direction, f32 maxDistance,
                                                  LayerMask mask = kAllLayers) const;

  [[nodiscard]] glm::vec3 gravity() const noexcept;
  [[nodiscard]] PhysicsStats stats() const;
  [[nodiscard]] bool multithreaded() const noexcept;

 private:
  struct Impl;
  explicit PhysicsWorld(std::unique_ptr<Impl> impl) noexcept;
  std::unique_ptr<Impl> impl_;
};

}  // namespace playground::physics
