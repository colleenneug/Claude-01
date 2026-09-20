#pragma once
#include "Gl.h"
#include "Camera.h"
#include "Content.h"
#include <string>
#include <vector>

// Scripted camera beats.
//
// A cutscene here is a run of held shots sharing a name (CutsceneShot::scene
// in Content.h). Consecutive shots of the same scene interpolate into each
// other, so a slow push in is two lines of content rather than a keyframe
// format, and a hard cut is two shots that do not share a position.
//
// While one is playing the world keeps simulating but takes no input: the
// alarm keeps sounding and nothing walks into a frozen room, and you cannot
// walk out of your own establishing shot. Space, Enter or Escape skips it,
// because a cutscene you have already seen is a loading screen.
class Cutscene {
public:
  // Queues every shot belonging to `scene`. Does nothing, and returns false,
  // if the mission has no scene by that name — a mission without cutscenes
  // is not an error, it just has none.
  bool play(const std::vector<CutsceneShot>& all, const std::string& scene);

  void stop() { shots_.clear(); index_ = 0; t_ = 0.0f; }
  bool playing() const { return index_ < shots_.size(); }

  // Advances, and drives `camera` while it runs. Returns true on the frame
  // it finishes, so the caller can hand control back exactly once.
  bool update(float dt, Camera& camera);

  // Skips to the end. Called on the skip key.
  void skip() { stop(); }

  const std::string& caption() const { return caption_; }
  // 0 at a cut, rising to 1: the letterbox slides in and the picture fades
  // up rather than snapping, which is the difference between a cut and a
  // glitch.
  float fade() const { return fade_; }

private:
  std::vector<CutsceneShot> shots_;
  size_t index_ = 0;
  float t_ = 0.0f;
  float fade_ = 0.0f;
  std::string caption_;
};
