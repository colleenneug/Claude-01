#include "erebus/core/GameLoop.h"

#include <algorithm>

namespace erebus {

FrameStats GameLoop::tick() {
  const Clock::time_point now = Clock::now();

  // The first tick has no previous timestamp. Treating it as "zero elapsed"
  // rather than "elapsed since epoch" is what stops the first frame after a
  // level load from running a thousand catch-up steps.
  if (!started_) {
    previous_ = now;
    started_  = true;
  }

  const f64 real = std::chrono::duration<f64>(now - previous_).count();
  previous_ = now;

  stats_ = FrameStats{};
  stats_.realDeltaSeconds = real;

  // Clamp a single frame's contribution before it ever reaches the
  // accumulator. A breakpoint in a debugger produces an elapsed time of
  // minutes; without this the next tick tries to simulate all of it.
  const f32 clampedReal = std::min(static_cast<f32>(real), 0.25f);
  accumulator_ += clampedReal * timeScale_;

  u32 steps = 0;
  while (accumulator_ >= kFixedDelta) {
    if (steps >= maxSteps_) {
      // Catch-up cap reached. Drop the surplus: running slow is recoverable,
      // teleporting after a hitch is not. Reported so a frame-pacing HUD or a
      // telemetry sink can see it happening rather than guessing.
      accumulator_ = 0.0f;
      stats_.clamped = true;
      break;
    }
    if (simulate_) simulate_(kFixedDelta);
    accumulator_ -= kFixedDelta;
    ++steps;
  }

  stats_.simulationSteps = steps;
  // How far past the last simulated state the display is. Render systems
  // interpolate previous->current transforms by this; see
  // ecs/Components.h::Transform, which stores both for exactly this reason.
  stats_.interpolation = accumulator_ / kFixedDelta;

  if (render_) render_(stats_.interpolation, stats_);
  return stats_;
}

void GameLoop::run(const std::function<bool()>& shouldContinue) {
  while (running_) {
    if (shouldContinue && !shouldContinue()) break;
    tick();
  }
}

}  // namespace erebus
