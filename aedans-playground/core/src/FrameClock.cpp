#include "playground/core/FrameClock.h"

#include <algorithm>

namespace playground {

FrameClock::FrameClock(f64 maxDeltaSeconds) noexcept
    : maxDeltaSeconds_(maxDeltaSeconds > 0.0 ? maxDeltaSeconds : kDefaultMaxDeltaSeconds) {}

void FrameClock::reset() noexcept {
  started_ = false;
  elapsedSeconds_ = 0.0;
  nextFrameIndex_ = 0;
}

FrameTime FrameClock::tick(Clock::time_point now) noexcept {
  f64 raw = 0.0;
  if (started_) {
    raw = std::chrono::duration<f64>(now - previous_).count();
    // steady_clock cannot go backwards, but a time passed in by a caller can.
    raw = std::max(raw, 0.0);
  }
  started_ = true;
  previous_ = now;

  const f64 delta = std::min(raw, maxDeltaSeconds_);
  elapsedSeconds_ += delta;

  return FrameTime{
      .deltaSeconds = delta,
      .rawDeltaSeconds = raw,
      .elapsedSeconds = elapsedSeconds_,
      .frameIndex = nextFrameIndex_++,
  };
}

}  // namespace playground
