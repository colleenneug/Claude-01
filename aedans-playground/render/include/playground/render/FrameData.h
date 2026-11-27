#pragma once
// =============================================================================
//  playground/render/FrameData.h — what to draw this frame, as plain data.
//
//  The renderer never reads the ECS. Each frame the game copies what is
//  visible into a FrameData ("extraction", engine::extractSprites), and the
//  renderer draws that. The two sides share no pointers, which keeps them
//  decoupled and is what later lets rendering run on its own thread.
//
//  COORDINATES: world units, +x right, +y up, +z towards the viewer.
//  `viewProjection` (usually engine::Camera::viewProjection) maps them to
//  the screen; left empty, the renderer uses one unit per pixel with (0, 0)
//  at the window's centre. Sprites' `depth` is their z: with the depth test,
//  a larger z is in front whatever the draw order; equal z falls back to
//  draw order (later on top).
// =============================================================================

#include "playground/core/Types.h"

#include <algorithm>
#include <optional>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/mat4x4.hpp>

#include <glm/common.hpp>
#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

namespace playground::render {

struct SpriteInstance {
  glm::vec2 position{0.0f};          // centre, world units
  glm::vec2 size{32.0f};             // full width and height, world units
  f32 rotation = 0.0f;               // radians, counter-clockwise
  glm::vec4 colour{1.0f};            // linear RGBA; alpha blends
  f32 depth = 0.0f;                  // world z: larger is nearer the viewer
};

struct FrameData {
  std::vector<SpriteInstance> sprites;      // equal depth: drawn in order, later on top
  std::optional<glm::mat4> viewProjection;  // world -> clip (reverse-Z); empty = pixels
  glm::vec2 viewportSize{0.0f};             // framebuffer pixels, filled in before extraction

  // Keeps the sprites' capacity (no allocation per frame); resets the view.
  void clear() noexcept {
    sprites.clear();
    viewProjection.reset();
  }
};

// Converts a projection with depth 0 (near) .. 1 (far) into reverse-Z,
// 1 (near) .. 0 (far): z_clip' = w_clip - z_clip. See engine/Camera.h.
[[nodiscard]] inline glm::mat4 reverseZ() noexcept {
  glm::mat4 m(1.0f);
  m[2][2] = -1.0f;  // column 2, row 2
  m[3][2] = 1.0f;   // column 3, row 2
  return m;
}

// One world unit per pixel, (0, 0) at the centre, reverse-Z over z -1000..1000:
// the view used when a frame sets no viewProjection.
[[nodiscard]] inline glm::mat4 pixelViewProjection(glm::vec2 viewportSize) noexcept {
  const glm::vec2 half = 0.5f * glm::max(viewportSize, glm::vec2{1.0f});
  return reverseZ() * glm::orthoRH_ZO(-half.x, half.x, -half.y, half.y, -1000.0f, 1000.0f);
}

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
