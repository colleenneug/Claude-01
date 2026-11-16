#include "Camera.h"
#include <algorithm>

glm::vec3 Camera::forward() const {
  float ry = glm::radians(yaw), rp = glm::radians(pitch);
  return glm::normalize(glm::vec3(std::cos(ry) * std::cos(rp), std::sin(rp), std::sin(ry) * std::cos(rp)));
}

glm::vec3 Camera::right() const {
  return glm::normalize(glm::cross(forward(), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::mat4 Camera::view() const {
  return glm::lookAt(position, position + forward(), glm::vec3(0.0f, 1.0f, 0.0f));
}

void Camera::look(float dxPixels, float dyPixels, float sensitivity) {
  yaw += dxPixels * sensitivity;
  pitch -= dyPixels * sensitivity;
  pitch = std::clamp(pitch, -89.0f, 89.0f);
}

void Camera::update(const Input& in, float dt, bool aiming) {
  float target = aiming ? 1.0f : 0.0f;
  aim += (target - aim) * std::min(1.0f, dt * 10.0f);

  bool sprint = in.held(Key::LeftShift) && !aiming;
  float speed = (sprint ? 8.5f : aiming ? 2.6f : 4.6f) * dt;

  glm::vec3 f = forward(), r = right();
  f.y = 0.0f; if (glm::length(f) > 1e-4f) f = glm::normalize(f);
  glm::vec3 move(0.0f);
  if (in.held(Key::W)) move += f;
  if (in.held(Key::S)) move -= f;
  if (in.held(Key::D)) move += r;
  if (in.held(Key::A)) move -= r;
  if (glm::length(move) > 1e-4f) position += glm::normalize(move) * speed;

  if (in.held(Key::Space)) position.y += speed;
  if (in.held(Key::LeftControl)) position.y -= speed;
  position.y = std::max(position.y, 1.2f);
}
