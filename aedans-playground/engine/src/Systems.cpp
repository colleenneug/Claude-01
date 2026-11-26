#include "playground/engine/Systems.h"

#include "playground/engine/Components.h"

#include <cmath>

#include <glm/common.hpp>  // glm::abs

namespace playground::engine {

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

void extractSprites(ecs::World& world, render::FrameData& frame) {
  frame.sprites.reserve(frame.sprites.size() + world.count<Sprite>());
  world.each<Transform, Sprite>([&frame](ecs::Entity, const Transform& transform, const Sprite& sprite) {
    frame.sprites.push_back({
        .position = transform.position,
        .size = sprite.size * transform.scale,
        .rotation = transform.rotation,
        .colour = sprite.colour,
    });
  });
}

}  // namespace playground::engine
