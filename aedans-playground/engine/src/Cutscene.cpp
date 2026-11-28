#include "playground/engine/Cutscene.h"

#include "playground/core/Log.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

#include <glm/geometric.hpp>
#include <nlohmann/json.hpp>

namespace playground::engine {
namespace {

using log::Category;
using nlohmann::json;

bool finite(glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }

// Uniform Catmull-Rom between p1 and p2 (p0 and p3 their neighbours): passes
// through p1 at u = 0 and p2 at u = 1, with the tangent at each point
// parallel to the chord between its neighbours, so a path through several
// keys has no kinks.
glm::vec3 catmullRom(glm::vec3 p0, glm::vec3 p1, glm::vec3 p2, glm::vec3 p3, f32 u) {
  const f32 u2 = u * u;
  const f32 u3 = u2 * u;
  return 0.5f * (2.0f * p1 + (p2 - p0) * u + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * u2 +
                 (3.0f * p1 - p0 - 3.0f * p2 + p3) * u3);
}

}  // namespace

f32 ease(Ease curve, f32 t) noexcept {
  t = std::clamp(t, 0.0f, 1.0f);
  switch (curve) {
    case Ease::Linear: return t;
    case Ease::In: return t * t;
    case Ease::Out: return 1.0f - (1.0f - t) * (1.0f - t);
    case Ease::InOut: return t * t * (3.0f - 2.0f * t);
  }
  return t;
}

std::optional<Ease> easeFromName(std::string_view name) noexcept {
  if (name == "linear") return Ease::Linear;
  if (name == "in") return Ease::In;
  if (name == "out") return Ease::Out;
  if (name == "inOut") return Ease::InOut;
  return std::nullopt;
}

CameraPose blend(const CameraPose& a, const CameraPose& b, f32 alpha) noexcept {
  return {.position = glm::mix(a.position, b.position, alpha), .rotation = glm::slerp(a.rotation, b.rotation, alpha),
          .fovDegrees = a.fovDegrees + (b.fovDegrees - a.fovDegrees) * alpha};
}

glm::quat lookRotation(glm::vec3 eye, glm::vec3 target) noexcept {
  const glm::vec3 direction = target - eye;
  if (glm::length(direction) < 1e-6f) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  const glm::vec3 forward = glm::normalize(direction);
  // Straight up or down, "up" is undefined: use -z as up instead.
  const glm::vec3 up = std::abs(forward.y) > 0.999f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
  return glm::quatLookAt(forward, up);
}

u64 tickAtOrAfter(f64 time) noexcept {
  if (!(time > 0.0)) return 0;
  // A hair under, so 0.1 s * 120 = 12.000000000000002 is tick 12, not 13.
  return static_cast<u64>(std::ceil(time * kCutsceneTicksPerSecond - 1e-6));
}

// --- The sequence ----------------------------------------------------------------------

CameraPose CutsceneSequence::cameraAt(f64 time) const {
  if (camera.empty()) return {};
  const CameraKey& first = camera.front();
  const CameraKey& last = camera.back();
  if (time <= first.time || camera.size() == 1) {
    return {.position = first.position, .rotation = first.rotation, .fovDegrees = first.fovDegrees};
  }
  if (time >= last.time) return {.position = last.position, .rotation = last.rotation, .fovDegrees = last.fovDegrees};
  usize i = 0;
  while (i + 2 < camera.size() && camera[i + 1].time <= time) ++i;
  const CameraKey& a = camera[i];
  const CameraKey& b = camera[i + 1];
  const f64 span = b.time - a.time;
  const f32 u = ease(a.ease, span > 0.0 ? static_cast<f32>((time - a.time) / span) : 1.0f);
  // The neighbours; at the ends, mirrored (so the path ends heading straight at the last key).
  const glm::vec3 before = i > 0 ? camera[i - 1].position : 2.0f * a.position - b.position;
  const glm::vec3 after = i + 2 < camera.size() ? camera[i + 2].position : 2.0f * b.position - a.position;
  return {.position = catmullRom(before, a.position, b.position, after, u),
          .rotation = glm::slerp(a.rotation, b.rotation, u),
          .fovDegrees = a.fovDegrees + (b.fovDegrees - a.fovDegrees) * u};
}

const Subtitle* CutsceneSequence::subtitleAt(f64 time) const {
  for (const Subtitle& s : subtitles) {
    if (time >= s.start && time < s.end) return &s;
  }
  return nullptr;
}

u64 CutsceneSequence::durationTicks() const noexcept { return tickAtOrAfter(duration); }

Status CutsceneSequence::validate() {
  if (!(duration > 0.0) || !std::isfinite(duration)) {
    return fail(Errc::InvalidArgument, "cutscene \"{}\": the duration must be positive", id);
  }
  if (camera.empty()) return fail(Errc::InvalidArgument, "cutscene \"{}\": no camera keys", id);
  auto byTime = [](const auto& a, const auto& b) { return a.time < b.time; };
  std::stable_sort(camera.begin(), camera.end(), byTime);
  std::stable_sort(events.begin(), events.end(), byTime);
  std::stable_sort(subtitles.begin(), subtitles.end(), [](const auto& a, const auto& b) { return a.start < b.start; });
  for (CameraKey& key : camera) {
    if (!(key.time >= 0.0) || key.time > duration || !finite(key.position) || !(key.fovDegrees > 1.0f) ||
        !(key.fovDegrees < 179.0f)) {
      return fail(Errc::InvalidArgument, "cutscene \"{}\": camera key at {} s is outside 0..{} s or not finite", id,
                  key.time, duration);
    }
    const f32 length = glm::length(key.rotation);
    if (!(length > 1e-6f) || !std::isfinite(length)) {
      return fail(Errc::InvalidArgument, "cutscene \"{}\": camera key at {} s has no rotation", id, key.time);
    }
    key.rotation /= length;
  }
  for (const CutsceneEvent& event : events) {
    if (!(event.time >= 0.0) || event.time > duration || event.action.empty() || !finite(event.position)) {
      return fail(Errc::InvalidArgument, "cutscene \"{}\": event \"{}\" at {} s is outside 0..{} s or unnamed", id,
                  event.action, event.time, duration);
    }
  }
  for (const Subtitle& s : subtitles) {
    if (!(s.start >= 0.0) || !(s.end > s.start)) {
      return fail(Errc::InvalidArgument, "cutscene \"{}\": a subtitle must end after it starts (\"{}\")", id, s.text);
    }
  }
  return {};
}

Result<CutsceneSequence> CutsceneSequence::fromJson(std::string_view text) {
  const json doc = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (doc.is_discarded() || !doc.is_object()) return fail(Errc::InvalidArgument, "cutscene: not valid JSON");
  CutsceneSequence out;
  auto vec3 = [](const json& v, const char* what) {
    if (!v.is_array() || v.size() != 3 || !v[0].is_number() || !v[1].is_number() || !v[2].is_number()) {
      throw std::runtime_error(std::format("\"{}\" must be [x, y, z]", what));
    }
    return glm::vec3(v[0].get<f32>(), v[1].get<f32>(), v[2].get<f32>());
  };
  try {
    out.id = doc.value("id", std::string("cutscene"));
    out.duration = doc.at("duration").get<f64>();
    out.skippable = doc.value("skippable", true);
    for (const json& k : doc.at("camera")) {
      CameraKey key{.time = k.at("time").get<f64>(), .position = vec3(k.at("position"), "position")};
      if (k.contains("lookAt")) {
        key.rotation = lookRotation(key.position, vec3(k["lookAt"], "lookAt"));
      } else if (k.contains("rotation")) {
        const json& r = k["rotation"];
        if (!r.is_array() || r.size() != 4) throw std::runtime_error("\"rotation\" must be [w, x, y, z]");
        key.rotation = glm::quat(r[0].get<f32>(), r[1].get<f32>(), r[2].get<f32>(), r[3].get<f32>());
      }
      key.fovDegrees = k.value("fov", 50.0f);
      const auto curve = easeFromName(k.value("ease", std::string("inOut")));
      if (!curve) throw std::runtime_error("\"ease\" must be linear, in, out or inOut");
      key.ease = *curve;
      out.camera.push_back(key);
    }
    if (doc.contains("events")) {
      for (const json& e : doc["events"]) {
        CutsceneEvent event{.time = e.at("time").get<f64>(), .action = e.at("action").get<std::string>(),
                            .target = e.value("target", std::string()), .value = e.value("value", 0.0f),
                            .fireOnSkip = e.value("fireOnSkip", true)};
        if (e.contains("position")) event.position = vec3(e["position"], "position");
        out.events.push_back(std::move(event));
      }
    }
    if (doc.contains("subtitles")) {
      for (const json& s : doc["subtitles"]) {
        out.subtitles.push_back({.start = s.at("start").get<f64>(), .end = s.at("end").get<f64>(),
                                 .speaker = s.value("speaker", std::string()), .text = s.at("text").get<std::string>()});
      }
    }
  } catch (const std::exception& error) {
    return fail(Errc::InvalidArgument, "cutscene \"{}\": {}", out.id, error.what());
  }
  if (Status s = out.validate(); !s) return tl::unexpected(std::move(s.error()));
  return out;
}

Result<CutsceneSequence> CutsceneSequence::loadFromFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return fail(Errc::IoFailure, "cutscene: could not open {}", path);
  std::ostringstream text;
  text << in.rdbuf();
  return fromJson(text.str());
}

// --- The player ------------------------------------------------------------------------

Status CutscenePlayer::start(const CutsceneSequence& sequence, u64 startTick) {
  CutsceneSequence copy = sequence;
  if (Status s = copy.validate(); !s) return s;
  sequence_ = std::move(copy);
  state_ = State::Waiting;
  startTick_ = startTick;
  lastTick_ = startTick;
  endedTick_ = 0;
  scheduledEnd_.reset();
  nextEvent_ = 0;
  skipHeldTicks_ = 0;
  skipped_ = false;
  log::info(Category::Game, "cutscene \"{}\" ({:.1f} s, {} camera keys, {} events, {} subtitles) starts on tick {}",
            sequence_.id, sequence_.duration, sequence_.camera.size(), sequence_.events.size(),
            sequence_.subtitles.size(), startTick);
  return {};
}

void CutscenePlayer::fireUntil(f64 time, const EventHandler& onEvent) {
  const u64 elapsed = tickAtOrAfter(time);
  while (nextEvent_ < sequence_.events.size() && tickAtOrAfter(sequence_.events[nextEvent_].time) <= elapsed) {
    const CutsceneEvent& event = sequence_.events[nextEvent_++];
    if (onEvent) onEvent(event);
  }
}

void CutscenePlayer::end(u64 tick) {
  lastPose_ = sequence_.cameraAt(std::min(time(), sequence_.duration));
  state_ = State::HandingOff;
  endedTick_ = tick;
  lastTick_ = tick;
  scheduledEnd_.reset();
  skipHeldTicks_ = 0;
  log::info(Category::Game, "cutscene \"{}\" {} on tick {}", sequence_.id, skipped_ ? "skipped" : "finished", tick);
}

void CutscenePlayer::tick(u64 tick, const EventHandler& onEvent) {
  switch (state_) {
    case State::Idle: return;
    case State::HandingOff:
      lastTick_ = tick;
      if (tick >= endedTick_ + tickAtOrAfter(kHandoffSeconds)) state_ = State::Idle;
      return;
    case State::Waiting:
    case State::Playing: break;
  }
  if (tick < startTick_) {  // scheduled ahead (co-op): not yet
    lastTick_ = tick;
    return;
  }
  state_ = State::Playing;
  lastTick_ = tick;
  if (scheduledEnd_ && tick >= *scheduledEnd_) {  // a co-op skip, on the agreed tick
    skip(onEvent);
    return;
  }
  const u64 elapsed = tick - startTick_;
  fireUntil(static_cast<f64>(elapsed) / kCutsceneTicksPerSecond, onEvent);
  if (elapsed >= sequence_.durationTicks()) {
    fireUntil(sequence_.duration, onEvent);
    end(tick);
  }
}

void CutscenePlayer::skip(const EventHandler& onEvent) {
  if (!active()) return;
  // What must still happen (the world ends up as if it had played), in order.
  while (nextEvent_ < sequence_.events.size()) {
    const CutsceneEvent& event = sequence_.events[nextEvent_++];
    if (event.fireOnSkip && onEvent) onEvent(event);
  }
  skipped_ = true;
  end(lastTick_);
}

bool CutscenePlayer::holdSkip(bool held) noexcept {
  if (!playing() || !sequence_.skippable || !held) {
    skipHeldTicks_ = 0;
    return false;
  }
  ++skipHeldTicks_;
  return skipHeldTicks_ >= tickAtOrAfter(kSkipHoldSeconds);
}

f32 CutscenePlayer::skipProgress() const noexcept {
  return std::min(1.0f, static_cast<f32>(skipHeldTicks_) / static_cast<f32>(tickAtOrAfter(kSkipHoldSeconds)));
}

f64 CutscenePlayer::time() const noexcept {
  if (state_ == State::Idle) return 0.0;
  const u64 at = state_ == State::HandingOff ? endedTick_ : lastTick_;
  if (at <= startTick_) return 0.0;
  return std::min(static_cast<f64>(at - startTick_) / kCutsceneTicksPerSecond, sequence_.duration);
}

CameraPose CutscenePlayer::camera(const CameraPose& player, f32 alpha) const {
  switch (state_) {
    case State::Idle:
    case State::Waiting: return player;
    case State::Playing: {
      // Between the previous tick and this one, like everything else drawn.
      const f64 ticks = static_cast<f64>(lastTick_ - startTick_) - 1.0 + static_cast<f64>(alpha);
      return sequence_.cameraAt(std::clamp(ticks / kCutsceneTicksPerSecond, 0.0, sequence_.duration));
    }
    case State::HandingOff: {
      const f64 ticks = static_cast<f64>(lastTick_ - endedTick_) - 1.0 + static_cast<f64>(alpha);
      const auto h = static_cast<f32>(std::clamp(ticks / (kHandoffSeconds * kCutsceneTicksPerSecond), 0.0, 1.0));
      return blend(lastPose_, player, ease(Ease::InOut, h));
    }
  }
  return player;
}

const Subtitle* CutscenePlayer::subtitle() const { return playing() ? sequence_.subtitleAt(time()) : nullptr; }

f32 CutscenePlayer::letterbox(f32 alpha) const {
  constexpr f64 kSlideSeconds = 0.5;
  if (state_ == State::Playing) {
    const f64 t = (static_cast<f64>(lastTick_ - startTick_) - 1.0 + alpha) / kCutsceneTicksPerSecond;
    return ease(Ease::InOut, static_cast<f32>(std::clamp(t / kSlideSeconds, 0.0, 1.0)));
  }
  if (state_ == State::HandingOff) {
    const f64 t = (static_cast<f64>(lastTick_ - endedTick_) - 1.0 + alpha) / kCutsceneTicksPerSecond;
    return 1.0f - ease(Ease::InOut, static_cast<f32>(std::clamp(t / kSlideSeconds, 0.0, 1.0)));
  }
  return 0.0f;
}

}  // namespace playground::engine
