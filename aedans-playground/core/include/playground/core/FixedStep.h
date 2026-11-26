#pragma once
// =============================================================================
//  playground/core/FixedStep.h — turning frame time into fixed simulation steps.
//
//  WHY A FIXED STEP
//    Movement integrated with the frame's own delta time gives different
//    results on different machines: a sprite bouncing at 144 fps and at
//    30 fps hits the wall at different moments, because every integration
//    step rounds a little differently. Running the simulation in steps of
//    exactly 1/120 s makes it behave the same everywhere, whatever the frame
//    rate. That matters for physics, and for co-op, where two machines must
//    agree.
//
//  THE ACCUMULATOR
//    Each frame, its (clamped) real duration is added to an accumulator.
//    While the accumulator holds at least one step, a step is run and taken
//    out. What is left, less than one step, is how far real time has moved
//    past the last simulated state:
//
//        alpha = accumulator / step        in [0, 1)
//
//    The renderer draws previous + (current - previous) * alpha, so motion is
//    smooth even when the display rate and the simulation rate differ (a
//    120 Hz simulation on a 144 Hz monitor, or on a 60 Hz one).
//
//  THE SPIRAL OF DEATH, AND THE TWO GUARDS AGAINST IT
//    If one step costs more real time than it simulates, every frame owes
//    more steps than the last, and the game locks up trying to catch up.
//      1. Each frame's delta is clamped (default 0.25 s) before it is added,
//         so a breakpoint, a window drag or a level-load stall cannot dump
//         minutes into the accumulator.
//      2. At most maxStepsPerFrame steps run per frame (default 8, about
//         67 ms of simulation at 120 Hz). Whole steps beyond that are
//         dropped: the game runs slower for a moment instead of freezing,
//         and does not teleport everything forward afterwards.
//
//  Pure arithmetic: no clock in here. The caller passes the frame's
//  duration, which is what makes the behaviour testable with exact numbers.
// =============================================================================

#include "playground/core/Types.h"

namespace playground {

// What one simulation step receives.
struct FixedStep {
  f64 deltaSeconds = 0.0;  // always the configured step, e.g. 1/120
  u64 index = 0;           // 0, 1, 2, ... since the loop started
};

// What advance() decided for one frame.
struct FixedStepPlan {
  u32 steps = 0;              // how many fixed steps to run now
  f64 alpha = 0.0;            // leftover fraction of a step, in [0, 1): the render interpolation factor
  f64 droppedSeconds = 0.0;   // time thrown away this frame by the two guards (0 normally)
};

class FixedStepAccumulator {
 public:
  static constexpr f64 kDefaultStepSeconds = 1.0 / 120.0;
  static constexpr u32 kDefaultMaxStepsPerFrame = 8;
  static constexpr f64 kDefaultMaxFrameSeconds = 0.25;

  // Non-positive values fall back to the defaults.
  explicit FixedStepAccumulator(f64 stepSeconds = kDefaultStepSeconds,
                                u32 maxStepsPerFrame = kDefaultMaxStepsPerFrame,
                                f64 maxFrameSeconds = kDefaultMaxFrameSeconds) noexcept;

  // Adds one frame's duration and says how many steps to run. Negative
  // durations count as zero.
  [[nodiscard]] FixedStepPlan advance(f64 frameSeconds) noexcept;

  // Empties the accumulator (start of a run, after a pause or a load).
  void reset() noexcept { accumulated_ = 0.0; }

  [[nodiscard]] f64 stepSeconds() const noexcept { return stepSeconds_; }
  [[nodiscard]] u32 maxStepsPerFrame() const noexcept { return maxStepsPerFrame_; }
  [[nodiscard]] f64 maxFrameSeconds() const noexcept { return maxFrameSeconds_; }
  [[nodiscard]] f64 accumulatedSeconds() const noexcept { return accumulated_; }

 private:
  f64 stepSeconds_;
  u32 maxStepsPerFrame_;
  f64 maxFrameSeconds_;
  f64 accumulated_ = 0.0;
};

}  // namespace playground
