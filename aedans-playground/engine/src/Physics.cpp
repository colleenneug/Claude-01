#include "playground/engine/Physics.h"

#include "playground/core/Log.h"
#include "playground/engine/Components.h"

#include <vector>

namespace playground::engine {
namespace {

using log::Category;

bool samePose(glm::vec3 p, glm::quat r, glm::vec3 q, glm::quat s) {
  return p == q && r.x == s.x && r.y == s.y && r.z == s.z && r.w == s.w;
}

}  // namespace

std::optional<physics::BodyId> PhysicsSystem::bodyOf(ecs::Entity entity) const {
  const auto it = bodies_.find(entity.index);
  if (it == bodies_.end() || it->second.entity != entity) return std::nullopt;
  return it->second.body;
}

// Bodies and characters whose entity is gone, or no longer has what made it one.
void PhysicsSystem::forgetStale(ecs::World& world) {
  for (auto it = bodies_.begin(); it != bodies_.end();) {
    const ecs::Entity e = it->second.entity;
    if (world.alive(e) && world.has<Transform3D>(e) && world.has<RigidBody>(e) && world.has<Collider>(e)) {
      ++it;
      continue;
    }
    physics_.removeBody(it->second.body);
    if (RigidBody* rb = world.alive(e) ? world.tryGet<RigidBody>(e) : nullptr) rb->body = {};
    it = bodies_.erase(it);
  }
  for (auto it = characters_.begin(); it != characters_.end();) {
    const ecs::Entity e = it->second.entity;
    if (world.alive(e) && world.has<Transform3D>(e) && world.has<CharacterController>(e)) {
      ++it;
      continue;
    }
    physics_.removeCharacter(it->second.character);
    if (CharacterController* cc = world.alive(e) ? world.tryGet<CharacterController>(e) : nullptr) cc->character = {};
    it = characters_.erase(it);
  }
  for (auto it = failed_.begin(); it != failed_.end();) {  // gone: a new entity may reuse the index
    it = world.alive(it->second) ? std::next(it) : failed_.erase(it);
  }
}

void PhysicsSystem::createNew(ecs::World& world) {
  world.each<Transform3D, RigidBody, Collider>([&](ecs::Entity e, Transform3D& t, RigidBody& rb, Collider& collider) {
    if (bodies_.contains(e.index) || hasFailed(e)) return;
    auto body = physics_.addBody({.shape = collider.shape, .motion = rb.motion, .layer = rb.layer,
                                  .position = t.position, .rotation = t.rotation, .mass = rb.mass,
                                  .friction = rb.friction, .restitution = rb.restitution,
                                  .linearDamping = rb.linearDamping, .angularDamping = rb.angularDamping,
                                  .gravityFactor = rb.gravityFactor, .linearVelocity = rb.initialVelocity},
                                 e);
    if (!body) {
      log::error(Category::Engine, "physics: entity {} gets no body: {}", e.index, body.error().message);
      failed_[e.index] = e;
      return;
    }
    rb.body = *body;
    bodies_.emplace(e.index, TrackedBody{.entity = e, .body = *body, .motion = rb.motion, .position = t.position,
                                         .rotation = t.rotation});
    t.snapshot();  // where it starts: nothing to blend from
  });
  world.each<Transform3D, CharacterController>([&](ecs::Entity e, Transform3D& t, CharacterController& cc) {
    if (characters_.contains(e.index) || hasFailed(e)) return;
    auto character = physics_.addCharacter({.radius = cc.radius, .height = cc.height, .position = t.position,
                                            .maxSlopeDegrees = cc.maxSlopeDegrees, .stepHeight = cc.stepHeight},
                                           e);
    if (!character) {
      log::error(Category::Engine, "physics: entity {} gets no character: {}", e.index, character.error().message);
      failed_[e.index] = e;
      return;
    }
    cc.character = *character;
    characters_.emplace(e.index, TrackedCharacter{.entity = e, .character = *character, .position = t.position});
    t.snapshot();
  });
}

void PhysicsSystem::step(ecs::World& world, f32 dt) {
  forgetStale(world);
  createNew(world);

  // 2. The game's changes in.
  std::vector<u32> teleported;
  for (auto& [index, tracked] : bodies_) {
    const Transform3D& t = world.get<Transform3D>(tracked.entity);
    if (samePose(t.position, t.rotation, tracked.position, tracked.rotation)) continue;
    if (tracked.motion == physics::MotionType::Kinematic) {
      physics_.moveKinematic(tracked.body, t.position, t.rotation, dt);  // glides there this step, pushing
    } else {
      physics_.setTransform(tracked.body, t.position, t.rotation);  // jumps there
      teleported.push_back(index);
    }
  }
  for (auto& [index, tracked] : characters_) {
    Transform3D& t = world.get<Transform3D>(tracked.entity);
    CharacterController& cc = world.get<CharacterController>(tracked.entity);
    const bool jumped = t.position != tracked.position;
    if (jumped) physics_.setCharacterPosition(tracked.character, t.position);
    const physics::CharacterState state =
        physics_.moveCharacter(tracked.character, {.velocity = cc.moveVelocity, .jumpSpeed = cc.jumpSpeed}, dt);
    cc.jumpSpeed = 0.0f;  // one jump per request
    cc.ground = state.ground;
    cc.velocity = state.velocity;
    t.previousPosition = jumped ? state.position : tracked.position;
    t.previousRotation = t.rotation;  // the game turns characters; nothing to blend here
    t.hasPrevious = true;
    t.position = tracked.position = state.position;
  }

  // 3. The step.
  physics_.step(dt);

  // 4. Physics's changes out.
  usize next = 0;
  for (auto& [index, tracked] : bodies_) {
    const bool wasTeleported = next < teleported.size() && teleported[next] == index;
    if (wasTeleported) ++next;
    Transform3D& t = world.get<Transform3D>(tracked.entity);
    if (tracked.motion == physics::MotionType::Static) {
      t.snapshot();
      tracked.position = t.position;
      tracked.rotation = t.rotation;
      continue;
    }
    const auto state = physics_.bodyState(tracked.body);
    if (!state) continue;
    t.previousPosition = wasTeleported ? t.position : tracked.position;
    t.previousRotation = wasTeleported ? t.rotation : tracked.rotation;
    t.hasPrevious = true;
    t.position = tracked.position = state->position;
    t.rotation = tracked.rotation = state->rotation;
  }
}

}  // namespace playground::engine
