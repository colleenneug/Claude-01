#include "playground/game/Simulation.h"

#include <algorithm>

#include <glm/geometric.hpp>

namespace playground::game {

PlayerState stepPlayer(const PlayerState& state, const PlayerInput& input) noexcept {
  glm::vec2 move = input.move;
  const f32 length = glm::length(move);
  if (length > 1.0f) move /= length;
  const glm::vec2 limit = kWorldBounds.halfSize - glm::vec2(0.5f * kPlayerSize);
  return {.position = glm::clamp(state.position + kPlayerSpeed * kStepSeconds * move, -limit, limit)};
}

glm::vec2 spawnPosition(u32 slot) noexcept {
  // 0, +120, -120, +240, -240, ... wrapping before the walls.
  const u32 step = (slot + 1) / 2 % 6;
  const f32 side = slot % 2 == 1 ? 1.0f : -1.0f;
  return {step == 0 ? 0.0f : side * 120.0f * static_cast<f32>(step), 0.0f};
}

}  // namespace playground::game
