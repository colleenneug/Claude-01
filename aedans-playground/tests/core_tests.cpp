// Tests for the core layer. No window, no GPU: these run anywhere.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/core/Assert.h"
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
