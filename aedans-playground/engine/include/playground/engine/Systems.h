#pragma once
// =============================================================================
//  playground/engine/Systems.h — behaviour that runs over components.
//
//  A system is a plain function: it asks the World for every entity with
//  the components it needs and updates them. No base class, no registration.
//  The game calls them in order, in each fixed simulation step:
//
//      engine::snapshotTransforms(world);      // previous = current, first
//      engine::movementSystem(world, step.deltaSeconds);
//      engine::bounceSystem(world, halfArea);
//      world.flush();                          // apply queued destroys
//
//  and once per rendered frame:
//
//      engine::extractSprites(world, frame, alpha);   // blended copy for the renderer
// =============================================================================

#include "playground/core/Types.h"
#include "playground/ecs/World.h"
#include "playground/engine/Components.h"
#include "playground/render/FrameData.h"

#include <glm/vec2.hpp>

namespace playground::engine {

// Every Transform: previous = current, and mark it as having a previous
// state. Call first in each fixed step, before anything moves.
void snapshotTransforms(ecs::World& world);

// Transform + Velocity: position += linear * dt, rotation += angular * dt.
void movementSystem(ecs::World& world, f32 deltaSeconds);

// Transform + Velocity + Sprite: keeps each sprite inside the rectangle
// from -halfArea to +halfArea, reflecting its velocity off the edges.
// (Uses the sprite's size as its extent until there is a collider component.)
void bounceSystem(ecs::World& world, glm::vec2 halfArea);

// Transform + Sprite -> render::SpriteInstance, appended to frame.sprites,
// drawn `alpha` of the way from the previous state to the current one
// (alpha in [0, 1], from the Application's extract hook). A Transform with
// no previous state yet is drawn where it is.
void extractSprites(ecs::World& world, render::FrameData& frame, f64 alpha);

// The blends extractSprites uses, exposed for testing. Rotation goes the
// short way round, so 179 degrees -> -179 degrees turns 2 degrees, not 358.
[[nodiscard]] glm::vec2 interpolatePosition(const Transform& transform, f32 alpha) noexcept;
[[nodiscard]] f32 interpolateRotation(const Transform& transform, f32 alpha) noexcept;

}  // namespace playground::engine
