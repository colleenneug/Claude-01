// Tests for the core layer. No window, no GPU: these run anywhere.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/core/Assert.h"
#include "playground/core/FixedStep.h"
#include "playground/core/FrameClock.h"
#include "playground/core/Log.h"
#include "playground/core/Result.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace playground;
using namespace std::chrono_literals;

namespace {

Result<int> parsePositive(int value) {
  if (value <= 0) return fail(Errc::InvalidArgument, "expected a positive number, got {}", value);
  return value;
}

std::string readFile(const std::filesystem::path& path) {
  std::ifstream file(path);
  std::stringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

std::filesystem::path scratchPath(const std::string& name) {
  return std::filesystem::temp_directory_path() / "playground_core_tests" / name;
}

}  // namespace

// --- Result ------------------------------------------------------------------

TEST_CASE("Result holds a value on success") {
  const Result<int> result = parsePositive(7);
  REQUIRE(result.has_value());
  CHECK(*result == 7);
}

TEST_CASE("fail() keeps the code and formats the message") {
  const Result<int> result = parsePositive(-3);
  REQUIRE_FALSE(result.has_value());
  CHECK(result.error().code == Errc::InvalidArgument);
  CHECK(result.error().message == "expected a positive number, got -3");
}

TEST_CASE("an empty Status is success") {
  const Status status{};
  CHECK(status.has_value());
}

// --- FrameClock --------------------------------------------------------------

TEST_CASE("FrameClock: the first frame has a zero delta") {
  FrameClock clock;
  const FrameTime first = clock.tick(FrameClock::Clock::time_point{} + 10s);
  CHECK(first.frameIndex == 0);
  CHECK(first.deltaSeconds == 0.0);
  CHECK(first.rawDeltaSeconds == 0.0);
  CHECK(first.elapsedSeconds == 0.0);
}

TEST_CASE("FrameClock: delta is the time between ticks") {
  FrameClock clock;
  const auto start = FrameClock::Clock::time_point{};
  clock.tick(start);
  const FrameTime second = clock.tick(start + 16ms);
  const FrameTime third = clock.tick(start + 16ms + 9ms);

  CHECK(second.frameIndex == 1);
  CHECK(second.deltaSeconds == doctest::Approx(0.016));
  CHECK(third.frameIndex == 2);
  CHECK(third.deltaSeconds == doctest::Approx(0.009));
  CHECK(third.elapsedSeconds == doctest::Approx(0.025));
}

TEST_CASE("FrameClock: a long stall is clamped, but the raw delta is kept") {
  FrameClock clock(0.25);
  const auto start = FrameClock::Clock::time_point{};
  clock.tick(start);
  const FrameTime stalled = clock.tick(start + 3s);

  CHECK(stalled.deltaSeconds == doctest::Approx(0.25));
  CHECK(stalled.rawDeltaSeconds == doctest::Approx(3.0));
  CHECK(stalled.elapsedSeconds == doctest::Approx(0.25));
}

TEST_CASE("FrameClock: time going backwards is treated as zero") {
  FrameClock clock;
  const auto start = FrameClock::Clock::time_point{} + 10s;
  clock.tick(start);
  CHECK(clock.tick(start - 1s).deltaSeconds == 0.0);
}

TEST_CASE("FrameClock: reset starts over at frame 0") {
  FrameClock clock;
  const auto start = FrameClock::Clock::time_point{};
  clock.tick(start);
  clock.tick(start + 1s);
  clock.reset();
  const FrameTime after = clock.tick(start + 5s);
  CHECK(after.frameIndex == 0);
  CHECK(after.deltaSeconds == 0.0);
  CHECK(after.elapsedSeconds == 0.0);
}

TEST_CASE("FrameClock: a non-positive clamp falls back to the default") {
  CHECK(FrameClock(0.0).maxDeltaSeconds() == FrameClock::kDefaultMaxDeltaSeconds);
  CHECK(FrameClock(-1.0).maxDeltaSeconds() == FrameClock::kDefaultMaxDeltaSeconds);
}

TEST_CASE("FrameClock: the real clock gives a non-negative delta") {
  FrameClock clock;
  clock.tick();
  CHECK(clock.tick().rawDeltaSeconds >= 0.0);
}

// --- FixedStepAccumulator ----------------------------------------------------
// Step sizes here are powers of two (0.25 s) or exact halvings of 1/120, so
// the arithmetic is exact in binary floating point and the expected values
// can be exact too.

TEST_CASE("FixedStep: 60 fps frames at 120 Hz run exactly two steps each") {
  FixedStepAccumulator accumulator(1.0 / 120.0);
  for (int frame = 0; frame < 10; ++frame) {
    const FixedStepPlan plan = accumulator.advance(1.0 / 60.0);
    CHECK(plan.steps == 2);
    CHECK(plan.alpha == 0.0);
    CHECK(plan.droppedSeconds == 0.0);
  }
}

TEST_CASE("FixedStep: a 240 Hz display steps every other frame, alpha alternates 0.5 / 0") {
  FixedStepAccumulator accumulator(1.0 / 120.0);
  for (int pair = 0; pair < 5; ++pair) {
    const FixedStepPlan first = accumulator.advance(1.0 / 240.0);
    CHECK(first.steps == 0);
    CHECK(first.alpha == 0.5);  // half way to the next step
    const FixedStepPlan second = accumulator.advance(1.0 / 240.0);
    CHECK(second.steps == 1);
    CHECK(second.alpha == 0.0);
  }
}

TEST_CASE("FixedStep: leftover time carries over and becomes alpha") {
  // A frame clamp well above the 0.625 s frame below, so only carry-over is tested.
  FixedStepAccumulator accumulator(0.25, /*maxStepsPerFrame=*/8, /*maxFrameSeconds=*/10.0);
  const FixedStepPlan plan = accumulator.advance(0.625);  // 2 steps + half a step
  CHECK(plan.steps == 2);
  CHECK(plan.alpha == 0.5);
  CHECK(accumulator.accumulatedSeconds() == 0.125);

  const FixedStepPlan next = accumulator.advance(0.125);  // the other half
  CHECK(next.steps == 1);
  CHECK(next.alpha == 0.0);
}

TEST_CASE("FixedStep: a stalled frame is clamped before it is accumulated") {
  // Room for 100 steps per frame, so only the clamp limits this frame.
  FixedStepAccumulator accumulator(0.25 / 16.0, /*maxStepsPerFrame=*/100, /*maxFrameSeconds=*/0.25);
  const FixedStepPlan plan = accumulator.advance(10.0);  // a 10-second breakpoint
  CHECK(plan.steps == 16);                // 0.25 s worth, not 10 s worth
  CHECK(plan.droppedSeconds == 9.75);
  CHECK(plan.alpha == 0.0);
}

TEST_CASE("FixedStep: the catch-up cap drops whole steps but keeps the fraction") {
  FixedStepAccumulator accumulator(0.25, /*maxStepsPerFrame=*/2, /*maxFrameSeconds=*/10.0);
  const FixedStepPlan plan = accumulator.advance(1.625);  // owes 6.5 steps
  CHECK(plan.steps == 2);               // the cap
  CHECK(plan.droppedSeconds == 1.0);    // 4 whole steps thrown away
  CHECK(plan.alpha == 0.5);             // the half step is kept
  CHECK(accumulator.advance(0.0).steps == 0);  // and nothing more is owed afterwards
}

TEST_CASE("FixedStep: no time is lost or invented over many uneven frames") {
  FixedStepAccumulator accumulator(1.0 / 120.0, 8, 0.25);
  const double frames[] = {0.016, 0.007, 0.033, 0.0, 0.051, 0.002, 0.016, 0.1, 0.004, 0.019};
  double fed = 0.0;
  double simulated = 0.0;
  double dropped = 0.0;
  for (int lap = 0; lap < 20; ++lap) {
    for (double frame : frames) {
      const FixedStepPlan plan = accumulator.advance(frame);
      CHECK(plan.alpha >= 0.0);
      CHECK(plan.alpha < 1.0);
      fed += frame;
      simulated += plan.steps * accumulator.stepSeconds();
      dropped += plan.droppedSeconds;
    }
  }
  // Every second fed in was simulated, dropped, or is still waiting.
  CHECK(simulated + dropped + accumulator.accumulatedSeconds() == doctest::Approx(fed).epsilon(1e-12));
}

TEST_CASE("FixedStep: negative frames count as zero; reset empties; bad settings fall back") {
  FixedStepAccumulator accumulator(0.25);
  CHECK(accumulator.advance(-1.0).steps == 0);
  CHECK(accumulator.accumulatedSeconds() == 0.0);

  (void)accumulator.advance(0.125);
  accumulator.reset();
  CHECK(accumulator.accumulatedSeconds() == 0.0);

  const FixedStepAccumulator fallback(0.0, 0, -1.0);
  CHECK(fallback.stepSeconds() == FixedStepAccumulator::kDefaultStepSeconds);
  CHECK(fallback.maxStepsPerFrame() == FixedStepAccumulator::kDefaultMaxStepsPerFrame);
  CHECK(fallback.maxFrameSeconds() == FixedStepAccumulator::kDefaultMaxFrameSeconds);
}

// --- Log ---------------------------------------------------------------------

TEST_CASE("Log writes to its file, filters by level, and refuses a second init") {
  const auto path = scratchPath("filtering.log");
  std::filesystem::remove(path);

  {
    auto session = log::init({.filePath = path, .level = log::Level::Info, .console = false});
    REQUIRE(session.has_value());
    CHECK(log::isInitialized());

    log::info(log::Category::Platform, "visible line {}", 42);
    log::debug(log::Category::Platform, "hidden line");

    const auto second = log::init({.filePath = path, .console = false});
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().code == Errc::AlreadyInitialized);
  }  // session ends: flushed and closed

  CHECK_FALSE(log::isInitialized());
  const std::string contents = readFile(path);
  CHECK(contents.find("visible line 42") != std::string::npos);
  CHECK(contents.find("[platform]") != std::string::npos);
  CHECK(contents.find("hidden line") == std::string::npos);
}

TEST_CASE("Log lines reach the file immediately, while the session is still open") {
  const auto path = scratchPath("live.log");
  std::filesystem::remove(path);
  auto session = log::init({.filePath = path, .level = log::Level::Info, .console = false,
                            .flushLevel = log::Level::Info});
  REQUIRE(session.has_value());

  log::info(log::Category::Engine, "written at once");
  // Read while logging is still running: what `tail -f` would see.
  CHECK(readFile(path).find("written at once") != std::string::npos);
}

TEST_CASE("Log reports a log file it cannot create") {
  // A folder where the file should be: opening it for writing must fail.
  const auto path = scratchPath("is_a_folder.log");
  std::filesystem::create_directories(path);

  const auto session = log::init({.filePath = path, .console = false});
  REQUIRE_FALSE(session.has_value());
  CHECK(session.error().code == Errc::IoFailure);
  CHECK(session.error().message.find("is_a_folder.log") != std::string::npos);
  CHECK_FALSE(log::isInitialized());
}

TEST_CASE("a moved Session keeps logging alive until the new owner dies") {
  auto first = log::init({.filePath = {}, .console = false});
  REQUIRE(first.has_value());
  {
    log::Session moved = std::move(*first);
    CHECK(log::isInitialized());
  }
  CHECK_FALSE(log::isInitialized());
}

// --- Assert ------------------------------------------------------------------

TEST_CASE("PG_VERIFY and PG_ASSERT pass on a true condition") {
  int evaluations = 0;
  PG_VERIFY(++evaluations == 1, "VERIFY evaluates its condition exactly once");
  CHECK(evaluations == 1);
  PG_ASSERT(evaluations == 1, "ASSERT sees the same state");
}
