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

void Camera::update(GLFWwindow* window, float dt, bool aiming) {
  float target = aiming ? 1.0f : 0.0f;
  aim += (target - aim) * std::min(1.0f, dt * 10.0f);

  bool sprint = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS && !aiming;
  float speed = (sprint ? 8.5f : aiming ? 2.6f : 4.6f) * dt;

  glm::vec3 f = forward(), r = right();
  f.y = 0.0f; if (glm::length(f) > 1e-4f) f = glm::normalize(f);
  glm::vec3 move(0.0f);
  if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) move += f;
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) move -= f;
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) move += r;
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) move -= r;
  if (glm::length(move) > 1e-4f) position += glm::normalize(move) * speed;

  if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) position.y += speed;
  if (glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS) position.y -= speed;
  position.y = std::max(position.y, 1.2f);
}
