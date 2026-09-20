#include "Cutscene.h"
#include <algorithm>
#include <cmath>

bool Cutscene::play(const std::vector<CutsceneShot>& all, const std::string& scene) {
  stop();
  for (const CutsceneShot& s : all) {
    if (s.scene == scene) shots_.push_back(s);
  }
  if (shots_.empty()) return false;
  caption_ = shots_[0].caption;
  return true;
}

bool Cutscene::update(float dt, Camera& camera) {
  if (!playing()) return false;

  const CutsceneShot& shot = shots_[index_];
  t_ += dt;
  float u = shot.seconds > 0.0001f ? std::clamp(t_ / shot.seconds, 0.0f, 1.0f) : 1.0f;

  // Where the camera is: this shot's position, eased toward the next one if
  // the next belongs to the same scene. Smoothstep rather than linear —
  // a camera that starts and stops at constant speed reads as a slide, not
  // as a move.
  glm::vec3 from = shot.from;
  glm::vec3 look = shot.lookAt;
  if (index_ + 1 < shots_.size()) {
    float e = u * u * (3.0f - 2.0f * u);
    from = glm::mix(shot.from, shots_[index_ + 1].from, e * 0.5f);
    look = glm::mix(shot.lookAt, shots_[index_ + 1].lookAt, e * 0.5f);
  }

  camera.position = from;
  glm::vec3 d = look - from;
  if (glm::length(d) > 1e-4f) {
    d = glm::normalize(d);
    camera.yaw = glm::degrees(std::atan2(d.z, d.x));
    camera.pitch = glm::degrees(std::asin(std::clamp(d.y, -1.0f, 1.0f)));
  }

  // Fade up over the first third of a shot and hold. The last shot also
  // fades back down, so the hand-off to gameplay is a dissolve rather than
  // the bars vanishing mid-sentence.
  const float in = std::clamp(t_ / std::max(0.25f, shot.seconds * 0.3f), 0.0f, 1.0f);
  const bool last = index_ + 1 >= shots_.size();
  const float out = last ? std::clamp((shot.seconds - t_) / 0.45f, 0.0f, 1.0f) : 1.0f;
  fade_ = std::min(in, out);
  caption_ = shot.caption;

  if (t_ >= shot.seconds) {
    index_++;
    t_ = 0.0f;
    if (!playing()) {
      fade_ = 0.0f;
      caption_.clear();
      return true;
    }
    caption_ = shots_[index_].caption;
  }
  return false;
}
