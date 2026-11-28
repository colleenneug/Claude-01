#pragma once
// =============================================================================
//  playground/game/Interpolation.h — drawing other things smoothly.
//
//  Snapshots arrive 20 times a second, a little irregularly, and some never
//  arrive. Drawing each entity where the newest snapshot put it would make
//  everything jump 20 times a second. Instead the client draws the world a
//  little IN THE PAST (kInterpolationDelayTicks behind the newest server
//  tick it knows), where it has a snapshot on each side, and blends between
//  the two: smooth motion, at the price of seeing others ~0.1 s late. (The
//  local player is not drawn this way: it is predicted, Prediction.h.)
//
//  Past the newest snapshot (a long loss), entities hold their last known
//  place rather than guess (no extrapolation).
// =============================================================================

#include "playground/core/Types.h"
#include "playground/game/Protocol.h"

#include <deque>
#include <span>
#include <vector>

#include <glm/vec2.hpp>

namespace playground::game {

// Two snapshots and a bit: 117 ms, so one lost snapshot still leaves a pair
// to blend between.
inline constexpr f64 kInterpolationDelayTicks = 2.0 * protocol::kSnapshotEveryTicks + 2.0;

struct EntitySample {
  u32 id = 0;
  glm::vec2 position{0.0f};
  f32 rotation = 0.0f;
};

class SnapshotBuffer {
 public:
  static constexpr usize kCapacity = 32;  // 1.6 s of snapshots

  // A snapshot newer than every one held (the caller drops older ones).
  void push(u32 tick, std::span<const protocol::SnapshotEntity> entities);

  // Every entity at server time `tick` (fractional), blended between the
  // snapshots either side of it: each entity in the newer one, with its
  // place in the older one if it was there too. Angles blend the short way.
  [[nodiscard]] std::vector<EntitySample> sample(f64 tick) const;

  [[nodiscard]] bool empty() const noexcept { return snapshots_.empty(); }
  [[nodiscard]] u32 newestTick() const noexcept { return snapshots_.empty() ? 0 : snapshots_.back().tick; }
  [[nodiscard]] usize size() const noexcept { return snapshots_.size(); }
  void clear() noexcept { snapshots_.clear(); }

 private:
  struct Stored {
    u32 tick = 0;
    std::vector<protocol::SnapshotEntity> entities;  // sorted by id
  };
  std::deque<Stored> snapshots_;
};

// Where (in server ticks) the client should be drawing others: follows the
// newest server tick heard of, kInterpolationDelayTicks behind, advancing
// one tick per client tick and nudged gently toward the target, so jitter
// in arrival times does not jerk it around.
class InterpolationClock {
 public:
  // A snapshot for `serverTick` just arrived.
  void heard(u32 serverTick) noexcept;
  // One client tick passed.
  void advance() noexcept;
  [[nodiscard]] f64 renderTick() const noexcept { return renderTick_; }
  // The server's tick now, estimated from the snapshots heard (jitter-smoothed).
  [[nodiscard]] f64 serverTick() const noexcept { return newestHeard_; }
  [[nodiscard]] bool started() const noexcept { return started_; }

 private:
  f64 renderTick_ = 0.0;
  f64 newestHeard_ = 0.0;  // the newest server tick, estimated forward to now
  bool started_ = false;
};

}  // namespace playground::game
