#pragma once
// =============================================================================
//  playground/render/FrameData.h — what to draw this frame, as plain data.
//
//  The renderer never reads the ECS. Each frame the game copies what is
//  visible into a FrameData ("extraction", engine::extractSprites), and the
//  renderer draws that. The two sides share no pointers, which keeps them
//  decoupled and is what later lets rendering run on its own thread.
//
//  COORDINATES: world units are framebuffer pixels, (0, 0) is the centre of
//  the window, +x right, +y up.
// =============================================================================

#include "playground/core/Types.h"

#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace playground::render {

struct SpriteInstance {
  glm::vec2 position{0.0f};          // centre, in pixels
  glm::vec2 size{32.0f};             // full width and height, in pixels
  f32 rotation = 0.0f;               // radians, counter-clockwise
  glm::vec4 colour{1.0f};            // linear RGBA; alpha blends
};

struct FrameData {
  std::vector<SpriteInstance> sprites;  // drawn in order: later ones on top

  void clear() noexcept { sprites.clear(); }  // keeps the capacity: no allocation per frame
};

}  // namespace playground::render
