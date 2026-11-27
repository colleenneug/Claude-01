#include "playground/game/Interpolation.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace playground::game {
namespace {

f32 shortWayAngle(f32 from, f32 to, f32 t) {
  constexpr f32 kTwoPi = 2.0f * std::numbers::pi_v<f32>;
  f32 delta = std::fmod(to - from, kTwoPi);
  if (delta > std::numbers::pi_v<f32>) delta -= kTwoPi;
  if (delta < -std::numbers::pi_v<f32>) delta += kTwoPi;
  return from + delta * t;
}

bool byId(const protocol::SnapshotEntity& a, const protocol::SnapshotEntity& b) { return a.id < b.id; }

}  // namespace

void SnapshotBuffer::push(u32 tick, std::span<const protocol::SnapshotEntity> entities) {
  if (!snapshots_.empty() && tick <= snapshots_.back().tick) return;  // the caller should have dropped it
  Stored stored{.tick = tick, .entities = {entities.begin(), entities.end()}};
  std::sort(stored.entities.begin(), stored.entities.end(), byId);
  snapshots_.push_back(std::move(stored));
  while (snapshots_.size() > kCapacity) snapshots_.pop_front();
}

std::vector<EntitySample> SnapshotBuffer::sample(f64 tick) const {
  std::vector<EntitySample> out;
  if (snapshots_.empty()) return out;

  // The newest snapshot at or before `tick` (or the oldest, before them all)
  // and the one after it (none past the newest: hold).
  usize older = 0;
  while (older + 1 < snapshots_.size() && static_cast<f64>(snapshots_[older + 1].tick) <= tick) ++older;
  const Stored& a = snapshots_[older];
  const bool hold = older + 1 >= snapshots_.size() || tick <= static_cast<f64>(a.tick);
  const Stored& b = hold ? a : snapshots_[older + 1];
  const f32 t = hold ? 0.0f
                     : static_cast<f32>(std::clamp((tick - static_cast<f64>(a.tick)) /
                                                       static_cast<f64>(b.tick - a.tick), 0.0, 1.0));

  out.reserve(b.entities.size());
  for (const protocol::SnapshotEntity& next : b.entities) {
    const auto it = std::lower_bound(a.entities.begin(), a.entities.end(), next, byId);
    if (hold || it == a.entities.end() || it->id != next.id) {
      // Only in the newer one (it just appeared): there.
      out.push_back({.id = next.id, .position = next.position, .rotation = next.rotation});
      continue;
    }
    out.push_back({.id = next.id, .position = it->position + (next.position - it->position) * t,
                   .rotation = shortWayAngle(it->rotation, next.rotation, t)});
  }
  return out;
}

void InterpolationClock::heard(u32 serverTick) noexcept {
  const auto tick = static_cast<f64>(serverTick);
  if (!started_) {
    started_ = true;
    newestHeard_ = tick;
    renderTick_ = tick - kInterpolationDelayTicks;
    return;
  }
  // Newer than the estimate: take it. Older (it came the slow way, or the
  // estimate ran ahead): pull the estimate back a little, not all the way,
  // so one late packet does not drag the view back.
  newestHeard_ = tick > newestHeard_ ? tick : newestHeard_ + 0.1 * (tick - newestHeard_);
}

void InterpolationClock::advance() noexcept {
  if (!started_) return;
  newestHeard_ += 1.0;  // the server ticked too, probably
  renderTick_ += 1.0;
  const f64 error = (newestHeard_ - kInterpolationDelayTicks) - renderTick_;
  if (std::abs(error) > 4.0 * kInterpolationDelayTicks) {
    renderTick_ += error;  // far off (a stall, a long loss): jump
  } else {
    renderTick_ += std::clamp(0.05 * error, -0.25, 0.25);  // close: drift there, at most a quarter tick a tick
  }
}

}  // namespace playground::game
