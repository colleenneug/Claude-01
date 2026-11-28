// Tests for cutscenes (engine/Cutscene.h): easing, the camera spline and
// rotation, events firing on exact fixed-step ticks, subtitles, skipping
// (holding, what fires, what does not), the letterbox, a clean camera
// handoff back to the player, and loading sequences from JSON.
//
// No window, no GPU.

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "playground/engine/Cutscene.h"

#include <cmath>
#include <numbers>
#include <string>
#include <vector>

#include <glm/geometric.hpp>

using namespace playground;
using namespace playground::engine;

namespace {

glm::quat yaw(f32 degrees) { return glm::angleAxis(glm::radians(degrees), glm::vec3(0.0f, 1.0f, 0.0f)); }

// Three keys along +x, two seconds apart: easy to reason about.
CutsceneSequence straightLine(Ease curve = Ease::Linear) {
  return {.id = "line", .duration = 4.0,
          .camera = {{.time = 0.0, .position = {0, 1, 0}, .rotation = yaw(0), .fovDegrees = 40, .ease = curve},
                     {.time = 2.0, .position = {10, 1, 0}, .rotation = yaw(90), .fovDegrees = 60, .ease = curve},
                     {.time = 4.0, .position = {20, 1, 0}, .rotation = yaw(90), .fovDegrees = 60, .ease = curve}}};
}

f32 angleBetween(glm::quat a, glm::quat b) { return glm::degrees(glm::angle(glm::inverse(a) * b)); }

}  // namespace

TEST_CASE("easing curves start at 0, end at 1, and have the promised shapes") {
  for (const Ease e : {Ease::Linear, Ease::In, Ease::Out, Ease::InOut}) {
    CHECK(ease(e, 0.0f) == 0.0f);
    CHECK(ease(e, 1.0f) == 1.0f);
    CHECK(ease(e, -1.0f) == 0.0f);  // clamped
    CHECK(ease(e, 2.0f) == 1.0f);
    f32 last = 0.0f;
    for (int i = 1; i <= 100; ++i) {
      const f32 v = ease(e, static_cast<f32>(i) / 100.0f);
      CHECK(v >= last);  // never goes back
      last = v;
    }
  }
  CHECK(ease(Ease::Linear, 0.25f) == 0.25f);
  CHECK(ease(Ease::In, 0.5f) < 0.5f);
  CHECK(ease(Ease::Out, 0.5f) > 0.5f);
  CHECK(ease(Ease::InOut, 0.5f) == doctest::Approx(0.5f));
  CHECK(ease(Ease::InOut, 0.1f) < 0.1f);
  CHECK(easeFromName("inOut") == Ease::InOut);
  CHECK_FALSE(easeFromName("bouncy").has_value());
}

TEST_CASE("camera spline: through every key exactly, straight where the keys are, no kink at a key") {
  CutsceneSequence line = straightLine();
  REQUIRE(line.validate().has_value());
  for (const CameraKey& key : line.camera) {
    CHECK(glm::length(line.cameraAt(key.time).position - key.position) < 1e-5f);
  }
  // Evenly spaced points on a line: Catmull-Rom stays on it, at an even pace.
  CHECK(glm::length(line.cameraAt(1.0).position - glm::vec3(5, 1, 0)) < 1e-4f);
  CHECK(glm::length(line.cameraAt(3.0).position - glm::vec3(15, 1, 0)) < 1e-4f);
  // Clamped before the first key and after the last.
  CHECK(line.cameraAt(-5.0).position == line.camera.front().position);
  CHECK(line.cameraAt(99.0).position == line.camera.back().position);

  // A bend: through the corner key, and the velocity just before it equals the velocity just after.
  CutsceneSequence bend{.id = "bend", .duration = 3.0,
                        .camera = {{.time = 0.0, .position = {0, 0, 0}, .ease = Ease::Linear},
                                   {.time = 1.0, .position = {4, 0, 0}, .ease = Ease::Linear},
                                   {.time = 2.0, .position = {4, 0, -4}, .ease = Ease::Linear},
                                   {.time = 3.0, .position = {8, 0, -4}, .ease = Ease::Linear}}};
  REQUIRE(bend.validate().has_value());
  constexpr f64 h = 1e-3;
  const glm::vec3 before = (bend.cameraAt(1.0).position - bend.cameraAt(1.0 - h).position) / static_cast<f32>(h);
  const glm::vec3 after = (bend.cameraAt(1.0 + h).position - bend.cameraAt(1.0).position) / static_cast<f32>(h);
  CHECK(glm::length(before - after) < 0.05f * glm::length(before));
}

TEST_CASE("camera rotation slerps the short way at an even rate; field of view follows the ease") {
  CutsceneSequence line = straightLine();
  REQUIRE(line.validate().has_value());
  CHECK(angleBetween(line.cameraAt(1.0).rotation, yaw(45)) < 0.01f);
  CHECK(angleBetween(line.cameraAt(0.5).rotation, yaw(22.5f)) < 0.01f);
  CHECK(line.cameraAt(1.0).fovDegrees == doctest::Approx(50.0f));
  // From 350 to 10 degrees is 20 degrees the short way, not 340 the long way.
  CutsceneSequence wrap{.id = "wrap", .duration = 1.0,
                        .camera = {{.time = 0.0, .rotation = yaw(350), .ease = Ease::Linear},
                                   {.time = 1.0, .rotation = yaw(10), .ease = Ease::Linear}}};
  REQUIRE(wrap.validate().has_value());
  CHECK(angleBetween(wrap.cameraAt(0.5).rotation, yaw(0)) < 0.01f);
  // An eased segment: slow at the ends.
  CutsceneSequence eased = straightLine(Ease::InOut);
  REQUIRE(eased.validate().has_value());
  CHECK(eased.cameraAt(0.2).position.x < 1.0f);  // a tenth of the time, much less than a tenth of the way
  CHECK(eased.cameraAt(1.0).position.x == doctest::Approx(5.0f).epsilon(0.01));  // halfway is halfway
  // lookRotation faces the target.
  const glm::quat look = lookRotation({0, 0, 0}, {0, 0, -5});
  CHECK(glm::length((look * glm::vec3(0, 0, -1)) - glm::vec3(0, 0, -1)) < 1e-5f);
  const glm::quat down = lookRotation({0, 10, 0}, {0, 0, 0});  // straight down: no NaN
  CHECK(glm::length((down * glm::vec3(0, 0, -1)) - glm::vec3(0, -1, 0)) < 1e-4f);
}

TEST_CASE("events fire on exact ticks: the first at or after their time, once each, in order") {
  CutsceneSequence s = straightLine();
  s.events = {{.time = 1.0, .action = "b"}, {.time = 0.0, .action = "a"}, {.time = 0.3, .action = "a2"},
              {.time = 1.0, .action = "c"}, {.time = 0.1, .action = "tenth"}, {.time = 4.0, .action = "end"}};
  CutscenePlayer player;
  REQUIRE(player.start(s, 1000).has_value());
  std::vector<std::pair<std::string, u64>> fired;
  for (u64 tick = 990; tick <= 1600; ++tick) {
    player.tick(tick, [&](const CutsceneEvent& e) { fired.emplace_back(e.action, tick); });
  }
  const std::vector<std::pair<std::string, u64>> expected{
      {"a", 1000},      // time 0: the very first tick
      {"tenth", 1012},  // 0.1 s * 120 = 12 ticks, not 13 (floating point)
      {"a2", 1036},     // 0.3 s = 36 ticks
      {"b", 1120},      // two events at 1 s: in their listed order
      {"c", 1120},
      {"end", 1480},    // at the duration: still fired
  };
  CHECK(fired == expected);
  CHECK(tickAtOrAfter(0.0) == 0);
  CHECK(tickAtOrAfter(1.0 / 240.0) == 1);  // half a tick in: the next tick
  CHECK(tickAtOrAfter(2.5) == 300);
}

TEST_CASE("a cutscene waits for its start tick, plays for its duration, then ends") {
  CutsceneSequence s = straightLine();
  s.subtitles = {{.start = 0.5, .end = 1.5, .speaker = "Narrator", .text = "Hello"}};
  CutscenePlayer player;
  REQUIRE(player.start(s, 100).has_value());
  player.tick(90, {});  // scheduled ahead (co-op)
  CHECK(player.active());
  CHECK_FALSE(player.playing());
  CHECK(player.time() == 0.0);
  player.tick(100, {});
  CHECK(player.playing());
  player.tick(100 + 120, {});
  CHECK(player.time() == doctest::Approx(1.0));
  REQUIRE(player.subtitle() != nullptr);
  CHECK(player.subtitle()->text == "Hello");
  player.tick(100 + 240, {});
  CHECK(player.subtitle() == nullptr);
  player.tick(100 + 479, {});
  CHECK(player.playing());
  player.tick(100 + 480, {});  // 4 s
  CHECK_FALSE(player.active());
  CHECK(player.handingOff());
  CHECK_FALSE(player.skipped());
  CHECK(player.endedTick() == 580);
}

TEST_CASE("skipping: needs a held key, fires the state-changing events left, not the effects") {
  CutsceneSequence s = straightLine();
  s.events = {{.time = 0.0, .action = "start"}, {.time = 2.0, .action = "spawn crate", .fireOnSkip = true},
              {.time = 2.5, .action = "boom sound", .fireOnSkip = false}, {.time = 3.0, .action = "open door"}};
  CutscenePlayer player;
  REQUIRE(player.start(s, 0).has_value());
  std::vector<std::string> fired;
  auto record = [&](const CutsceneEvent& e) { fired.push_back(e.action); };
  u64 tick = 0;
  player.tick(tick, record);
  // A tap is not a skip; neither is a hold that is let go too soon.
  CHECK_FALSE(player.holdSkip(true));
  for (int i = 0; i < 60; ++i) CHECK_FALSE(player.holdSkip(true));
  CHECK(player.skipProgress() > 0.6f);
  CHECK_FALSE(player.holdSkip(false));
  CHECK(player.skipProgress() == 0.0f);
  // Held for the whole 0.75 s (90 ticks): skip.
  bool skip = false;
  for (int i = 0; i < 90 && !skip; ++i) {
    player.tick(++tick, record);
    skip = player.holdSkip(true);
  }
  REQUIRE(skip);
  CHECK(tick == 90);
  player.skip(record);
  CHECK(player.skipped());
  CHECK_FALSE(player.active());
  CHECK(fired == std::vector<std::string>{"start", "spawn crate", "open door"});  // no "boom sound"
  // An unskippable one ignores the key.
  s.skippable = false;
  REQUIRE(player.start(s, 1000).has_value());
  player.tick(1000, {});
  for (int i = 0; i < 200; ++i) CHECK_FALSE(player.holdSkip(true));
}

TEST_CASE("co-op skip: ends on the agreed tick, not before") {
  CutsceneSequence s = straightLine();
  s.events = {{.time = 3.0, .action = "late but important"}};
  CutscenePlayer player;
  REQUIRE(player.start(s, 0).has_value());
  std::vector<std::string> fired;
  player.skipAt(150);
  for (u64 tick = 0; tick < 150; ++tick) player.tick(tick, [&](const CutsceneEvent& e) { fired.push_back(e.action); });
  CHECK(player.playing());
  player.tick(150, [&](const CutsceneEvent& e) { fired.push_back(e.action); });
  CHECK_FALSE(player.active());
  CHECK(player.skipped());
  CHECK(player.endedTick() == 150);
  CHECK(fired == std::vector<std::string>{"late but important"});
}

TEST_CASE("camera handoff: no jump at the end, a smooth blend, then exactly the player's camera") {
  CutsceneSequence s = straightLine();
  CutscenePlayer player;
  const CameraPose you{.position = {0, 2, 10}, .rotation = yaw(180), .fovDegrees = 50};
  CHECK(player.camera(you, 0.5f).position == you.position);  // nothing playing: the player's
  REQUIRE(player.start(s, 0).has_value());
  u64 tick = 0;
  for (; tick <= 480; ++tick) player.tick(tick, {});
  REQUIRE(player.handingOff());
  // At the moment it ends: exactly where the cutscene was (its last key).
  CHECK(glm::length(player.camera(you, 0.0f).position - s.camera.back().position) < 1e-4f);
  CHECK(player.letterbox(0.0f) == doctest::Approx(1.0f));
  // Partway: between the two, closer to the player as it goes.
  f32 lastDistance = glm::length(s.camera.back().position - you.position);
  for (int i = 0; i < 70; ++i) {
    player.tick(tick++, {});
    const f32 distance = glm::length(player.camera(you, 1.0f).position - you.position);
    CHECK(distance <= lastDistance + 1e-4f);
    lastDistance = distance;
  }
  // Then back to the player, exactly, and idle.
  for (int i = 0; i < 10; ++i) player.tick(tick++, {});
  CHECK_FALSE(player.handingOff());
  const CameraPose after = player.camera(you, 0.3f);
  CHECK(after.position == you.position);
  CHECK(angleBetween(after.rotation, you.rotation) < 1e-3f);
  CHECK(after.fovDegrees == you.fovDegrees);
  CHECK(player.letterbox(0.5f) == 0.0f);
}

TEST_CASE("cutscenes load from JSON, and bad ones are refused with a reason") {
  const auto loaded = CutsceneSequence::fromJson(R"({
    "id": "intro", "duration": 6, "skippable": true,
    "camera": [
      {"time": 4, "position": [0, 3, 6], "lookAt": [0, 0, 0], "fov": 45},
      {"time": 0, "position": [8, 4, 8], "lookAt": [0, 0, 0], "ease": "linear"}
    ],
    "events": [{"time": 2, "action": "spawn", "target": "crate", "position": [1, 5, 0]},
               {"time": 1, "action": "sound", "target": "explosion", "fireOnSkip": false}],
    "subtitles": [{"start": 0.5, "end": 3, "speaker": "Narrator", "text": "Long ago..."}]
  })");
  REQUIRE_MESSAGE(loaded.has_value(), (loaded ? "" : loaded.error().message));
  CHECK(loaded->id == "intro");
  CHECK(loaded->camera.front().time == 0.0);  // sorted
  CHECK(loaded->camera.front().ease == Ease::Linear);
  CHECK(loaded->camera.back().fovDegrees == 45.0f);
  CHECK(glm::length(loaded->cameraAt(4.0).forward() - glm::normalize(glm::vec3(0, -3, -6))) < 1e-4f);
  CHECK(loaded->events.front().action == "sound");
  CHECK_FALSE(loaded->events.front().fireOnSkip);
  CHECK(loaded->events.back().position == glm::vec3(1, 5, 0));
  CHECK(loaded->subtitleAt(1.0)->speaker == "Narrator");
  CHECK(loaded->durationTicks() == 720);

  auto refuse = [](const char* json, const char* expect) {
    CAPTURE(json);
    const auto r = CutsceneSequence::fromJson(json);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find(expect) != std::string::npos);
  };
  refuse("{nope", "not valid JSON");
  refuse(R"({"id": "x", "duration": 0, "camera": [{"time": 0, "position": [0,0,0]}]})", "duration");
  refuse(R"({"id": "x", "duration": 2, "camera": []})", "no camera keys");
  refuse(R"({"id": "x", "duration": 2, "camera": [{"time": 5, "position": [0,0,0]}]})", "outside");
  refuse(R"({"id": "x", "duration": 2, "camera": [{"time": 0, "position": [0,0]}]})", "[x, y, z]");
  refuse(R"({"id": "x", "duration": 2, "camera": [{"time": 0, "position": [0,0,0], "ease": "wobbly"}]})", "ease");
  refuse(R"({"id": "x", "duration": 2, "camera": [{"time": 0, "position": [0,0,0]}],
             "subtitles": [{"start": 1, "end": 0.5, "text": "backwards"}]})", "end after");
}
