#pragma once
// =============================================================================
//  playground/engine/CameraController.h — steering a 3D camera with the
//  mouse and keyboard.
//
//  Two ways to move, switched with setMode() (the demo uses Tab):
//
//    ORBIT  (looking at a model) The camera circles a TARGET point at a
//           distance, always facing it, like a turntable.
//             left-drag      spin around the target (yaw) and tilt (pitch)
//             right-drag     slide the target sideways/up (pan)
//             wheel, Q / E   closer / farther
//             A D / arrows   spin;  W S / arrows  tilt
//
//    FLY    (walking around a level) The camera moves freely and looks
//           where you point it, like a spectator in a game.
//             W A S D        forward, left, back, right (where you look)
//             E / Space      up;  Q / Ctrl  down   (world up, not view up)
//             right-drag     look around
//             Shift          4x faster;  wheel  change the speed
//
//  YAW is the turn left/right about the world's up axis (yaw 0 looks along
//  -z, the way the default camera does); PITCH is the tilt up (+) or down
//  (-), kept short of straight up or down, where "up on screen" would flip.
//
//  Switching mode keeps the view exactly: the camera does not jump. Mouse
//  movement is measured between update() calls, so the first call after
//  construction (or reset) never sees a jump either.
//
//  Per frame, not per fixed step: the camera is how you look at the
//  simulation, not part of it, and should be as smooth as the display.
// =============================================================================

#include "playground/core/Types.h"
#include "playground/engine/Camera.h"
#include "playground/platform/Input.h"

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace playground::engine {

enum class CameraMode : u8 { Orbit, Fly };

struct CameraControls {
  f32 dragRadiansPerPixel = 0.006f;  // orbit spin/tilt and fly look
  f32 keyRadiansPerSecond = 1.6f;    // orbit spin/tilt with the keys
  f32 zoomPerClick = 0.85f;          // orbit: distance x this per wheel click towards you
  f32 zoomPerSecond = 2.5f;          // orbit: Q/E, distance x (or /) this per second
  f32 minDistance = 0.05f;
  f32 maxDistance = 5000.0f;
  f32 flySpeed = 3.0f;               // units per second
  f32 fastMultiplier = 4.0f;         // with Shift
  f32 speedPerClick = 1.25f;         // fly: speed x this per wheel click
};

class CameraController {
 public:
  explicit CameraController(CameraControls controls = {}) noexcept : controls_(controls) {}

  // Orbit `target` from `distance` away, at the given yaw and pitch (radians).
  void orbit(glm::vec3 target, f32 distance, f32 yaw = 0.0f, f32 pitch = 0.0f) noexcept;
  // Orbit a sphere's centre from far enough away to see all of it with this
  // vertical field of view (and a little margin).
  void frame(glm::vec3 centre, f32 radius, f32 fovY, f32 yaw = 0.0f, f32 pitch = -0.35f) noexcept;

  void setMode(CameraMode mode) noexcept;  // keeps the current view
  [[nodiscard]] CameraMode mode() const noexcept { return mode_; }

  // Applies this frame's input, then writes the result into `camera`
  // (position, target, up; perspective projection).
  void update(const platform::InputState& input, f32 deltaSeconds, Camera& camera) noexcept;

  [[nodiscard]] glm::vec3 eye() const noexcept;
  [[nodiscard]] glm::vec3 forward() const noexcept;  // unit length, where the camera looks
  [[nodiscard]] glm::vec3 target() const noexcept { return target_; }
  [[nodiscard]] f32 distance() const noexcept { return distance_; }
  [[nodiscard]] f32 yaw() const noexcept { return yaw_; }
  [[nodiscard]] f32 pitch() const noexcept { return pitch_; }
  [[nodiscard]] f32 flySpeed() const noexcept { return controls_.flySpeed; }

 private:
  void updateOrbit(const platform::InputState& input, f32 dt, glm::vec2 mouseDelta) noexcept;
  void updateFly(const platform::InputState& input, f32 dt, glm::vec2 mouseDelta) noexcept;
  void clampPitch() noexcept;

  CameraControls controls_;
  CameraMode mode_ = CameraMode::Orbit;
  // Orbit: the eye is target - forward * distance. Fly: the eye is position_.
  glm::vec3 target_{0.0f};
  f32 distance_ = 5.0f;
  glm::vec3 position_{0.0f, 0.0f, 5.0f};
  f32 yaw_ = 0.0f;
  f32 pitch_ = 0.0f;
  glm::vec2 lastMouse_{0.0f};
  bool hasLastMouse_ = false;
};

}  // namespace playground::engine
