#pragma once
// =============================================================================
//  playground/engine/Components.h — the basic components.
//
//  Components are plain data: no behaviour, no pointers to other objects.
//  Behaviour lives in systems (Systems.h), which run over every entity that
//  has the components they need.
//
//  Units: pixels, seconds, radians. +y is up; (0, 0) is the window's centre.
// =============================================================================

#include "playground/core/Types.h"

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace playground::engine {

// Where an entity is, which way it faces, and how big it is.
struct Transform {
  glm::vec2 position{0.0f};
  f32 rotation = 0.0f;       // radians, counter-clockwise
  glm::vec2 scale{1.0f};
};

// How fast it moves and spins. Read by movementSystem.
struct Velocity {
  glm::vec2 linear{0.0f};    // pixels per second
  f32 angular = 0.0f;        // radians per second
};

// How it looks: a coloured rectangle, size multiplied by Transform::scale.
struct Sprite {
  glm::vec2 size{32.0f};     // pixels
  glm::vec4 colour{1.0f};    // linear RGBA
};

}  // namespace playground::engine
