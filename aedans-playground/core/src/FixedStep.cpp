#include "playground/core/FixedStep.h"

#include <algorithm>
#include <cmath>

namespace playground {

FixedStepAccumulator::FixedStepAccumulator(f64 stepSeconds, u32 maxStepsPerFrame, f64 maxFrameSeconds) noexcept
    : stepSeconds_(stepSeconds > 0.0 ? stepSeconds : kDefaultStepSeconds),
      maxStepsPerFrame_(maxStepsPerFrame > 0 ? maxStepsPerFrame : kDefaultMaxStepsPerFrame),
      maxFrameSeconds_(maxFrameSeconds > 0.0 ? maxFrameSeconds : kDefaultMaxFrameSeconds) {}

FixedStepPlan FixedStepAccumulator::advance(f64 frameSeconds) noexcept {
  FixedStepPlan plan;

  // Guard 1: clamp the frame before it reaches the accumulator.
  const f64 frame = std::max(frameSeconds, 0.0);
  const f64 clamped = std::min(frame, maxFrameSeconds_);
  plan.droppedSeconds = frame - clamped;
  accumulated_ += clamped;

  // Take whole steps out. Subtracting one step at a time (rather than
  // floor-dividing) keeps the arithmetic identical to running the steps.
  while (accumulated_ >= stepSeconds_ && plan.steps < maxStepsPerFrame_) {
    accumulated_ -= stepSeconds_;
    ++plan.steps;
  }

  // Guard 2: whole steps still owed after the cap are dropped. The fraction
  // of a step is kept, so alpha stays continuous.
  if (accumulated_ >= stepSeconds_) {
    const f64 leftover = std::fmod(accumulated_, stepSeconds_);
    plan.droppedSeconds += accumulated_ - leftover;
    accumulated_ = leftover;
  }

  plan.alpha = std::clamp(accumulated_ / stepSeconds_, 0.0, 1.0);
  return plan;
}

}  // namespace playground
