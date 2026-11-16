#pragma once
// =============================================================================
//  playground/core/FrameClock.h — measures the time between frames.
//
//  WHICH CLOCK
//    std::chrono::steady_clock, never system_clock: the system clock follows
//    the wall clock, which jumps when NTP corrects it or the user changes the
//    time zone, and a jump of an hour is an hour-long frame.
//
//    Not high_resolution_clock either, despite the name. The standard lets it
//    be an alias of either of the other two, and in libstdc++ (GCC on Linux)
//    it is system_clock. steady_clock is just as precise on every platform we
//    target (nanosecond ticks on Linux, QueryPerformanceCounter on Windows).
//
//  CLAMPING
//    A frame can be arbitrarily long: a breakpoint, a window being dragged
//    (Windows blocks the loop during a drag), a level load. Fed straight into
//    gameplay, a 30-second delta moves every object 30 seconds at once.
//    deltaSeconds is clamped to maxDeltaSeconds; rawDeltaSeconds keeps the
//    true value for profiling.
//
//  TESTABILITY
//    tick(now) takes the time as a parameter, so tests can drive the clock
//    with exact values instead of sleeping. tick() reads the real clock.
// =============================================================================

#include "playground/core/Types.h"

#include <chrono>

namespace playground {

struct FrameTime {
  f64 deltaSeconds = 0.0;     // since the previous frame, clamped: use this for gameplay
  f64 rawDeltaSeconds = 0.0;  // the same, unclamped: use this for profiling
  f64 elapsedSeconds = 0.0;   // sum of deltaSeconds since the clock started
  u64 frameIndex = 0;         // 0 on the first frame after start or reset
};

class FrameClock {
 public:
  using Clock = std::chrono::steady_clock;
  static_assert(Clock::is_steady, "the frame clock must never go backwards");

  static constexpr f64 kDefaultMaxDeltaSeconds = 0.25;

  explicit FrameClock(f64 maxDeltaSeconds = kDefaultMaxDeltaSeconds) noexcept;

  // Starts over: the next tick is frame 0 with a delta of zero.
  void reset() noexcept;

  // Marks the start of a new frame and reports the time since the last one.
  // The first tick after construction or reset() has a delta of zero: there
  // is no previous frame, and "time since the epoch" is not a frame time.
  FrameTime tick() noexcept { return tick(Clock::now()); }
  FrameTime tick(Clock::time_point now) noexcept;

  [[nodiscard]] f64 maxDeltaSeconds() const noexcept { return maxDeltaSeconds_; }

 private:
  Clock::time_point previous_{};
  f64 maxDeltaSeconds_;
  f64 elapsedSeconds_ = 0.0;
  u64 nextFrameIndex_ = 0;
  bool started_ = false;
};

}  // namespace playground
