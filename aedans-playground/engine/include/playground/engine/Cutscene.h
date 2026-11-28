#pragma once
// =============================================================================
//  playground/engine/Cutscene.h — cutscenes: choreographed camera, timed
//  events and subtitles, played on the fixed-step clock.
//
//  A CutsceneSequence is DATA (it loads from JSON: fromJson), three tracks
//  over `duration` seconds:
//    camera     keyframes {time, position, rotation, field of view, ease}.
//               Between two keys the camera follows a Catmull-Rom spline
//               through the key positions (it passes through every key, and
//               its speed does not jump at them), turns by slerp (the short
//               way, at an even rate) and changes its field of view, all at
//               the segment's eased pace (`ease` of the key it leaves).
//    events     {time, action, target, position, value, fireOnSkip}: named
//               things to do ("spawn", "light", "sound"...) the game handles
//               (CutscenePlayer's EventHandler). fireOnSkip: state changes
//               (a door opens, a crate appears) still happen when the
//               cutscene is skipped, so the world ends up the same either
//               way; effects (a sound, a flash) do not.
//    subtitles  {start, end, speaker, text}.
//
//  CutscenePlayer plays one on the game's FIXED STEPS, by tick number, not
//  by wall-clock time: tick n of a cutscene started on tick s is at time
//  (n - s) / 120 s. So it is deterministic, the same on every machine, and
//  in co-op every player sees the same moment on the same server tick. An
//  event at time T fires on the first tick at or after T (tick s + ceil(T
//  * 120)), exactly once, events at one time in their listed order.
//
//  SKIPPING (hold, not tap: a stray key press must not throw a scene away):
//  hold Space or Escape for kSkipHoldSeconds. Solo, the skip happens at
//  once; in co-op the server ends it only when every player holds skip.
//  Skipping fires the remaining fireOnSkip events, in order, then ends.
//
//  HANDOFF: when a cutscene ends the camera is not cut back to the player:
//  it blends from the cutscene's last pose to the player's own camera over
//  kHandoffSeconds (eased), starting exactly where the cutscene left off.
// =============================================================================

#include "playground/core/Result.h"
#include "playground/core/Types.h"

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <glm/gtc/quaternion.hpp>
#include <glm/vec3.hpp>

namespace playground::engine {

inline constexpr f64 kCutsceneTicksPerSecond = 120.0;  // the fixed step (core/FixedStep.h)
inline constexpr f64 kSkipHoldSeconds = 0.75;
inline constexpr f64 kHandoffSeconds = 0.6;

enum class Ease : u8 { Linear, In, Out, InOut };
// t in [0, 1] -> [0, 1]: In starts slow, Out ends slow, InOut both (smoothstep).
[[nodiscard]] f32 ease(Ease curve, f32 t) noexcept;
[[nodiscard]] std::optional<Ease> easeFromName(std::string_view name) noexcept;

struct CameraKey {
  f64 time = 0.0;
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  f32 fovDegrees = 50.0f;
  Ease ease = Ease::InOut;  // the pace of the segment that starts here
};

struct CameraPose {
  glm::vec3 position{0.0f};
  glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
  f32 fovDegrees = 50.0f;
  [[nodiscard]] glm::vec3 forward() const noexcept { return rotation * glm::vec3(0.0f, 0.0f, -1.0f); }
};
// `alpha` of the way from a to b (position lerp, rotation slerp, FOV lerp).
[[nodiscard]] CameraPose blend(const CameraPose& a, const CameraPose& b, f32 alpha) noexcept;
// A rotation that looks from `eye` at `target` (+y up).
[[nodiscard]] glm::quat lookRotation(glm::vec3 eye, glm::vec3 target) noexcept;

struct CutsceneEvent {
  f64 time = 0.0;
  std::string action;  // what to do, for the game's handler: "spawn", "light", "sound"...
  std::string target;  // what to do it to (a Name, a sound...), if anything
  glm::vec3 position{0.0f};
  f32 value = 0.0f;
  bool fireOnSkip = true;
};

struct Subtitle {
  f64 start = 0.0;
  f64 end = 0.0;
  std::string speaker;
  std::string text;
};

struct CutsceneSequence {
  std::string id;
  f64 duration = 0.0;  // seconds
  bool skippable = true;
  std::vector<CameraKey> camera;      // by time
  std::vector<CutsceneEvent> events;  // by time (stable: same-time events keep their order)
  std::vector<Subtitle> subtitles;

  // The camera at `time` (clamped to the first and last keys). Needs a key.
  [[nodiscard]] CameraPose cameraAt(f64 time) const;
  [[nodiscard]] const Subtitle* subtitleAt(f64 time) const;
  [[nodiscard]] u64 durationTicks() const noexcept;
  // Sorts the tracks by time and checks them: a duration, keys within it,
  // finite numbers, subtitles that end after they start.
  [[nodiscard]] Status validate();

  // JSON (see README "Cutscenes"): {"id", "duration", "skippable", "camera":
  // [{"time", "position": [x,y,z], "lookAt": [x,y,z] or "rotation": [w,x,y,z],
  // "fov", "ease"}], "events": [...], "subtitles": [...]}. All or nothing.
  [[nodiscard]] static Result<CutsceneSequence> fromJson(std::string_view json);
  [[nodiscard]] static Result<CutsceneSequence> loadFromFile(const std::string& path);
};

// The first tick (counted from the cutscene's start) at or after `time`.
[[nodiscard]] u64 tickAtOrAfter(f64 time) noexcept;

class CutscenePlayer {
 public:
  using EventHandler = std::function<void(const CutsceneEvent&)>;

  // Starts `sequence` (copied, then validated: an invalid one does not
  // start) so that its time 0 is fixed-step tick `startTick`. Replaces one
  // already playing (without firing the rest of its events).
  [[nodiscard]] Status start(const CutsceneSequence& sequence, u64 startTick);

  // Once per fixed step, with that step's tick number: fires every event
  // due by now, once each, and ends the cutscene at its duration. Ticks
  // before startTick do nothing (a co-op start scheduled ahead).
  void tick(u64 tick, const EventHandler& onEvent);

  // Ends it now (or at `endTick`, for co-op: tick() ends it there), first
  // firing the remaining fireOnSkip events.
  void skip(const EventHandler& onEvent);
  void skipAt(u64 endTick) noexcept { scheduledEnd_ = endTick; }

  // Skip-holding, once per fixed step: true once held for kSkipHoldSeconds.
  // Releasing starts the count again.
  bool holdSkip(bool held) noexcept;
  [[nodiscard]] f32 skipProgress() const noexcept;  // 0..1, for the prompt

  [[nodiscard]] bool active() const noexcept { return state_ == State::Playing || state_ == State::Waiting; }
  [[nodiscard]] bool playing() const noexcept { return state_ == State::Playing; }
  // Ended, and the camera is still blending back (see handoff()).
  [[nodiscard]] bool handingOff() const noexcept { return state_ == State::HandingOff; }
  [[nodiscard]] bool skipped() const noexcept { return skipped_; }
  [[nodiscard]] const CutsceneSequence* sequence() const noexcept { return active() || handingOff() ? &sequence_ : nullptr; }
  // Time into the cutscene at the last tick.
  [[nodiscard]] f64 time() const noexcept;
  [[nodiscard]] u64 startTick() const noexcept { return startTick_; }
  [[nodiscard]] u64 endedTick() const noexcept { return endedTick_; }

  // The camera to draw: `alpha` of the way between the last two ticks.
  // While playing, the cutscene's; while handing off, blended towards
  // `player` (the player's own camera now); otherwise `player` itself.
  [[nodiscard]] CameraPose camera(const CameraPose& player, f32 alpha) const;
  [[nodiscard]] const Subtitle* subtitle() const;
  // 0 (no bars) .. 1 (full letterbox), easing in and out with the cutscene.
  [[nodiscard]] f32 letterbox(f32 alpha) const;

 private:
  enum class State : u8 { Idle, Waiting, Playing, HandingOff };
  void fireUntil(f64 time, const EventHandler& onEvent);
  void end(u64 tick);

  CutsceneSequence sequence_;
  State state_ = State::Idle;
  u64 startTick_ = 0;
  u64 lastTick_ = 0;
  u64 endedTick_ = 0;
  std::optional<u64> scheduledEnd_;
  usize nextEvent_ = 0;
  CameraPose lastPose_;  // the cutscene's pose when it ended
  u32 skipHeldTicks_ = 0;
  bool skipped_ = false;
};

}  // namespace playground::engine
