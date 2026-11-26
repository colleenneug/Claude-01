#pragma once
// =============================================================================
//  playground/engine/WorldBounds.h — the size of the game world.
//
//  The playing area belongs to the game, not to the window. Before this, the
//  bounce walls were the window's edges: resizing the window moved the walls
//  and changed the simulation, so two players with different window sizes
//  would see different games. Now the world is a fixed rectangle, the
//  simulation only ever reads this, and the window is just a view onto it:
//  the renderer scales the world to fit (render::fitView), with bars at the
//  sides or top if the shapes differ.
//
//  Units are world units, centred on (0, 0), +y up. At a window the same size
//  as the world, one world unit is one pixel.
// =============================================================================

#include <glm/vec2.hpp>

namespace playground::engine {

struct WorldBounds {
  glm::vec2 halfSize{800.0f, 450.0f};  // the world spans -halfSize .. +halfSize

  [[nodiscard]] glm::vec2 size() const noexcept { return 2.0f * halfSize; }
  [[nodiscard]] bool contains(glm::vec2 point) const noexcept {
    return point.x >= -halfSize.x && point.x <= halfSize.x && point.y >= -halfSize.y && point.y <= halfSize.y;
  }
};

}  // namespace playground::engine
