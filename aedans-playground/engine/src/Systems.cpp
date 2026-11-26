#include "playground/engine/Systems.h"

#include "playground/engine/Components.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <glm/common.hpp>  // glm::abs

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

void bounceSystem(ecs::World& world, glm::vec2 halfArea) {
  world.each<Transform, Velocity, Sprite>(
      [halfArea](ecs::Entity, Transform& transform, Velocity& velocity, const Sprite& sprite) {
        const glm::vec2 halfExtent = 0.5f * sprite.size * glm::abs(transform.scale);
        for (int axis = 0; axis < 2; ++axis) {
          const f32 limit = std::fmax(halfArea[axis] - halfExtent[axis], 0.0f);
          // Only reflect a velocity that points further out, so a sprite
          // pushed past the edge (the window shrank) comes back in smoothly
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
    });
  });
}

}  // namespace playground::engine
