#pragma once
// =============================================================================
//  playground/engine/Systems.h — behaviour that runs over components.
//
//  A system is a plain function: it asks the World for every entity with
//  the components it needs and updates them. No base class, no registration.
//  The game calls them in order once per frame:
//
//      engine::movementSystem(world, dt);
//      engine::bounceSystem(world, halfArea);
//      world.flush();                          // apply queued destroys
//      ...
//      engine::extractSprites(world, frame);   // copy out for the renderer
// =============================================================================

#include "playground/core/Types.h"
#include "playground/ecs/World.h"
#include "playground/render/FrameData.h"

#include <glm/vec2.hpp>

namespace playground::engine {

// Transform + Velocity: position += linear * dt, rotation += angular * dt.
void movementSystem(ecs::World& world, f32 deltaSeconds);

// Transform + Velocity + Sprite: keeps each sprite inside the rectangle
// from -halfArea to +halfArea, reflecting its velocity off the edges.
// (Uses the sprite's size as its extent until there is a collider component.)
void bounceSystem(ecs::World& world, glm::vec2 halfArea);

// Transform + Sprite -> render::SpriteInstance, appended to frame.sprites.
void extractSprites(ecs::World& world, render::FrameData& frame);

}  // namespace playground::engine
