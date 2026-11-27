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
//
// PREVIOUS STATE, FOR SMOOTH DRAWING
//   The simulation moves in fixed 1/120 s steps, but the screen refreshes at
//   its own rate. Drawing the latest step as-is makes motion stutter, so
//   the renderer draws a blend: previous + (current - previous) * alpha.
//   snapshotTransforms() copies current into previous at the start of every
//   step.
//
// NO STREAK ON SPAWN
//   A new entity has no previous state yet. If previous defaulted to (0, 0),
//   its first frame would be drawn part-way between the origin and where it
//   really is: a visible streak. `hasPrevious` is false until the first
//   snapshot, and until then extraction draws the current state. So this is
//   safe however the Transform is made, `Transform{.position = p}` included.
//
// TELEPORTING
//   Setting `position` directly mid-game makes the next frame blend across
//   the jump. Use teleport() to move without that.
struct Transform {
  glm::vec2 position{0.0f};
  f32 rotation = 0.0f;       // radians, counter-clockwise; may be wrapped (blending takes the short way)
  glm::vec2 scale{1.0f};

  glm::vec2 previousPosition{0.0f};  // as of the start of the last simulation step
  f32 previousRotation = 0.0f;
  bool hasPrevious = false;          // false until the first snapshot (or after teleport)

  // Moves instantly: the next frame draws the new place with no blend from
  // the old one.
  void teleport(glm::vec2 newPosition, f32 newRotation) noexcept {
    position = previousPosition = newPosition;
    rotation = previousRotation = newRotation;
    hasPrevious = true;
  }
};

// How fast it moves and spins. Read by movementSystem.
struct Velocity {
  glm::vec2 linear{0.0f};    // pixels per second
  f32 angular = 0.0f;        // radians per second
};

// Marks the entity the player steers. inputSystem sets its velocity from
// the Move action.
struct PlayerTag {
  f32 speed = 360.0f;  // world units per second at full stick / key
};

// Destroyed when this runs out (lifetimeSystem). Bullets, effects.
struct Lifetime {
  f32 secondsLeft = 1.0f;
};

// How it looks: a coloured rectangle, size multiplied by Transform::scale.
struct Sprite {
  glm::vec2 size{32.0f};     // world units
  glm::vec4 colour{1.0f};    // linear RGBA
  f32 layer = 0.0f;          // world z: a higher layer is drawn in front, whatever the order
};

}  // namespace playground::engine
