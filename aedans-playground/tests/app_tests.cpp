// Tests for the Application: loop, renderer slot, shutdown order.
//
// These open real windows, so they need a display. On Linux without one
// (a CI box, a cloud container) the test exits with code 77, which CTest
// reports as "skipped" rather than "failed". Run under `xvfb-run -a` to get a
// virtual display.

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>

#include "playground/core/Log.h"
#include "playground/engine/Application.h"

#include <algorithm>
#include <cstdio>
#include <memory>
#include <vector>

using namespace playground;

namespace {

constexpr int kSkipExitCode = 77;

// What a fake renderer saw, kept outside it so it outlives the renderer.
struct RendererLog {
  int frames = 0;
  int resizes = 0;
  bool destroyed = false;
  bool windowOpenWhenDestroyed = false;
};

// Stands in for the Vulkan renderer: counts calls, can fail on demand, and
// checks on destruction that the window it depends on is still there.
class FakeRenderer final : public render::Renderer {
 public:
  FakeRenderer(platform::Window& window, RendererLog& record, int failOnFrame = 0)
      : window_(window), record_(record), failOnFrame_(failOnFrame) {}

  ~FakeRenderer() override {
    record_.destroyed = true;
    record_.windowOpenWhenDestroyed = window_.isOpen();
  }

  std::string_view name() const noexcept override { return "fake"; }

  Status onFramebufferResized(platform::Extent2D) override {
    ++record_.resizes;
    return {};
  }

  Status renderFrame(const FrameTime&, const render::FrameData&) override {
    ++record_.frames;
    if (record_.frames == failOnFrame_) {
      return fail(Errc::RendererFailure, "simulated device loss on frame {}", record_.frames);
    }
    return {};
  }

 private:
  platform::Window& window_;
  RendererLog& record_;
  int failOnFrame_;
};

engine::ApplicationConfig smallWindow(u64 maxFrames) {
  return {
      .window = {.title = "playground app_tests", .width = 320, .height = 240},
      .maxFrames = maxFrames,
  };
}

std::unique_ptr<engine::Application> makeApp(u64 maxFrames) {
  auto app = engine::Application::create(smallWindow(maxFrames));
  REQUIRE_MESSAGE(app.has_value(), (app ? "" : app.error().message));
  return std::move(*app);
}

}  // namespace

TEST_CASE("the loop runs exactly maxFrames frames, rendering each one") {
  RendererLog record;
  auto app = makeApp(10);
  app->setRenderer(std::make_unique<FakeRenderer>(app->window(), record));

  const Status status = app->run();
  CHECK(status.has_value());
  CHECK(record.frames == 10);
  CHECK(app->framesRun() == 10);
  CHECK(app->stopReason() == engine::StopReason::FrameLimit);
}

TEST_CASE("update sees every frame in order, with a zero first delta") {
  auto app = makeApp(5);
  std::vector<FrameTime> seen;
  app->setUpdate([&seen](const FrameTime& time) { seen.push_back(time); });

  REQUIRE(app->run().has_value());
  REQUIRE(seen.size() == 5);
  CHECK(seen.front().deltaSeconds == 0.0);
  for (usize i = 0; i < seen.size(); ++i) {
    CHECK(seen[i].frameIndex == i);
    CHECK(seen[i].deltaSeconds >= 0.0);
    CHECK(seen[i].deltaSeconds <= FrameClock::kDefaultMaxDeltaSeconds);
  }
  CHECK(seen.back().elapsedSeconds > 0.0);  // no renderer: paced at 60 fps, so time passes
}

TEST_CASE("requestQuit from update stops the loop after that frame") {
  RendererLog record;
  auto app = makeApp(0);  // no frame limit: only the quit request can stop it
  engine::Application* raw = app.get();
  app->setRenderer(std::make_unique<FakeRenderer>(app->window(), record));
  app->setUpdate([raw](const FrameTime& time) {
    if (time.frameIndex == 4) raw->requestQuit();
  });

  REQUIRE(app->run().has_value());
  CHECK(record.frames == 5);
  CHECK(app->stopReason() == engine::StopReason::QuitRequested);
}

TEST_CASE("a renderer error stops the loop and run() returns it") {
  RendererLog record;
  auto app = makeApp(100);
  app->setRenderer(std::make_unique<FakeRenderer>(app->window(), record, /*failOnFrame=*/3));

  const Status status = app->run();
  REQUIRE_FALSE(status.has_value());
  CHECK(status.error().code == Errc::RendererFailure);
  CHECK(status.error().message == "simulated device loss on frame 3");
  CHECK(record.frames == 3);
  CHECK(app->stopReason() == engine::StopReason::RendererError);
}

TEST_CASE("shutdown destroys the renderer while its window still exists, and is idempotent") {
  RendererLog record;
  auto app = makeApp(1);
  app->setRenderer(std::make_unique<FakeRenderer>(app->window(), record));
  REQUIRE(app->run().has_value());

  app->shutdown();
  CHECK(record.destroyed);
  CHECK(record.windowOpenWhenDestroyed);

  app->shutdown();  // second call: no-op, no crash
  const Status afterShutdown = app->run();
  REQUIRE_FALSE(afterShutdown.has_value());
  CHECK(afterShutdown.error().code == Errc::InvalidState);
}

TEST_CASE("destroying the Application without shutdown() still tears down in order") {
  RendererLog record;
  {
    auto app = makeApp(1);
    app->setRenderer(std::make_unique<FakeRenderer>(app->window(), record));
    REQUIRE(app->run().has_value());
  }
  CHECK(record.destroyed);
  CHECK(record.windowOpenWhenDestroyed);
}

TEST_CASE("only one Platform may exist at a time") {
  auto app = makeApp(1);
  const auto second = platform::Platform::create();
  REQUIRE_FALSE(second.has_value());
  CHECK(second.error().code == Errc::AlreadyInitialized);
}

TEST_CASE("an unusable window size is rejected with a reason") {
  auto config = smallWindow(1);
  config.window.width = 0;
  const auto app = engine::Application::create(config);
  REQUIRE_FALSE(app.has_value());
  CHECK(app.error().code == Errc::InvalidArgument);
  CHECK(app.error().message == "window size 0x240 is outside 1..16384");
}

TEST_CASE("a non-positive delta clamp is rejected") {
  auto config = smallWindow(1);
  config.maxDeltaSeconds = 0.0;
  const auto app = engine::Application::create(config);
  REQUIRE_FALSE(app.has_value());
  CHECK(app.error().code == Errc::InvalidArgument);
}

// --- Fixed timestep ------------------------------------------------------------

TEST_CASE("fixed update runs in exact 1/120 s steps, numbered without gaps") {
  // No renderer: the loop paces itself at 60 fps, so about 2 steps a frame.
  // The catch-up cap is raised so a slow test machine cannot drop steps and
  // make the bookkeeping below inexact.
  auto config = smallWindow(30);
  config.maxFixedStepsPerFrame = 1000;
  auto app = engine::Application::create(config);
  REQUIRE(app.has_value());

  std::vector<FixedStep> steps;
  f64 realSeconds = 0.0;
  (*app)->setFixedUpdate([&](const FixedStep& step) { steps.push_back(step); });
  (*app)->setUpdate([&](const FrameTime& time) { realSeconds += std::min(time.rawDeltaSeconds, config.maxDeltaSeconds); });

  REQUIRE((*app)->run().has_value());
  REQUIRE_FALSE(steps.empty());
  CHECK(steps.size() == (*app)->fixedStepsRun());
  for (usize i = 0; i < steps.size(); ++i) {
    CHECK(steps[i].index == i);
    CHECK(steps[i].deltaSeconds == FixedStepAccumulator::kDefaultStepSeconds);
  }
  // Real time in = simulated time out + the fraction still waiting.
  const f64 simulated = static_cast<f64>(steps.size()) * FixedStepAccumulator::kDefaultStepSeconds;
  const f64 waiting = (*app)->lastAlpha() * FixedStepAccumulator::kDefaultStepSeconds;
  CHECK(simulated + waiting == doctest::Approx(realSeconds).epsilon(1e-9));
}

TEST_CASE("each frame: fixed steps, then update, then extract with alpha in [0, 1)") {
  RendererLog record;
  auto app = makeApp(40);
  app->setRenderer(std::make_unique<FakeRenderer>(app->window(), record));

  std::vector<char> order;  // 'f' fixed step, 'u' update, 'e' extract
  std::vector<f64> alphas;
  app->setFixedUpdate([&](const FixedStep&) { order.push_back('f'); });
  app->setUpdate([&](const FrameTime&) { order.push_back('u'); });
  app->setExtract([&](render::FrameData&, f64 alpha) {
    order.push_back('e');
    alphas.push_back(alpha);
  });

  REQUIRE(app->run().has_value());
  REQUIRE(alphas.size() == 40);
  for (const f64 alpha : alphas) {
    CHECK(alpha >= 0.0);
    CHECK(alpha < 1.0);
  }
  CHECK(alphas.back() == app->lastAlpha());
  // Within every frame, all fixed steps come before update, update before extract.
  std::size_t i = 0;
  for (int frame = 0; frame < 40; ++frame) {
    while (i < order.size() && order[i] == 'f') ++i;
    REQUIRE(i + 1 < order.size());
    CHECK(order[i] == 'u');
    CHECK(order[i + 1] == 'e');
    i += 2;
  }
  CHECK(i == order.size());
}

TEST_CASE("unusable fixed-step settings are rejected with a reason") {
  auto zeroStep = smallWindow(1);
  zeroStep.fixedStepSeconds = 0.0;
  const auto a = engine::Application::create(zeroStep);
  REQUIRE_FALSE(a.has_value());
  CHECK(a.error().code == Errc::InvalidArgument);

  auto noSteps = smallWindow(1);
  noSteps.maxFixedStepsPerFrame = 0;
  const auto b = engine::Application::create(noSteps);
  REQUIRE_FALSE(b.has_value());
  CHECK(b.error().code == Errc::InvalidArgument);
}

int main(int argc, char** argv) {
  // Warnings and errors only, to the console: the tests check outcomes, and
  // the log is there to explain a failure.
  auto session = log::init({.filePath = {}, .level = log::Level::Warn});
  if (!session) {
    std::fprintf(stderr, "could not start logging: %s\n", session.error().message.c_str());
    return 1;
  }

  {
    auto probe = platform::Platform::create();
    if (!probe) {
      std::printf("skipping app_tests: %s\n", probe.error().message.c_str());
      return kSkipExitCode;
    }
  }

  doctest::Context context(argc, argv);
  return context.run();
}
