#pragma once
// =============================================================================
//  playground/render/FrameData.h — what to draw this frame, as plain data.
//
//  The renderer never reads the ECS. Each frame the game copies what is
//  visible into a FrameData ("extraction", engine::extractSprites), and the
//  renderer draws that. The two sides share no pointers, which keeps them
//  decoupled and is what later lets rendering run on its own thread.
//
//  COORDINATES: world units, (0, 0) at the centre of the view, +x right,
//  +y up. `viewHalfExtent` says how much of the world must be visible; the
//  renderer scales it to fit the window, keeping its shape (fitView). Left
//  at zero, one world unit is one framebuffer pixel.
// =============================================================================

#include "playground/core/Types.h"

#include <algorithm>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace playground::render {

struct SpriteInstance {
  glm::vec2 position{0.0f};          // centre, world units
  glm::vec2 size{32.0f};             // full width and height, world units
  f32 rotation = 0.0f;               // radians, counter-clockwise
  glm::vec4 colour{1.0f};            // linear RGBA; alpha blends
};

struct FrameData {
  std::vector<SpriteInstance> sprites;  // drawn in order: later ones on top
  glm::vec2 viewHalfExtent{0.0f};       // world area that must be visible; zero = pixels

  // Keeps the sprites' capacity (no allocation per frame); resets the view.
  void clear() noexcept {
    sprites.clear();
    viewHalfExtent = glm::vec2{0.0f};
  }
};

// The half-size of the world area actually shown in a viewport of
// `viewportHalf` pixels so that all of `required` fits and nothing is
// stretched: the shorter fit wins, and the other axis shows extra world
// (bars at the sides of a wide window, above and below a tall one).
// `required` of zero means "one unit per pixel".
[[nodiscard]] inline glm::vec2 fitView(glm::vec2 viewportHalf, glm::vec2 required) noexcept {
  if (required.x <= 0.0f || required.y <= 0.0f || viewportHalf.x <= 0.0f || viewportHalf.y <= 0.0f) {
    return viewportHalf;
  }
  const float unitsPerPixel = std::max(required.x / viewportHalf.x, required.y / viewportHalf.y);
  return viewportHalf * unitsPerPixel;
}

}  // namespace playground::render
