#pragma once
// =============================================================================
//  playground/engine/Physics.h — physics for ECS entities.
//
//  Components (plain data, like the rest):
//    RigidBody            how it moves: static, kinematic or dynamic, and its
//                         mass, friction, bounciness;
//    Collider             its shape: box, sphere, capsule or triangle mesh;
//    CharacterController  a walking capsule (physics::PhysicsWorld's
//                         character: slopes, stairs, wall sliding) moved by
//                         the game each step.
//  With a Transform3D, an entity with RigidBody + Collider gets a body, and
//  one with a CharacterController gets a character.
//
//  PhysicsSystem::step(world, dt), once per fixed step, keeps the two sides
//  in step:
//    1. entities that are new get bodies and characters; those destroyed
//       (or that lost a component) lose theirs;
//    2. what the GAME changed goes in: a Transform3D moved by the game since
//       the last step (anything but physics writing it) is a teleport for a
//       static or dynamic body and the target of the step for a kinematic
//       one; a character's moveVelocity and jumpSpeed move it;
//    3. the physics steps;
//    4. what PHYSICS changed comes out: each moved entity's Transform3D,
//       with previousPosition/previousRotation set to where it was before
//       this step (or to where it was teleported), so interpolatedMatrix()
//       blends exactly the motion of this step.
//  Entities with a Transform3D but no physics are left alone.
//
//  Collider and RigidBody settings are read when the body is created;
//  change them by removing the component and adding it again.
// =============================================================================

#include "playground/core/Types.h"
#include "playground/ecs/World.h"
#include "playground/physics/PhysicsWorld.h"

#include <map>
#include <optional>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace playground::engine {

struct RigidBody {
  physics::MotionType motion = physics::MotionType::Dynamic;
  f32 mass = 0.0f;  // kg; 0: from the shape's volume
  f32 friction = 0.5f;
  f32 restitution = 0.0f;
  f32 linearDamping = 0.05f;
  f32 angularDamping = 0.05f;
  f32 gravityFactor = 1.0f;
  std::optional<physics::Layer> layer;
  glm::vec3 initialVelocity{0.0f};

  physics::BodyId body;  // set by PhysicsSystem (read-only for the game)
};

struct Collider {
  physics::ShapeDesc shape;
};

struct CharacterController {
  f32 radius = 0.3f;
  f32 height = 1.8f;  // the entity's Transform3D position is the feet
  f32 maxSlopeDegrees = 45.0f;
  f32 stepHeight = 0.35f;

  // Set by the game before each step:
  glm::vec3 moveVelocity{0.0f};  // wanted horizontal velocity, m/s
  f32 jumpSpeed = 0.0f;          // > 0: jump this step if on the ground (cleared after the step)

  // Set by PhysicsSystem after each step:
  physics::CharacterId character;
  physics::GroundState ground = physics::GroundState::InAir;
  glm::vec3 velocity{0.0f};
};

class PhysicsSystem {
 public:
  explicit PhysicsSystem(physics::PhysicsWorld& physics) noexcept : physics_(physics) {}

  // One fixed step of `dt` seconds (see the top of this file).
  void step(ecs::World& world, f32 dt);

  [[nodiscard]] physics::PhysicsWorld& physics() noexcept { return physics_; }
  [[nodiscard]] usize bodyCount() const noexcept { return bodies_.size(); }
  [[nodiscard]] usize characterCount() const noexcept { return characters_.size(); }
  // The body of an entity, if it has one.
  [[nodiscard]] std::optional<physics::BodyId> bodyOf(ecs::Entity entity) const;
  // Drops the entity's body or character; the next step makes a new one
  // from its components as they are now (after editing RigidBody,
  // Collider or CharacterController settings).
  void rebuild(ecs::Entity entity);

 private:
  struct TrackedBody {
    ecs::Entity entity;
    physics::BodyId body;
    physics::MotionType motion = physics::MotionType::Dynamic;
    glm::vec3 position{0.0f};  // what physics last wrote (or the game last set)
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  };
  struct TrackedCharacter {
    ecs::Entity entity;
    physics::CharacterId character;
    glm::vec3 position{0.0f};
  };

  void forgetStale(ecs::World& world);
  void createNew(ecs::World& world);

  physics::PhysicsWorld& physics_;
  // By entity index; ordered, so bodies are always visited in the same
  // order (the physics is deterministic only if its inputs are).
  std::map<u32, TrackedBody> bodies_;
  std::map<u32, TrackedCharacter> characters_;
  std::map<u32, ecs::Entity> failed_;  // entities whose body could not be made (logged once, not retried)
  [[nodiscard]] bool hasFailed(ecs::Entity e) const {
    const auto it = failed_.find(e.index);
    return it != failed_.end() && it->second == e;
  }
};

}  // namespace playground::engine
