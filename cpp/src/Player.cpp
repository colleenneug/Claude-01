#include "Player.h"
#include <algorithm>
#include <cmath>

namespace {
constexpr float GRAVITY = 18.0f;
constexpr float JUMP_SPEED = 6.0f;
constexpr float WALK_SPEED = 4.6f;
constexpr float SPRINT_SPEED = 8.2f;
constexpr float GROUND_ACCEL = 45.0f;
constexpr float AIR_ACCEL = 8.0f;
}

void Player::update(GLFWwindow* window, float dt, float yawRadians, bool sprint, const Level& level,
                     bool forceForward) {
  glm::vec3 fwd(std::cos(yawRadians), 0.0f, std::sin(yawRadians));
  glm::vec3 right(-fwd.z, 0.0f, fwd.x);

  glm::vec3 wish(0.0f);
  if (forceForward || glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) wish += fwd;
  if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) wish -= fwd;
  if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) wish += right;
  if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) wish -= right;
  if (glm::length(wish) > 1e-4f) wish = glm::normalize(wish);

  float targetSpeed = (sprint && grounded) ? SPRINT_SPEED : WALK_SPEED;
  glm::vec3 targetVel = wish * targetSpeed;
  float accel = grounded ? GROUND_ACCEL : AIR_ACCEL;
  glm::vec3 horizVel(velocity.x, 0.0f, velocity.z);
  glm::vec3 delta = targetVel - horizVel;
  float deltaLen = glm::length(delta);
  float step = accel * dt;
  if (deltaLen > 1e-5f) {
    glm::vec3 add = delta * std::min(1.0f, step / deltaLen);
    velocity.x += add.x;
    velocity.z += add.z;
  }

  if (grounded && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS) {
    velocity.y = JUMP_SPEED;
    grounded = false;
  }
  velocity.y -= GRAVITY * dt;

  // Substepped, the same reason the browser build substeps at boost speed:
  // a single large step at sprint velocity can skip clean over a thin
  // collider in one frame.
  const int SUBSTEPS = 4;
  glm::vec3 stepVel = velocity * (dt / SUBSTEPS);
  for (int i = 0; i < SUBSTEPS; i++) {
    position += stepVel;
    grounded = level.resolve(position, radius, height);
    if (grounded && velocity.y < 0.0f) velocity.y = 0.0f;
  }
}
