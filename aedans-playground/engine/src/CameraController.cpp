#include "playground/engine/CameraController.h"

#include <algorithm>
#include <cmath>

#include <glm/geometric.hpp>

namespace playground::engine {

using platform::Key;
using platform::MouseButton;

namespace {

constexpr glm::vec3 kWorldUp{0.0f, 1.0f, 0.0f};
constexpr f32 kMaxPitch = 1.5f;  // about 86 degrees: never quite straight up or down

f32 axis(const platform::InputState& input, Key negative, Key positive, Key negative2, Key positive2) noexcept {
  const bool minus = input.down(negative) || input.down(negative2);
  const bool plus = input.down(positive) || input.down(positive2);
  return (plus ? 1.0f : 0.0f) - (minus ? 1.0f : 0.0f);
}

}  // namespace

void CameraController::orbit(glm::vec3 target, f32 distance, f32 yaw, f32 pitch) noexcept {
  mode_ = CameraMode::Orbit;
  target_ = target;
  distance_ = std::clamp(distance, controls_.minDistance, controls_.maxDistance);
  yaw_ = yaw;
  pitch_ = pitch;
  clampPitch();
  position_ = eye();
}

void CameraController::frame(glm::vec3 centre, f32 radius, f32 fovY, f32 yaw, f32 pitch) noexcept {
  // The sphere fits when its radius is seen at half the field of view.
  const f32 distance = std::max(radius, 0.01f) / std::sin(0.5f * fovY) * 1.1f;
  orbit(centre, distance, yaw, pitch);
}

glm::vec3 CameraController::forward() const noexcept {
  const f32 c = std::cos(pitch_);
  return {std::sin(yaw_) * c, std::sin(pitch_), -std::cos(yaw_) * c};
}

glm::vec3 CameraController::eye() const noexcept {
  return mode_ == CameraMode::Orbit ? target_ - forward() * distance_ : position_;
}

void CameraController::setMode(CameraMode mode) noexcept {
  if (mode == mode_) return;
  if (mode != CameraMode::Orbit) {
    if (mode_ == CameraMode::Orbit) position_ = eye();  // stand where the orbit camera was, looking the same way
  } else {
    target_ = position_ + forward() * distance_;  // orbit the point the same distance ahead
  }
  mode_ = mode;
}

void CameraController::clampPitch() noexcept { pitch_ = std::clamp(pitch_, -kMaxPitch, kMaxPitch); }

void CameraController::update(const platform::InputState& input, f32 deltaSeconds, Camera& camera) noexcept {
  const glm::vec2 mouse{static_cast<f32>(input.mouseX()), static_cast<f32>(input.mouseY())};
  const glm::vec2 mouseDelta = hasLastMouse_ ? mouse - lastMouse_ : glm::vec2(0.0f);
  lastMouse_ = mouse;
  hasLastMouse_ = true;

  const f32 dt = std::max(deltaSeconds, 0.0f);
  switch (mode_) {
    case CameraMode::Orbit: updateOrbit(input, dt, mouseDelta); break;
    case CameraMode::Fly: updateFly(input, dt, mouseDelta); break;
    case CameraMode::Walk: updateLook(input, mouseDelta); break;
  }

  camera.projection = Projection::Perspective;
  camera.position = eye();
  camera.target = camera.position + forward();
  camera.up = kWorldUp;
}

void CameraController::updateOrbit(const platform::InputState& input, f32 dt, glm::vec2 mouseDelta) noexcept {
  // Spin and tilt: drag "grabs" the scene (drag right, it turns right).
  if (input.down(MouseButton::Left)) {
    yaw_ += mouseDelta.x * controls_.dragRadiansPerPixel;
    pitch_ -= mouseDelta.y * controls_.dragRadiansPerPixel;
  }
  yaw_ += axis(input, Key::A, Key::D, Key::Left, Key::Right) * controls_.keyRadiansPerSecond * dt;
  pitch_ += axis(input, Key::S, Key::W, Key::Down, Key::Up) * controls_.keyRadiansPerSecond * dt;
  clampPitch();

  // Pan: move the target across the view, at a speed that keeps the point
  // under the cursor roughly under it at any distance.
  if (input.down(MouseButton::Right)) {
    const glm::vec3 right = glm::normalize(glm::cross(forward(), kWorldUp));
    const glm::vec3 up = glm::cross(right, forward());
    const f32 unitsPerPixel = distance_ * 0.0015f;
    target_ += (-mouseDelta.x * right + mouseDelta.y * up) * unitsPerPixel;
  }

  // Zoom: multiplicative, so each click feels the same close up and far away.
  distance_ *= std::pow(controls_.zoomPerClick, static_cast<f32>(input.scroll()));
  const f32 zoomKeys = axis(input, Key::E, Key::Q, Key::Unknown, Key::Unknown);  // Q out, E in
  distance_ *= std::pow(controls_.zoomPerSecond, zoomKeys * dt);
  distance_ = std::clamp(distance_, controls_.minDistance, controls_.maxDistance);
}

// Look: drag right, look right; drag up, look up.
void CameraController::updateLook(const platform::InputState& input, glm::vec2 mouseDelta) noexcept {
  if (input.down(MouseButton::Right)) {
    yaw_ += mouseDelta.x * controls_.dragRadiansPerPixel;
    pitch_ -= mouseDelta.y * controls_.dragRadiansPerPixel;
    clampPitch();
  }
}

void CameraController::updateFly(const platform::InputState& input, f32 dt, glm::vec2 mouseDelta) noexcept {
  updateLook(input, mouseDelta);
  if (input.scroll() != 0.0) {
    controls_.flySpeed *= std::pow(controls_.speedPerClick, static_cast<f32>(input.scroll()));
    controls_.flySpeed = std::clamp(controls_.flySpeed, 0.01f, 1000.0f);
  }

  const glm::vec3 ahead = forward();
  const glm::vec3 right = glm::normalize(glm::cross(ahead, kWorldUp));
  glm::vec3 move = ahead * axis(input, Key::S, Key::W, Key::Down, Key::Up) +
                   right * axis(input, Key::A, Key::D, Key::Left, Key::Right) +
                   kWorldUp * axis(input, Key::Q, Key::E, Key::LeftControl, Key::Space);
  if (glm::length(move) > 0.0f) move = glm::normalize(move);  // diagonals are not faster
  const bool fast = input.down(Key::LeftShift) || input.down(Key::RightShift);
  position_ += move * controls_.flySpeed * (fast ? controls_.fastMultiplier : 1.0f) * dt;
}

}  // namespace playground::engine
