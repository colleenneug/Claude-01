#pragma once
// =============================================================================
//  playground/game/Simulation.h — the game rules both sides run.
//
//  The server is the authority: what it computes is what happened. The
//  client runs THE SAME function on the same (quantized) inputs to guess the
//  result before the server's answer arrives (client-side prediction,
//  Prediction.h). The guess only matches if the rules are identical and
//  deterministic, so they live here, in one place, and use nothing but the
//  input and the previous state: no clocks, no randomness, a fixed step.
//
//  A player's state advances once per INPUT, not once per server tick: the
//  server applies input number N to the state after input N - 1, whenever
//  N arrives. So "the state after input N" is the same number on both sides.
// =============================================================================

#include "playground/core/Types.h"
#include "playground/engine/WorldBounds.h"
#include "playground/game/Protocol.h"

#include <glm/vec2.hpp>

namespace playground::game {

inline constexpr f32 kStepSeconds = 1.0f / static_cast<f32>(protocol::kTickRate);
inline constexpr f32 kPlayerSpeed = 360.0f;  // world units per second at full stick
inline constexpr f32 kPlayerSize = 40.0f;    // a square, world units
inline constexpr f32 kBallSize = 16.0f;
// The server's world: the same 1600 x 900 as the 2D demo.
inline constexpr engine::WorldBounds kWorldBounds{.halfSize = {800.0f, 450.0f}};

struct PlayerState {
  glm::vec2 position{0.0f};
  friend bool operator==(const PlayerState&, const PlayerState&) = default;
};

// One input's worth of movement: at most kPlayerSpeed (a diagonal is no
// faster than straight), kept inside the world.
[[nodiscard]] PlayerState stepPlayer(const PlayerState& state, const PlayerInput& input) noexcept;

// Where the `slot`th player to join starts: spread along the middle.
[[nodiscard]] glm::vec2 spawnPosition(u32 slot) noexcept;

}  // namespace playground::game
