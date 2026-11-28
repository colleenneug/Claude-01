#include "playground/engine/Systems.h"

#include "playground/engine/Components.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/common.hpp>  // glm::abs
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace playground::engine {

void snapshotTransforms(ecs::World& world) {
  world.each<Transform>([](ecs::Entity, Transform& transform) {
    transform.previousPosition = transform.position;
    transform.previousRotation = transform.rotation;
    transform.hasPrevious = true;
  });
}

void movementSystem(ecs::World& world, f32 deltaSeconds) {
  world.each<Transform, Velocity>([deltaSeconds](ecs::Entity, Transform& transform, const Velocity& velocity) {
    transform.position += velocity.linear * deltaSeconds;
    transform.rotation += velocity.angular * deltaSeconds;
  });
}

void inputSystem(ecs::World& world, const ActionMap& actions, const platform::InputState& input) {
  const glm::vec2 move = actions.axis2D(input, actions::kMove);
  world.each<PlayerTag, Velocity>([move](ecs::Entity, const PlayerTag& player, Velocity& velocity) {
    velocity.linear = move * player.speed;
  });
}

void lifetimeSystem(ecs::World& world, f32 deltaSeconds) {
  world.each<Lifetime>([&world, deltaSeconds](ecs::Entity entity, Lifetime& lifetime) {
    lifetime.secondsLeft -= deltaSeconds;
    if (lifetime.secondsLeft <= 0.0f) world.destroy(entity);  // deferred: safe mid-iteration
  });
}

void bounceSystem(ecs::World& world, const WorldBounds& bounds) {
  const glm::vec2 halfArea = bounds.halfSize;
  world.each<Transform, Velocity, Sprite>(
      [halfArea](ecs::Entity, Transform& transform, Velocity& velocity, const Sprite& sprite) {
        const glm::vec2 halfExtent = 0.5f * sprite.size * glm::abs(transform.scale);
        for (int axis = 0; axis < 2; ++axis) {
          const f32 limit = std::fmax(halfArea[axis] - halfExtent[axis], 0.0f);
          // Only reflect a velocity that points further out, so a sprite
          // pushed past the edge (the bounds shrank) comes back in smoothly
          // instead of flipping direction every frame.
          if (transform.position[axis] > limit) {
            transform.position[axis] = limit;
            if (velocity.linear[axis] > 0.0f) velocity.linear[axis] = -velocity.linear[axis];
          } else if (transform.position[axis] < -limit) {
            transform.position[axis] = -limit;
            if (velocity.linear[axis] < 0.0f) velocity.linear[axis] = -velocity.linear[axis];
          }
        }
      });
}

std::optional<ecs::Entity> pickSprite(ecs::World& world, glm::vec2 point) {
  std::optional<ecs::Entity> best;
  f32 bestLayer = 0.0f;
  world.each<Transform, Sprite>([&](ecs::Entity entity, const Transform& transform, const Sprite& sprite) {
    // Into the sprite's own frame: undo its position and rotation, then
    // compare with its half-size.
    const glm::vec2 offset = point - transform.position;
    const f32 c = std::cos(-transform.rotation);
    const f32 s = std::sin(-transform.rotation);
    const glm::vec2 local{offset.x * c - offset.y * s, offset.x * s + offset.y * c};
    const glm::vec2 half = 0.5f * sprite.size * glm::abs(transform.scale);
    if (std::fabs(local.x) > half.x || std::fabs(local.y) > half.y) return;
    // Pool order is draw order, so ">=" makes a later sprite on the same
    // layer win, as it does on screen.
    if (!best || sprite.layer >= bestLayer) {
      best = entity;
      bestLayer = sprite.layer;
    }
  });
  return best;
}

glm::mat4 interpolatedMatrix(const Transform3D& transform, f32 alpha) noexcept {
  glm::vec3 position = transform.position;
  glm::quat rotation = transform.rotation;
  if (transform.hasPrevious) {
    position = glm::mix(transform.previousPosition, transform.position, alpha);
    rotation = glm::slerp(transform.previousRotation, transform.rotation, alpha);  // takes the short way
  }
  return glm::scale(glm::translate(glm::mat4(1.0f), position) * glm::mat4_cast(rotation), transform.scale);
}

glm::vec2 interpolatePosition(const Transform& transform, f32 alpha) noexcept {
  if (!transform.hasPrevious) return transform.position;  // just spawned: no streak from the origin
  return glm::mix(transform.previousPosition, transform.position, alpha);
}

f32 interpolateRotation(const Transform& transform, f32 alpha) noexcept {
  if (!transform.hasPrevious) return transform.rotation;
  // The change since the last step, taken the short way round: remainder()
  // maps it into [-pi, pi]. A plain lerp would spin the long way whenever
  // the stored angle wraps from +pi to -pi.
  constexpr f32 kTwoPi = 2.0f * std::numbers::pi_v<f32>;
  const f32 change = std::remainder(transform.rotation - transform.previousRotation, kTwoPi);
  return transform.previousRotation + change * alpha;
}

void extractSprites(ecs::World& world, render::FrameData& frame, f64 alpha) {
  const auto blend = static_cast<f32>(std::clamp(alpha, 0.0, 1.0));
  frame.sprites.reserve(frame.sprites.size() + world.count<Sprite>());
  world.each<Transform, Sprite>([&frame, blend](ecs::Entity, const Transform& transform, const Sprite& sprite) {
    frame.sprites.push_back({
        .position = interpolatePosition(transform, blend),
        .size = sprite.size * transform.scale,
        .rotation = interpolateRotation(transform, blend),
        .colour = sprite.colour,
        .depth = sprite.layer,
    });
  });
}

}  // namespace playground::engine
